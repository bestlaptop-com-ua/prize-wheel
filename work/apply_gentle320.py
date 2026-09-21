from pathlib import Path
import shutil
p=Path(__file__).parent;src=p.parent/'prize_wheel_gpt/prize_wheel_gpt.ino'
bak=p/'before-gentle320';bak.mkdir(exist_ok=False);shutil.copy2(src,bak/src.name)
s=src.read_text()
changes={
'const uint32_t DECEL_CEILING_SPS2     = 650;    // natural-motion decel ceiling':'const uint32_t DECEL_CEILING_SPS2     = 320;    // direct drive: 0.10 rev/s^2 maximum',
'const uint32_t ASSIST_DECEL_MAX_SPS2  = 1100;   // weak-spin nearest-target cap':'const uint32_t ASSIST_DECEL_MAX_SPS2  = 320;    // fallbacks must obey the same gentle limit',
'v2-brake2200-20260918; smooth ramp; brake2200; hold1650; tracking-loss latch':'v2-gentle320-20260918; decel320; brake2200; hold1650; tracking-loss latch'
}
for a,b in changes.items():
 assert s.count(a)==1,a
 s=s.replace(a,b)
src.write_bytes(s.replace('\n','\r\n').encode())
for old,new in [('compile_brake2200.py','compile_gentle320.py'),('upload_verify_brake2200.py','upload_verify_gentle320.py'),('brake2200_capture.py','gentle320_capture.py')]:
 s=(p/old).read_text().replace('brake2200','gentle320')
 (p/new).write_bytes(s.replace('\n','\r\n').encode())
(p/'gentle320_commands.txt').write_text('?\ns\nd\n')
print('Patched two deceleration caps only plus build identity; prepared compile/upload/recorder scripts. No flash.')