# forcestop_guard_20260922 - live party build + forceStop guard

Base: work/imbalance_20260921/prize_wheel_gpt as flashed 2026-09-22 21:31 (commit 470022c).
Three lines changed in prize_wheel_gpt.ino (AUDIT_2026-09-22.md, section 1); every other file is byte-identical:
- landingVerdict (line 2384): forceStop() only while the stepper is running. On an idle stepper,
  FastAccelStepper 1.2.7 keeps its one-shot immediate-stop flag armed, and it cancelled the next
  capture (PULSEFIRST reject=2 hz=0.0 after every landing).
- RECOVERY_NUDGE timeout (line 3168): same guard.
- Boot banner (line 2447): build string tagged forcestop-guard-20260922.

Compiled 22:47 (exit 0, 1693571 bytes, RAM unchanged). Flashed to COM7 22:48 (hash verified).
After boot: IDLE_STOPPED, fault NONE, tmc=1, takeover=1, dirCal=1, rawZero=1947, driver healthy.

Expect: no more PULSEFIRST reject=2 hz=0.0; the capture after each landing runs.
Rollback: run work/imbalance_20260921/upload_candidate.py (its firmware/ folder holds the 21:31
binaries on MILL-PC; binaries are not in git).
