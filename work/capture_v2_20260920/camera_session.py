"""Finite camera prerequisite for one separately guarded wheel trial."""
import json
import math
from pathlib import Path
import subprocess
import sys
import time


class CameraError(RuntimeError):
    pass


class CameraSession:
    def __init__(self, script: Path, runtime: Path, record):
        self.record = record
        self.directory = record.directory / "camera"
        self.directory.mkdir(exist_ok=False)
        self.ready_path = self.directory / "wheel_ready.json"
        self.live_path = self.directory / "wheel_live.json"
        self.ready = None
        self.required = False
        self.process = None
        self.opened_at = time.monotonic()
        self.log = (self.directory / "process.log").open("wb")
        command = [sys.executable, str(script), "--seconds", "45", "--name", "wheel",
                   "--runtime-dir", str(runtime), "--output-dir", str(self.directory)]
        try:
            self.process = subprocess.Popen(command, stdout=self.log, stderr=subprocess.STDOUT,
                                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except BaseException:
            self.log.close()
            raise
        record.event("camera_started", pid=self.process.pid, command=command)

    def poll_ready(self):
        if self.process.poll() is not None:
            raise CameraError("Camera process ended before the trial started")
        if self.ready_path.exists():
            data = json.loads(self.ready_path.read_text(encoding="utf8"))
            stamp = float(data["host_monotonic_s"])
            if (data.get("event") != "CAMERA_READY" or not math.isfinite(stamp)
                    or stamp < self.opened_at or stamp > time.monotonic()
                    or time.monotonic() - stamp > 3 or data.get("frame_pts") is None):
                raise CameraError("Camera readiness is stale or invalid")
            self.ready = data
            self.check_fresh_video()
            self.record.event("camera_ready", **data)
            return True
        if time.monotonic() - self.opened_at > 15:
            raise CameraError("Camera did not decode a keyframe within the readiness deadline")
        return False

    def check_live(self):
        if self.required and self.process.poll() is not None:
            raise CameraError("Camera recording ended during the motor trial")
        if self.required:
            self.check_fresh_video()

    def check_fresh_video(self):
        try:
            live = json.loads(self.live_path.read_text(encoding="utf8"))
            stamp = float(live["host_monotonic_s"])
            if (not math.isfinite(stamp) or stamp < self.opened_at
                    or stamp > time.monotonic() or time.monotonic() - stamp > 2
                    or int(live["decoded_frames"]) < 1):
                raise CameraError("Camera video heartbeat is stale or invalid")
        except (OSError, ValueError, KeyError) as exc:
            raise CameraError("Camera video heartbeat is unavailable") from exc

    def release_requirement(self):
        self.required = False

    def close_log(self):
        # The child records at most 45 seconds after readiness, with separate
        # open/read/warm-up/byte bounds. Do not kill it during the motor trial.
        self.log.close()

    def result_status(self):
        code = self.process.poll()
        result = {"directory": str(self.directory), "process_id": self.process.pid,
                  "process_exit": code, "still_recording": code is None}
        final = self.directory / "wheel.json"
        if code is not None and final.exists():
            result["metadata"] = json.loads(final.read_text(encoding="utf8"))
        return result
