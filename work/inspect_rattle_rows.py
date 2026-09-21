from pathlib import Path
s=(Path(__file__).parent/'rattle_serial.log').read_text(encoding='utf-8')
b=s.split('# DIAG columns:')[-1]
for i,line in enumerate(b.splitlines()):
 if i==0 or (line.startswith('D,') and len(line.split(','))!=23) or (not line.startswith('D,') and line.strip()):print(i,repr(line))
