from pathlib import Path
import shutil
p=Path(__file__).parent;root=p.parent
q=root/'prize_wheel_gpt';backup=p/'before-brake2200';backup.mkdir(exist_ok=True)
for name in ('prize_wheel_gpt.ino','pw_party_impl.h'):shutil.copy2(q/name,backup/name)
s=(q/'prize_wheel_gpt.ino').read_text()
def swap(a,b):
 global s
 assert a in s,a
 s=s.replace(a,b,1)
swap('  FC_LANDING_UNSAFE        // settled on a dare after a controlled attempt','  FC_LANDING_UNSAFE,       // settled on a dare after a controlled attempt\n  FC_TRACKING_LOST         // continued travel after the commanded stop')
swap('const uint16_t CUR_BRAKE_MA     = 1650;','const uint16_t CUR_BRAKE_MA     = 2200; // retain capture torque through deceleration')
swap('const uint16_t CUR_HOLD1_MA     = 1650; // NEMA23: continuous hold at existing brake current','const uint16_t CUR_HOLD1_MA     = 1650; // continuous hold after confirmed stillness')
swap('    case FC_LANDING_UNSAFE: return "LANDING_UNSAFE";','    case FC_LANDING_UNSAFE: return "LANDING_UNSAFE";\n    case FC_TRACKING_LOST: return "TRACKING_LOST";')
a=s.index('      // Residual momentum may legitimately creep')
b=s.index('      if (encoderMotionReady() && fabsf(omega) <= STILL_REV_S)',a)
s=s[:a]+'''      // Excess travel after pulses stop is a failed controlled landing.
      // Motion alone does not prove guest contact; latch and preserve the
      // trace instead of automatically capturing the same coast again.
      if (fabsf(degreesForCounts(encoderCountsMT - settleEntryCounts)) >
              LANDING_DRAG_ABORT_DEG &&
          encoderMotionReady() && fabsf(omega) > STILL_REV_S) {
        enterFault(FC_TRACKING_LOST, "excess travel after pulse stop");
        break;
      }
'''+s[b:]
swap('v2-speedup-filter-20260918; smooth ramp; robust speedup; hold1650; 16K trace','v2-brake2200-20260918; smooth ramp; brake2200; hold1650; tracking-loss latch')
(q/'prize_wheel_gpt.ino').write_bytes(s.replace('\n','\r\n').encode())
s=(q/'pw_party_impl.h').read_text();needle='stored <= (uint8_t)FC_LANDING_UNSAFE';assert needle in s
s=s.replace(needle,'stored <= (uint8_t)FC_TRACKING_LOST')
(q/'pw_party_impl.h').write_bytes(s.replace('\n','\r\n').encode())
b=(p/'compile_smooth_wheel.py').read_text().replace('smooth_wheel','brake2200_wheel').replace('smooth_build.error','brake2200_build.error')
(p/'compile_brake2200.py').write_text(b)
# Prepare upload with no automatic fault clear; expected pre-update state is healthy hold.
u=(p/'upload_verify_speedup.py').read_text().replace('speedup_wheel','brake2200_wheel').replace('v2-speedup-filter-20260918','v2-brake2200-20260918')
a=u.index('  # Only the known pre-update fault')
b=u.index("  assert 'state=IDLE_STOPPED fault=NONE'",a)
u=u[:a]+u[b:]
(p/'upload_verify_brake2200.py').write_text(u)
r=(p/'speedup_capture.py').read_text().replace('speedup_','brake2200_')
(p/'brake2200_capture.py').write_text(r)
(p/'brake2200_commands.txt').write_text('?\ns\nd\n')
print('Prepared brake2200 firmware; capture and hold settings unchanged; TRACKING_LOST appended with NVS restore support')