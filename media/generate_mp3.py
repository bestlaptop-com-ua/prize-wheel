#!/usr/bin/env python3
"""Generate the six deterministic, original DFPlayer party tracks.

Requires Python 3 and lameenc (see requirements.txt). The script creates
44.1 kHz mono MP3 files in media/mp3 without downloading source audio.
"""

from __future__ import annotations

import math
import random
from array import array
from pathlib import Path

import lameenc


SAMPLE_RATE = 44_100
BIT_RATE_KBPS = 96
OUTPUT_DIR = Path(__file__).resolve().parent / "mp3"


def blank(seconds: float) -> list[float]:
    return [0.0] * round(seconds * SAMPLE_RATE)


def add_tone(
    samples: list[float],
    start: float,
    duration: float,
    frequency: float,
    amplitude: float,
    attack: float = 0.005,
    release: float = 0.04,
    harmonic: float = 0.0,
) -> None:
    first = round(start * SAMPLE_RATE)
    count = min(round(duration * SAMPLE_RATE), len(samples) - first)
    for offset in range(max(0, count)):
        t = offset / SAMPLE_RATE
        envelope = min(1.0, t / max(attack, 1e-5))
        envelope *= min(1.0, (duration - t) / max(release, 1e-5))
        fundamental = math.sin(math.tau * frequency * t)
        overtone = math.sin(math.tau * frequency * 2.0 * t)
        samples[first + offset] += amplitude * envelope * (
            fundamental + harmonic * overtone
        )


def add_noise(
    samples: list[float],
    rng: random.Random,
    start: float,
    duration: float,
    amplitude: float,
    decay: float,
    click_frequency: float = 0.0,
) -> None:
    first = round(start * SAMPLE_RATE)
    count = min(round(duration * SAMPLE_RATE), len(samples) - first)
    previous = 0.0
    for offset in range(max(0, count)):
        t = offset / SAMPLE_RATE
        white = rng.uniform(-1.0, 1.0)
        high_pass = white - 0.72 * previous
        previous = white
        envelope = math.exp(-t / decay)
        click = (
            0.35 * math.sin(math.tau * click_frequency * t)
            if click_frequency
            else 0.0
        )
        samples[first + offset] += amplitude * envelope * (high_pass + click)


def tick_track(rng: random.Random) -> list[float]:
    # LAME/MP3 framing contributes about 48 ms; a 30 ms impulse produces an
    # approximately 80 ms file on the DFPlayer without a wet reverb tail.
    samples = blank(0.030)
    add_noise(samples, rng, 0.0, 0.029, 0.82, 0.007, 1_900.0)
    add_tone(samples, 0.0, 0.027, 1_250.0, 0.22, 0.001, 0.008)
    return samples


def ratchet_track(rng: random.Random) -> list[float]:
    # Exactly 30 identical rhythmic cells: the seam is another ordinary tick.
    samples = blank(3.0)
    for index in range(30):
        start = index * 0.100
        add_noise(samples, rng, start, 0.052, 0.45, 0.009, 1_450.0)
        add_tone(samples, start, 0.040, 980.0, 0.10, 0.001, 0.010)
    return samples


def drumroll_track(rng: random.Random) -> list[float]:
    # Forty equal cells form a loopable three-second roll.
    samples = blank(3.0)
    for index in range(40):
        start = index * 0.075
        accent = 0.42 if index % 4 == 0 else 0.28
        add_noise(samples, rng, start, 0.065, accent, 0.027)
        add_tone(samples, start, 0.050, 170.0, 0.09, 0.002, 0.025)
    return samples


def fanfare_track(_: random.Random) -> list[float]:
    samples = blank(2.60)
    notes = [
        (0.00, 0.38, 523.25),
        (0.32, 0.38, 659.25),
        (0.64, 0.38, 783.99),
        (0.96, 0.42, 1_046.50),
    ]
    for start, duration, frequency in notes:
        add_tone(samples, start, duration, frequency, 0.32, harmonic=0.22)
        add_tone(samples, start, duration, frequency / 2.0, 0.12, harmonic=0.10)
    for frequency in (523.25, 659.25, 783.99, 1_046.50):
        add_tone(samples, 1.38, 1.16, frequency, 0.16, 0.015, 0.24, 0.15)
    return samples


def ambience_track(_: random.Random) -> list[float]:
    # Frequencies complete whole cycles over three seconds; a slow raised-
    # cosine modulation keeps the loop boundary quiet and deterministic.
    samples = blank(3.0)
    frequencies = (110.0, 165.0, 220.0, 330.0)
    amplitudes = (0.11, 0.07, 0.055, 0.025)
    for index in range(len(samples)):
        t = index / SAMPLE_RATE
        modulation = 0.72 + 0.18 * math.cos(math.tau * t / 3.0)
        samples[index] = modulation * sum(
            amplitude * math.sin(math.tau * frequency * t)
            for frequency, amplitude in zip(frequencies, amplitudes)
        )
    return samples


def guest_jingle(_: random.Random) -> list[float]:
    samples = blank(1.85)
    notes = [
        (0.00, 0.43, 392.00),
        (0.36, 0.43, 349.23),
        (0.72, 0.48, 293.66),
        (1.12, 0.70, 196.00),
    ]
    for start, duration, frequency in notes:
        add_tone(samples, start, duration, frequency, 0.30, 0.008, 0.10, 0.24)
        add_tone(samples, start, duration, frequency / 2.0, 0.10, 0.008, 0.10)
    return samples


def encode_mp3(samples: list[float], destination: Path) -> None:
    peak = max((abs(value) for value in samples), default=1.0)
    gain = 0.92 / max(peak, 0.001)
    pcm = array(
        "h",
        (
            max(-32_768, min(32_767, round(value * gain * 32_767)))
            for value in samples
        ),
    )
    encoder = lameenc.Encoder()
    encoder.set_bit_rate(BIT_RATE_KBPS)
    encoder.set_in_sample_rate(SAMPLE_RATE)
    encoder.set_channels(1)
    encoder.set_quality(2)
    encoded = encoder.encode(pcm.tobytes()) + encoder.flush()
    destination.write_bytes(encoded)


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    builders = [
        tick_track,
        ratchet_track,
        drumroll_track,
        fanfare_track,
        ambience_track,
        guest_jingle,
    ]
    total = 0
    for number, builder in enumerate(builders, start=1):
        rng = random.Random(0x505700 + number)
        destination = OUTPUT_DIR / f"{number:04d}.mp3"
        encode_mp3(builder(rng), destination)
        size = destination.stat().st_size
        total += size
        print(f"{destination.name}: {size} bytes")
    print(f"total: {total} bytes")
    if total >= 2_000_000:
        raise SystemExit("generated MP3 set exceeds the 2 MB party limit")


if __name__ == "__main__":
    main()
