# Validation performed, 20 September 2026

The integration remains **unvalidated on the physical wheel**, with takeover
off at boot. It was compiled but not uploaded. The board instead remains on
the separate motion-disabled audit with diagnostic fault15 retained.

## Host checks

`tests/run_host_tests.cmd` passed with MSVC C++17 `/W4 /WX`:

- Migration/reboot persistence, friction fit lifecycle, all 256 fault bytes and
  all 65,536 primary-fault/recovery-journal pairs.
- Actual capture helpers for rest/HOLD ownership, pulse proof, independent
  arming lease, coherent braking, absolute overspeed and sustained speed-up.

GPIO/timer stubs check logic rather than ESP timing. Independent source review
checked fixed-target ownership, one enable edge, current/EN ordering and
periodic driver-health handling. Neither review nor tests validates rotor
phase, true disc position, torque, temperatures or retained prize behavior.

## Actual target compilation

Arduino CLI compiled an isolated, source-hash-verified copy on the wheel PC,
using the exact dependencies/FQBN in [BUILD_PRODUCTION.md](BUILD_PRODUCTION.md),
with `--warnings all`. Exit code0; program900499 bytes, static RAM114816 bytes.
Warnings originate in the pinned dependency/core code (missing initializers,
deprecated GPIO API); no project-source error was reported.

| Artifact | SHA-256 |
| --- | --- |
| Frozen sketch bytes | `2ce791c640288c85067c7a71c3e0fb6ac7a3832a4b798dc9a56bf1df7daa7df6` |
| Source transfer archive, including isolated compile helper | `148bf844ad3bc7212a4d61f02dedd15119803a787d7c3a476d959ea5ee455945` |
| Application binary | `2e8183a12aee4a5171244ccbd31e3693b2b17d42e8147aba8f7c01a61648e92b` |
| Merged binary | `4fac5b984c9747580daa9e9c2e28e0f7991b63986142dcf71a6e51223cfa07f2` |
| Local build-log/manifest evidence archive | `4ef24ee4269daecd96e7878eb4f09e1443d6347b86a430a580e2e171b5cdcd73` |

Help identity is `plywood-capture-review-20260920; takeover defaults OFF`.
Byte hashes describe the actual transferred files; line-ending conversion
on another checkout can change source-byte hashes without changing code.

See [hardware findings and remaining work](HARDWARE_REVIEW_2026-09-20.md).
