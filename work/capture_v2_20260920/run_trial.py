#!/usr/bin/env python3
"""Inspect by default; --execute sends exactly one guarded self-spin request.

Requires pyserial. Never discovers ports, toggles reset lines, uploads, retries
motion, clears faults, or automatically runs the opposite direction. The local
firmware watchdog is authoritative; a Python process/USB connection is not a
physical emergency stop. Run only after the separate physical clearance.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import re
import sys
import time
from datetime import datetime, timezone
from typing import Any
import uuid
from camera_session import CameraSession, CameraError

BUILD = "# build: v2-capture-pulsefirst-20260920; based on gentle6"
FIRMWARE_LIMIT_S = 25.0
HOST_LIMIT_S = FIRMWARE_LIMIT_S + 1.0
START_LIMIT_S = 2.0
QUERY_LIMIT_S = 3.0
QUIET_RECOVERY_S = 2.0  # measured board needs encoder re-prime + 1s idle after verbose serial
FIELDS = re.compile(r"\b([A-Za-z_]+)=([^\s;(]+)")


class Refusal(RuntimeError):
    pass


def fields(line: str) -> dict[str, str]:
    return dict(FIELDS.findall(line))


def number(data: dict[str, str], key: str) -> float:
    try:
        value = float(data[key])
    except (KeyError, ValueError) as exc:
        raise Refusal(f"Missing or invalid status field: {key}") from exc
    if not math.isfinite(value):
        raise Refusal(f"Non-finite status field: {key}")
    return value


def readiness(status: dict[str, dict[str, str]], *, armed: bool,
              require_enabled: bool = True) -> list[str]:
    reasons: list[str] = []
    state, motor, test = (status.get(k, {}) for k in ("state", "motor", "selfspin"))
    try:
        for key, expected in (("state", "IDLE_STOPPED"), ("fault", "NONE"),
                              ("pos", "FRESH"), ("vel", "VALID")):
            if state.get(key) != expected:
                reasons.append(f"{key} must be {expected}")
        for key, expected in (("dirCal", 1), ("tmc", 1), ("takeover", 0), ("stage", 0)):
            if number(state, key) != expected:
                reasons.append(f"{key} must be {expected}")
        if abs(number(state, "omega")) > .020 or abs(number(state, "cmd")) > .0005:
            reasons.append("wheel or command is not stationary")
        if number(state, "age_us") >= 50000:
            reasons.append("encoder sample is too old")
        if number(motor, "EN") != 1 or number(motor, "current") != 0 or abs(number(motor, "fas")) > .001:
            reasons.append("outputs/pulse generator are not disabled and stopped")
        expected = {"timer_ready": 1, "consumed": 0, "active": 0, "inhibit": 1,
                    "phase": 0, "diag_buffer": 1, "diag_frozen": 0,
                    "dump_active": 0, "deadline_reason": 0}
        if require_enabled:
            expected["motion_compiled"] = 1
        else:
            number(test, "motion_compiled")
        if armed:
            expected["diag_armed"] = 1
        for key, value in expected.items():
            if number(test, key) != value:
                reasons.append(f"selfspin {key} must be {value}")
        if number(test, "idle_ms") < 1000:
            reasons.append("one-second stable-idle qualification is incomplete")
    except Refusal as exc:
        reasons.append(str(exc))
    return reasons


def disabled(status: dict[str, dict[str, str]]) -> bool:
    try:
        state, motor, test = (status[k] for k in ("state", "motor", "selfspin"))
        return (number(motor, "EN") == 1 and number(motor, "current") == 0
                and abs(number(motor, "fas")) <= .001 and number(state, "stage") == 0
                and number(test, "active") == 0 and number(test, "inhibit") == 1
                and number(test, "consumed") == 1)
    except (KeyError, Refusal):
        return False


class Recorder:
    def __init__(self, directory: Path):
        directory.mkdir(parents=True, exist_ok=False)
        self.directory = directory
        self.raw = (directory / "serial.raw").open("wb")
        self.events = (directory / "events.jsonl").open("w", encoding="utf-8", buffering=1)
        self.csv_file = (directory / "trace.csv").open("w", encoding="utf-8", newline="")
        self.csv_writer = csv.writer(self.csv_file)
        self.buffer = bytearray()
        self.lines: list[tuple[float, str]] = []
        self.trace_after = math.inf
        self.columns: list[str] | None = None
        self.rows = 0
        self.bad_rows = 0
        self.dump_complete: tuple[float, int] | None = None
        self.last_received = time.monotonic()

    def event(self, kind: str, **data: Any) -> None:
        self.events.write(json.dumps({"utc": datetime.now(timezone.utc).isoformat(),
                                     "monotonic": time.monotonic(), "kind": kind, **data}) + "\n")

    def receive(self, chunk: bytes) -> None:
        if not chunk:
            return
        self.last_received = time.monotonic()
        self.raw.write(chunk)
        self.raw.flush()
        self.buffer.extend(chunk)
        if len(self.buffer) > 262144:
            raise Refusal("Serial line buffer exceeded limit; raw data retained")
        while b"\n" in self.buffer:
            row, _, tail = self.buffer.partition(b"\n")
            self.buffer = bytearray(tail)
            line = row.rstrip(b"\r").decode("utf-8", errors="replace")
            now = time.monotonic()
            self.lines.append((now, line))
            self.event("receive", line=line)
            if now < self.trace_after:
                continue
            if line.startswith("# DIAG columns: "):
                if self.columns is not None:
                    raise Refusal("Unexpected second diagnostic block; raw data retained")
                self.columns = line.removeprefix("# DIAG columns: ").split(",")
                self.csv_writer.writerow(["record", *self.columns])
            elif line.startswith("D,"):
                cells = next(csv.reader([line]))
                self.csv_writer.writerow(cells)
                self.rows += 1
                if self.columns is None or len(cells) != len(self.columns) + 1:
                    self.bad_rows += 1
            elif match := re.fullmatch(r"# DIAG n=(\d+) wrapped=[01] frozen=[01]", line):
                self.dump_complete = (now, int(match.group(1)))
                self.csv_file.flush()

    def close(self) -> None:
        self.raw.close()
        self.events.close()
        self.csv_file.close()


class Session:
    def __init__(self, port: Any, record: Recorder):
        self.port = port
        self.record = record
        self.trigger_attempted = False
        self.abort_attempted = False
        self.camera = None
        self.command_line_start = 0
        self.abort_sent_at = None

    def pump(self) -> None:
        count = min(max(self.port.in_waiting, 1), 4096)
        self.record.receive(self.port.read(count))
        if self.camera is not None and self.trigger_attempted and not self.abort_attempted:
            try:
                self.camera.check_live()
            except CameraError as exc:
                raise Refusal(str(exc)) from exc

    def read_for(self, seconds: float) -> None:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.pump()

    def drain_existing(self) -> None:
        # Preserve existing bytes instead of reset_input_buffer(). Do not use
        # old queued status as evidence of a fresh command response.
        end, quiet_since = time.monotonic() + 2, time.monotonic()
        while time.monotonic() < end:
            before = len(self.record.lines)
            self.pump()
            if len(self.record.lines) != before or self.port.in_waiting:
                quiet_since = time.monotonic()
            elif time.monotonic() - quiet_since >= .2 and not self.record.buffer:
                return
        raise Refusal("Serial stream was already busy/incomplete; no experiment requested")

    def send(self, command: str) -> float:
        if len(command) != 1:
            raise ValueError("Only a single character may be transmitted")
        mark = time.monotonic()
        self.command_line_start = len(self.record.lines)
        self.record.event("send_attempt", command=command)
        if self.port.write(command.encode("ascii")) != 1:
            raise Refusal("Short serial write; delivery is uncertain")
        self.record.event("send_complete", command=command)
        return mark

    def wait_line(self, predicate: Any, after: float, timeout: float) -> tuple[float, str]:
        end = time.monotonic() + timeout
        # Windows monotonic timestamps may be equal for adjacent commands.
        # A response must also belong to the current command's receive range.
        cursor = self.command_line_start
        while time.monotonic() < end:
            for stamp, line in self.record.lines[cursor:]:
                if stamp >= after and predicate(line):
                    return stamp, line
            cursor = len(self.record.lines)
            self.pump()
        raise Refusal("Timed out awaiting required serial response")

    def status(self) -> dict[str, dict[str, str]]:
        mark = self.send("s")
        result: dict[str, dict[str, str]] = {}
        for category in ("state", "motor", "selfspin"):
            _, line = self.wait_line(lambda s, k=category: s.startswith(f"# {k}" + ("=" if k == "state" else " ")),
                                     mark, QUERY_LIMIT_S)
            result[category] = fields(line)
        self.record.event("status", status=result)
        return result

    def abort_once(self, reason: str) -> None:
        if not self.trigger_attempted or self.abort_attempted:
            return
        self.abort_attempted = True
        self.abort_sent_at = time.monotonic()
        self.command_line_start = len(self.record.lines)
        delivery: str = "write completed; device disable still unverified"
        # Write first: a disk/logging failure must not prevent the abort attempt.
        try:
            if self.port.write(b"!") != 1:
                delivery = "short write; delivery unconfirmed"
        except BaseException as exc:
            delivery = "write failed; delivery unconfirmed: " + repr(exc)
        try:
            self.record.event("abort_attempted", reason=reason, delivery=delivery)
            end = time.monotonic() + 2
            while time.monotonic() < end:
                self.pump()
        except BaseException:
            pass


def run(args: argparse.Namespace, session: Session) -> dict[str, Any]:
    record = session.record
    session.drain_existing()
    mark = session.send("?")
    session.wait_line(lambda line: line == BUILD, mark, QUERY_LIMIT_S)
    session.read_for(QUIET_RECOVERY_S)  # full help can block the encoder sampler
    status = session.status()
    reasons = readiness(status, armed=False)
    if not args.execute:
        return {"mode": "inspect", "build": BUILD, "status": status,
                "execute_readiness_issues": reasons, "motion_command_sent": False}
    if reasons:
        raise Refusal("Preflight rejected: " + "; ".join(reasons))

    session.read_for(QUIET_RECOVERY_S)
    mark = session.send("d")
    session.wait_line(lambda line: line.startswith("# DIAG armed: 16384 PSRAM samples")
                      or line == "# DIAG already armed", mark, QUERY_LIMIT_S)
    session.read_for(QUIET_RECOVERY_S)
    status = session.status()
    reasons = readiness(status, armed=True)
    if reasons:
        raise Refusal("Armed preflight rejected: " + "; ".join(reasons))
    # Printing status itself can perturb sampling. Do not poll repeatedly and
    # perpetuate re-prime; let firmware recover, then its start guard rechecks.
    session.read_for(QUIET_RECOVERY_S)

    if not args.camera_runtime or not args.camera_runtime.is_dir():
        raise Refusal("An explicit installed camera runtime is required for execution")
    session.camera = CameraSession(Path(__file__).with_name("capture_camera.py"),
                                   args.camera_runtime, record)
    while not session.camera.poll_ready():
        session.pump()
    session.camera.required = True
    session.camera.check_live()

    # Set BEFORE write: a serial exception may occur after the device saw it.
    session.trigger_attempted = True
    started_at = record.trace_after = time.monotonic()
    session.send("[" if args.direction == -1 else "]")
    problem = None
    terminal = lambda line: line.startswith("# SELFSPIN SEQUENCE_DONE ") or line.startswith("# SELFSPIN ABORT ")
    try:
        _, first = session.wait_line(lambda line: line.startswith("# SELFSPIN START ")
                                    or line.startswith("# SELFSPIN refused")
                                    or line.startswith("# SELFSPIN ABORT "),
                                    started_at, START_LIMIT_S)
        expected = re.fullmatch(r"# SELFSPIN START encoder_dir=([+-]?\d+) target_hz=640 accel=160 finite_steps=3200", first)
        if not expected or int(expected.group(1)) != args.direction:
            raise Refusal("No matching start confirmation: " + first)
        remaining = HOST_LIMIT_S - (time.monotonic() - started_at)
        _, outcome = session.wait_line(terminal, started_at, max(0, remaining))
    except Refusal as exc:
        problem = str(exc)
        session.abort_once(problem)
        # No new trigger: only collect an abort acknowledgement and its trace.
        _, outcome = session.wait_line(terminal, started_at, 2)
    if fields(outcome).get("EN") != "1":
        problem = "Terminal event does not report EN high"
        session.abort_once(problem)
        try:
            _, outcome = session.wait_line(
                lambda line: terminal(line) and fields(line).get("EN") == "1",
                session.abort_sent_at, 2)
        except Refusal as exc:
            raise Refusal("Shutdown unverified: no EN-high acknowledgement after abort") from exc
    record.event("terminal_en_high_readback", line=outcome)
    session.camera.release_requirement()

    # No s polling during motion. The terminal event reads EN directly after
    # sticky inhibition. Allow minutes for subsequent coast/CSV transmission.
    dump_deadline = time.monotonic() + args.dump_timeout
    initial_dump_wait = time.monotonic() + 5
    while record.columns is None and time.monotonic() < initial_dump_wait:
        session.pump()
    if (record.columns is None and not record.buffer and not session.port.in_waiting
            and time.monotonic() - record.last_received >= 2):
        # A refused/very early attempt may have no motion-triggered auto dump.
        # One read-only check; D is sent only if the hardware reports stopped,
        # fresh, and disabled. Firmware applies its own same guards to D.
        status = session.status()
        state = status["state"]
        if (disabled(status) and state.get("pos") == "FRESH" and state.get("vel") == "VALID"
                and abs(number(state, "omega")) <= .020 and record.columns is None
                and number(status["selfspin"], "dump_active") == 0):
            session.send("D")
    while record.dump_complete is None and time.monotonic() < dump_deadline:
        session.pump()
    if record.dump_complete is None:
        raise Refusal("Diagnostic dump did not finish; partial raw/CSV data retained")
    expected_rows = record.dump_complete[1]
    if record.columns is None or record.rows != expected_rows or record.bad_rows:
        raise Refusal(f"Incomplete diagnostic CSV: {record.rows}/{expected_rows}, malformed={record.bad_rows}")
    session.read_for(QUIET_RECOVERY_S)
    final_status = session.status()
    if not disabled(final_status):
        raise Refusal("Final output-disable check failed")
    return {"mode": "execute", "motion_command_sent": True, "direction": args.direction,
            "outcome_line": outcome, "status": final_status, "trace_rows": record.rows,
            "sequence_aborted": bool(problem) or outcome.startswith("# SELFSPIN ABORT "),
            "motion_problem": problem,
            "note": "Sequence completion does not establish quiet or accurate capture."}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Explicit serial port; never auto-detected")
    parser.add_argument("--direction", required=True, type=int, choices=(-1, 1),
                        help="Encoder direction, not an assumed physical CW/CCW label")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--inspect", action="store_true", help="Read-only status (the default)")
    mode.add_argument("--execute", action="store_true", help="After guarded preflight, send ONE bracket")
    parser.add_argument("--log-dir", type=Path, help="New directory; refuses to overwrite existing logs")
    parser.add_argument("--camera-runtime", type=Path,
                        help="Explicit PyAV runtime directory; required for --execute")
    parser.add_argument("--dump-timeout", type=float, default=480,
                        help="Seconds for coast/data dump AFTER terminal EN-high report, 30..600 (default 480)")
    args = parser.parse_args()
    if not 30 <= args.dump_timeout <= 600:
        parser.error("--dump-timeout must be between 30 and 600 seconds")
    directory = args.log_dir or Path(__file__).resolve().parent / "trial_logs" / (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid.uuid4().hex[:8])
    record = Recorder(directory)
    serial_port = None
    session = None
    result: dict[str, Any] = {"mode": "execute" if args.execute else "inspect", "port": args.port}
    exit_code = 1
    try:
        import serial
        # Configure inactive modem lines BEFORE opening; no reset toggling.
        serial_port = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=.5)
        serial_port.dtr = False
        serial_port.rts = False
        serial_port.port = args.port
        record.event("open_attempt", port=args.port, dtr=False, rts=False, execute=args.execute)
        serial_port.open()  # exactly one open; no auto reconnect/retry
        session = Session(serial_port, record)
        result.update(run(args, session))
        exit_code = 1 if result.get("sequence_aborted") else 0
    except BaseException as exc:
        result["error"] = repr(exc)
        if session is not None:
            session.abort_once(str(exc))
            result["motion_command_attempted"] = session.trigger_attempted
            result["abort_attempted"] = session.abort_attempted
        try:
            record.event("exception", error=repr(exc))
        except BaseException:
            pass
        exit_code = 130 if isinstance(exc, KeyboardInterrupt) else 1
    finally:
        if session is not None and session.camera is not None:
            session.camera.release_requirement()
            result["camera"] = session.camera.result_status()
            session.camera.close_log()
        if serial_port is not None and serial_port.is_open:
            try:
                serial_port.close()
            except BaseException as exc:
                result["close_error"] = repr(exc)
        result["logs"] = str(directory.resolve())
        (directory / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        record.close()
    print(json.dumps(result, indent=2))
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
