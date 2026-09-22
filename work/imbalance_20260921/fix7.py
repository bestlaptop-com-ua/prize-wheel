p=open('apply_imbalance.py').read()
j=p.index("def main():")
new=r'''edit('capture/brake current 2800',
     'const uint16_t CUR_CAPTURE_MA   = 2200;\n'
     'const uint16_t CUR_BRAKE_MA     = 2200; // retain capture torque through braking/settling\n',
     'const uint16_t CUR_CAPTURE_MA   = 2800; // 2026-09-21: owner asked for more torque margin (motor 3 A rated)\n'
     'const uint16_t CUR_BRAKE_MA     = 2800; // retain capture torque through braking/settling\n')


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
