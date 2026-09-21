from pathlib import Path
root=Path(__file__).parent.parent
p=root/'prize_wheel_gpt'/'prize_wheel_gpt.ino'
s=p.read_bytes().decode('utf-8')
for a,b in [
 ('CUR_HOLD1_MA     = 550;  // continuous hold until next spin','CUR_HOLD1_MA     = 1650; // NEMA23: continuous hold at existing brake current'),
 ('CUR_HOLD2_MA     = 550;  // legacy stage ID retained; no timed fade','CUR_HOLD2_MA     = 1650; // legacy stage ID retained; no timed fade'),
 ('v2-persistent-hold-20260918; 550mA hold until next spin','v2-hold1650-20260918; 1650mA hold until next spin')]:
 assert s.count(a)==1,repr(a);s=s.replace(a,b)
p.write_bytes(s.encode('utf-8'))
for original,new in [('build_hold.py','build_hold1650.py'),('upload_hold.py','upload_hold1650.py')]:
 s=(root/'work'/original).read_text(encoding='utf-8-sig')
 s=s.replace('wheel-hold-firmware','wheel-hold1650-firmware').replace('wheel_hold_','wheel_hold1650_')
 (root/'work'/new).write_text(s,encoding='utf-8')
print('Changed hold current only: 550 -> 1650 mA RMS; braking/current ladder otherwise unchanged.')
