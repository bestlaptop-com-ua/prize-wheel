"""Verify disabled firmware; optional explicit recovery of known diagnostic abort."""
import argparse
import json
from pathlib import Path
import time
import serial
from run_trial import BUILD, Recorder, Session, Refusal, number


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log-dir", type=Path, required=True)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--recover-known-abort", action="store_true")
    args = parser.parse_args()
    record = Recorder(args.log_dir)
    port = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=.5)
    port.dtr = port.rts = False
    port.port = args.port
    session = Session(port, record)
    result = {"build": BUILD, "recovery_requested": args.recover_known_abort}
    probe_pending = abort_sent = False

    def off_status():
        status = session.status()
        state, motor, test = (status[k] for k in ("state", "motor", "selfspin"))
        if not (number(test, "motion_compiled") == 0 and number(test, "active") == 0
                and number(test, "inhibit") == 1 and number(test, "timer_ready") == 1
                and number(test, "recovery_revision") == 2 and number(test, "recovery_pending") == 0
                and number(motor, "EN") == 1 and number(motor, "current") == 0
                and abs(number(motor, "fas")) < .001 and number(state, "stage") == 0
                and number(state, "takeover") == 0 and number(state, "tmc") == 1
                and state.get("pos") == "FRESH" and state.get("vel") == "VALID"
                and abs(number(state, "omega")) <= .020):
            raise Refusal("Disabled/healthy/stationary status not established")
        return status

    try:
        port.open()
        session.drain_existing()
        mark = session.send("?")
        session.wait_line(lambda line: line == BUILD, mark, 3)
        session.read_for(2)
        before = off_status()
        result["before"] = before
        session.read_for(2)
        probe_pending = True
        mark = session.send("[")
        _, line = session.wait_line(lambda line: line.startswith("# SELFSPIN refused:")
                                   or line.startswith("# SELFSPIN START "), mark, 3)
        if not line.startswith("# SELFSPIN refused:"):
            abort_sent = True
            port.write(b"!")
            raise Refusal("Unexpected start in disabled firmware; abort sent")
        probe_pending = False
        result["disabled_start_refused"] = True
        session.read_for(2)
        unchanged = off_status()
        if unchanged["state"].get("fault") != before["state"].get("fault"):
            raise Refusal("Saved fault changed during disabled verification")
        if args.recover_known_abort:
            if (before["state"].get("state") != "FAULT_LATCHED"
                    or before["state"].get("fault") != "SELFSPIN_ABORT"):
                raise Refusal("Recovery requires the exact known diagnostic abort")
            session.read_for(2)
            mark = session.send("r")
            _, line = session.wait_line(lambda line: line.startswith("# RECOVERY "), mark, 4)
            if not line.startswith("# RECOVERY known SELFSPIN_ABORT cleared;"):
                raise Refusal("Firmware refused explicit known-abort recovery: " + line)
            session.read_for(2)
            after = off_status()
            if not (after["state"].get("state") == "IDLE_STOPPED"
                    and after["state"].get("fault") == "NONE"
                    and number(after["selfspin"], "consumed") == 1):
                raise Refusal("Recovery did not end in disabled, consumed idle")
            result["after"] = after
            result["known_abort_cleared"] = True
        result["success"] = True
    except BaseException as exc:
        result.update(success=False, error=repr(exc))
        if probe_pending and not abort_sent:
            abort_sent = True
            try:
                port.write(b"!")
                result["uncertain_probe_abort"] = "sent; shutdown not yet verified"
            except BaseException:
                result["uncertain_probe_abort"] = "delivery unconfirmed"
    finally:
        if port.is_open:
            port.close()
        (record.directory / "result.json").write_text(json.dumps(result, indent=2))
        record.close()
    print(json.dumps(result, indent=2))
    return 0 if result.get("success") else 1


if __name__ == "__main__":
    raise SystemExit(main())
