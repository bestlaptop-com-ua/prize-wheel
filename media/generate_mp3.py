#!/usr/bin/env python3
"""Prize-wheel FX audio generator (PARTY_TASK.md Part 3).

Synthesizes the six DFPlayer tracks from scratch - no source material needed -
and encodes them to 44.1 kHz mono MP3 under media/mp3/:

  0001.mp3  dry wedge tick, ~80 ms
  0002.mp3  seamless ratchet loop, 3.0 s (48 ticks at exactly 16/s)
  0003.mp3  drumroll loop, 2.0 s (seamless)
  0004.mp3  win fanfare, ~2.6 s
  0005.mp3  idle ambience loop, 8.0 s (seamless; optional in firmware)
  0006.mp3  guest-stopped jingle, ~1.6 s (optional)

Copy the whole mp3/ folder to the microSD root (folder name must be "mp3",
files keep their 000N.mp3 names - the firmware plays them with the DFPlayer
"play from MP3 folder" command 0x12, which addresses files BY NAME, immune to
FAT copy order).

Encoding backends, tried in order:
  1. ffmpeg on PATH        (not present on the bench laptop as delivered)
  2. python -m pip install lameenc   (pure-wheel, works on Windows/Python 3.12)
If neither is available the script writes .wav files next to the target names
and prints what to install.  Synthesis itself is pure stdlib - deterministic
(seeded), so re-runs are bit-identical.
"""

import math
import os
import random
import struct
import subprocess
import shutil
import sys
import wave

SR = 44100
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mp3")
BITRATE_KBPS = 96

TWO_PI = 2.0 * math.pi


# --------------------------------------------------------------------------
# small synthesis toolkit (mono float lists in [-1, 1])
# --------------------------------------------------------------------------
def silence(seconds):
    return [0.0] * int(round(seconds * SR))


def mix_at(dest, src, at_seconds, gain=1.0):
    """Add src into dest starting at at_seconds (dest is extended if needed)."""
    start = int(round(at_seconds * SR))
    need = start + len(src)
    if need > len(dest):
        dest.extend([0.0] * (need - len(dest)))
    for i, v in enumerate(src):
        dest[start + i] += v * gain
    return dest


def env_exp(n, tau_s):
    """Exponential decay envelope of n samples with time constant tau."""
    k = math.exp(-1.0 / (tau_s * SR))
    e = 1.0
    out = []
    for _ in range(n):
        out.append(e)
        e *= k
    return out


def env_adsr(n, a, d, s_level, r):
    """Linear ADSR over n samples (times in seconds; sustain fills the rest)."""
    na, nd, nr = int(a * SR), int(d * SR), int(r * SR)
    ns = max(0, n - na - nd - nr)
    out = []
    for i in range(na):
        out.append(i / max(1, na))
    for i in range(nd):
        out.append(1.0 + (s_level - 1.0) * i / max(1, nd))
    out.extend([s_level] * ns)
    for i in range(nr):
        out.append(s_level * (1.0 - i / max(1, nr)))
    while len(out) < n:
        out.append(0.0)
    return out[:n]


def osc(n, freq, harmonics=((1, 1.0),), fm_depth=0.0, fm_rate=0.0, phase=0.0,
        freq_end=None):
    """Additive oscillator; optional linear glide to freq_end and vibrato."""
    out = []
    ph = phase
    for i in range(n):
        t = i / SR
        f = freq if freq_end is None else freq + (freq_end - freq) * (i / max(1, n - 1))
        if fm_depth:
            f *= 1.0 + fm_depth * math.sin(TWO_PI * fm_rate * t)
        ph += TWO_PI * f / SR
        v = 0.0
        for mult, amp in harmonics:
            v += amp * math.sin(ph * mult)
        out.append(v)
    return out


def noise(n, rng):
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def lowpass(sig, cutoff_hz):
    """One-pole lowpass."""
    if cutoff_hz >= SR / 2:
        return list(sig)
    a = 1.0 - math.exp(-TWO_PI * cutoff_hz / SR)
    y = 0.0
    out = []
    for v in sig:
        y += a * (v - y)
        out.append(y)
    return out


def highpass(sig, cutoff_hz):
    lp = lowpass(sig, cutoff_hz)
    return [v - l for v, l in zip(sig, lp)]


def gain(sig, g):
    return [v * g for v in sig]


def mul(sig, env):
    return [v * e for v, e in zip(sig, env)]


def softclip(sig, drive=1.0):
    return [math.tanh(v * drive) for v in sig]


def normalize(sig, peak=0.92):
    m = max(1e-9, max(abs(v) for v in sig))
    g = peak / m
    return [v * g for v in sig]


# --------------------------------------------------------------------------
# the six sounds
# --------------------------------------------------------------------------
def make_tick(rng, punch=1.0):
    """~80 ms dry wooden click: 3 ms noise transient + two damped modes."""
    n = int(0.080 * SR)
    click = mul(highpass(noise(int(0.004 * SR), rng), 1200),
                env_exp(int(0.004 * SR), 0.0012))
    body_hi = mul(osc(n, 1850 * (1.0 + rng.uniform(-0.02, 0.02))),
                  env_exp(n, 0.010))
    body_lo = mul(osc(n, 640 * (1.0 + rng.uniform(-0.03, 0.03))),
                  env_exp(n, 0.016))
    out = silence(0.080)
    mix_at(out, click, 0.0, 0.9 * punch)
    mix_at(out, body_hi, 0.001, 0.55 * punch)
    mix_at(out, body_lo, 0.001, 0.40 * punch)
    return softclip(out[:n], 1.4)


def make_tick_file():
    rng = random.Random(11)
    return normalize(make_tick(rng), 0.92)


def make_ratchet_loop():
    """3.0 s, exactly 48 ticks at 16/s; nothing rings across the loop point."""
    rng = random.Random(22)
    out = silence(3.0)
    n_total = len(out)
    for k in range(48):
        t = k / 16.0
        punch = 0.8 + 0.3 * rng.random()
        tick = make_tick(rng, punch)
        end = int(t * SR) + len(tick)
        if end > n_total:      # never ring across the wrap
            tick = tick[: n_total - int(t * SR)]
        mix_at(out, tick, t, 0.8)
    # faint mechanism bed, exactly periodic over 3 s (freqs are multiples
    # of 1/3 Hz) so the loop point stays seamless
    bed = gain(lowpass(noise(n_total, rng), 900), 0.02)
    lfo = [1.0 + 0.3 * math.sin(TWO_PI * (2.0 / 3.0) * (i / SR)) for i in range(n_total)]
    out = [o + b * l for o, b, l in zip(out, bed, lfo)]
    return normalize(out[:n_total], 0.9)


def make_drumroll_loop():
    """2.0 s snare roll, 32 hits/s, seamless (periodic LFO, no wrap ring)."""
    rng = random.Random(33)
    out = silence(2.0)
    n_total = len(out)
    hits = 64
    for k in range(hits):
        t = k / 32.0
        nh = int(0.030 * SR)
        burst = mul(highpass(noise(nh, rng), 1500), env_exp(nh, 0.006))
        bodyn = int(0.040 * SR)
        body = mul(osc(bodyn, 185 * (1 + rng.uniform(-0.02, 0.02))), env_exp(bodyn, 0.012))
        amp = 0.55 + 0.25 * math.sin(TWO_PI * k / 32.0) + 0.12 * rng.random()
        seg = silence(0.045)
        mix_at(seg, burst, 0.0, 0.8)
        mix_at(seg, body, 0.002, 0.5)
        end = int(t * SR) + len(seg)
        if end > n_total:
            seg = seg[: n_total - int(t * SR)]
        mix_at(out, seg, t, amp)
    return normalize(out[:n_total], 0.9)


def make_fanfare():
    """~2.6 s bright win fanfare: ascending arpeggio into a held chord."""
    brass = ((1, 1.0), (2, 0.55), (3, 0.32), (4, 0.18), (5, 0.09))
    notes = [523.25, 659.25, 783.99, 1046.5]        # C5 E5 G5 C6
    out = silence(2.6)
    for i, f in enumerate(notes):
        n = int(0.16 * SR)
        tone = mul(osc(n, f, brass), env_adsr(n, 0.01, 0.05, 0.75, 0.05))
        mix_at(out, tone, i * 0.13, 0.55)
    # held chord with vibrato + a shimmer octave
    nch = int(1.9 * SR)
    chord = silence(0.0)
    for f, g in ((523.25, 0.5), (659.25, 0.45), (783.99, 0.42), (1046.5, 0.30)):
        tone = mul(osc(nch, f, brass, fm_depth=0.006, fm_rate=5.5),
                   env_adsr(nch, 0.02, 0.25, 0.65, 0.55))
        chord = mix_at(chord, tone, 0.0, g)
    shimmer = mul(osc(nch, 2093.0, ((1, 1.0), (2, 0.2)), fm_depth=0.01, fm_rate=7.0),
                  env_adsr(nch, 0.30, 0.5, 0.35, 0.6))
    chord = mix_at(chord, shimmer, 0.0, 0.10)
    mix_at(out, chord, 0.52, 0.8)
    return normalize(softclip(out[: int(2.6 * SR)], 1.2), 0.92)


def make_ambience_loop():
    """8.0 s soft pad + air, every LFO period divides 8 s -> seamless loop."""
    rng = random.Random(55)
    n = int(8.0 * SR)
    out = [0.0] * n
    # slow-beating add9 cluster (C3 G3 C4 D4 E4), pure-ish tones
    for f, g, lfo_cycles in ((130.81, 0.30, 2), (196.00, 0.22, 3),
                             (261.63, 0.18, 4), (293.66, 0.12, 5),
                             (329.63, 0.12, 6)):
        tone = osc(n, f, ((1, 1.0), (2, 0.12)))
        for i in range(n):
            lfo = 0.75 + 0.25 * math.sin(TWO_PI * lfo_cycles * i / n)
            out[i] += tone[i] * g * lfo
    air = gain(lowpass(noise(n, rng), 500), 0.05)
    breathe = [1.0 + 0.5 * math.sin(TWO_PI * 2 * i / n) for i in range(n)]
    out = [o + a * b for o, a, b in zip(out, air, breathe)]
    return normalize(out, 0.5)      # deliberately quiet - background only


def make_guest_jingle():
    """~1.6 s gentle comedic 'aww' - two soft descending slides, no harshness."""
    horn = ((1, 1.0), (2, 0.4), (3, 0.2))
    out = silence(1.6)
    n1 = int(0.55 * SR)
    a = mul(osc(n1, 392.0, horn, fm_depth=0.012, fm_rate=6.0, freq_end=329.6),
            env_adsr(n1, 0.03, 0.1, 0.8, 0.15))
    n2 = int(0.85 * SR)
    b = mul(osc(n2, 329.6, horn, fm_depth=0.02, fm_rate=5.0, freq_end=246.9),
            env_adsr(n2, 0.03, 0.1, 0.75, 0.4))
    mix_at(out, a, 0.05, 0.6)
    mix_at(out, b, 0.62, 0.6)
    return normalize(softclip(out[: int(1.6 * SR)], 1.1), 0.85)


# --------------------------------------------------------------------------
# output
# --------------------------------------------------------------------------
def to_pcm16(sig):
    frames = bytearray()
    for v in sig:
        v = max(-1.0, min(1.0, v))
        frames += struct.pack("<h", int(v * 32767))
    return bytes(frames)


def write_wav(path, pcm):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm)


def encode_mp3(pcm, mp3_path):
    """Encode 16-bit mono PCM to MP3; returns True on success."""
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg:
        p = subprocess.run(
            [ffmpeg, "-y", "-loglevel", "error", "-f", "s16le", "-ar", str(SR),
             "-ac", "1", "-i", "pipe:0", "-codec:a", "libmp3lame",
             "-b:a", f"{BITRATE_KBPS}k", mp3_path],
            input=pcm)
        return p.returncode == 0
    try:
        import lameenc
    except ImportError:
        return False
    enc = lameenc.Encoder()
    enc.set_bit_rate(BITRATE_KBPS)
    enc.set_in_sample_rate(SR)
    enc.set_channels(1)
    enc.set_quality(2)
    data = enc.encode(pcm) + enc.flush()
    with open(mp3_path, "wb") as f:
        f.write(data)
    return True


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    tracks = [
        ("0001", make_tick_file, "wedge tick ~80 ms"),
        ("0002", make_ratchet_loop, "ratchet loop 3.0 s seamless"),
        ("0003", make_drumroll_loop, "drumroll loop 2.0 s seamless"),
        ("0004", make_fanfare, "win fanfare ~2.6 s"),
        ("0005", make_ambience_loop, "idle ambience loop 8.0 s (optional)"),
        ("0006", make_guest_jingle, "guest-stopped jingle ~1.6 s (optional)"),
    ]
    total = 0
    any_wav = False
    for name, fn, desc in tracks:
        sig = fn()
        pcm = to_pcm16(sig)
        mp3_path = os.path.join(OUT_DIR, f"{name}.mp3")
        if encode_mp3(pcm, mp3_path):
            size = os.path.getsize(mp3_path)
            total += size
            print(f"{name}.mp3  {size:7d} B  {len(sig)/SR:5.2f} s  {desc}")
        else:
            wav_path = os.path.join(OUT_DIR, f"{name}.wav")
            write_wav(wav_path, pcm)
            any_wav = True
            print(f"{name}.wav  (no MP3 encoder found)  {desc}")
    if any_wav:
        print("\nNo MP3 encoder available. Install one of:")
        print("  - ffmpeg on PATH, or")
        print("  - python -m pip install lameenc")
        print("then re-run this script.")
        sys.exit(1)
    print(f"\nTotal {total} bytes ({total/1024:.0f} KiB); budget < 2 MiB: "
          f"{'OK' if total < 2 * 1024 * 1024 else 'EXCEEDED'}")


if __name__ == "__main__":
    main()
