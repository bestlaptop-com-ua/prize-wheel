import re
p=open('apply_imbalance.py').read()
i=p.index("edit('D command'"); j=p.index("def main():")
new='''edit('D command',
     "    case 'F':\\n"
     '      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {\\n',
     "    case 'D':\\n"
     '      printDriver();\\n'
     '      break;\\n'
     "    case 'F':\\n"
     '      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {\\n')

edit('D help',
     '    " F  reset friction model to seeds\\\\n"\\n',
     '    " F  reset friction model to seeds\\\\n"\\n'
     '    " D  print driver registers (read-only)\\\\n"\\n')


'''
p=p[:i]+new+p[j:]
open('apply_imbalance.py','w').write(p)
'''
'''
