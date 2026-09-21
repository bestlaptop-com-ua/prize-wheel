from pathlib import Path
import shutil
p=Path(__file__).parent;s=p.parent/'prize_wheel_gpt/prize_wheel_gpt.ino'
t=s.read_text();assert 'const uint16_t DIAG_CAPACITY = 8192;' in t
t=t.replace('const uint16_t DIAG_CAPACITY = 8192;','const uint16_t DIAG_CAPACITY = 16384;').replace('8192 PSRAM samples','16384 PSRAM samples')
t=t.replace('planned pulse ramp; 1650mA persistent hold','planned pulse ramp; 1650mA persistent hold; 16K trace')
t=t.replace('cuts outside the planned ramp. They amplified a tracking loss.','cuts outside the planned ramp, which could worsen tracking loss.')
t=t.replace('// Wheel slower than the field: reduce toward the wheel so the motor can\n    // never lead it.  (Also naturally sheds braking authority when the wheel\n    // is dying early: the field settles to the wheel\'s own speed.)','// Request a lower field speed when the encoder is slower. The ramp\n    // limiter below bounds the response; the fault monitors remain active.')
t=t.replace('// Pulse train has tapered to zero; the wheel settles under taper\n      // current.  Landing requires BOTH interior position AND stillness.','// Pulse train has tapered to zero; retain braking torque during settle.\n      // Landing requires BOTH interior position AND stillness.')
s.write_bytes(t.replace('\n','\r\n').encode())
for name in ('smooth_wheel_verified_boot.log','smooth_wheel_upload.error'):
 if (p/name).exists():shutil.copy2(p/name,p/('pre16k_'+name))
(p/'smooth_wheel_build.exit').write_text('RUNNING')
b=(p/'compile_smooth_suite.py').read_text().replace("[('smooth_audit',p/'smooth_ramp_audit'),('smooth_wheel',p.parent/'prize_wheel_gpt')]","[('smooth_wheel',p.parent/'prize_wheel_gpt')]")
(p/'compile_smooth_wheel.py').write_text(b)