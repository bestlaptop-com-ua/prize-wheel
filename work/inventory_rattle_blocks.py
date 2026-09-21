from pathlib import Path
s=(Path(__file__).parent/'rattle_serial.log').read_text()
for i,b in enumerate(s.split('# DIAG columns:')[1:],1):
 lines=[l for l in b.splitlines() if l.startswith('D,')]
 print('block',i,'rows',len(lines),'first',lines[0] if lines else '', 'last',lines[-1] if lines else '')
