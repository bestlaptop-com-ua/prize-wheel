p=open('apply_imbalance.py').read()
j=p.index("def main():")
new='''edit('clear GSTAT after config',
     '  driver.iholddelay(0);\\n'
     '  driver.TPOWERDOWN(255);\\n'
     '}\\n',
     '  driver.iholddelay(0);\\n'
     '  driver.TPOWERDOWN(255);\\n'
     '  // TMC5160 GSTAT is write-to-clear: the power-up reset flag (and any uv_cp)\\n'
     '  // stays latched forever otherwise, and captureDriverHealthy() requires 0.\\n'
     '  driver.GSTAT(0x07);\\n'
     '}\\n')


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
