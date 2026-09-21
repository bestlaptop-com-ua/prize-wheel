from pathlib import Path
import re,collections,json,csv
p=Path(__file__).parent
text=(p/'hold1650_serial.log').read_text(encoding='utf-8',errors='replace')
# Host annotations can occur between serial chunks; remove only their exact wrapper.
text=re.sub(r'\n\[HOST [^\n]*\]\s*[^\n]*\n','',text)
for i,block in enumerate(text.split('# DIAG columns:')[1:],1):
 header=block.splitlines()[0].strip().split(','); rows=[]; bad=0
 for line in block.splitlines()[1:]:
  if not line.startswith('D,'): continue
  vals=line.strip().split(',')[1:]
  try:
   if len(vals)!=len(header): raise ValueError()
   rows.append({k:int(v,16 if k.endswith('_hex') else 10) for k,v in zip(header,vals)})
  except ValueError: bad+=1
 end=re.search(r'# DIAG n=(\d+)',block)
 print(json.dumps(dict(block=i,rows=len(rows),expected=int(end.group(1)) if end else None,bad=bad,states=dict(collections.Counter(r['state'] for r in rows)),stages=dict(collections.Counter(r['stage'] for r in rows)))))
 if end and len(rows)==int(end.group(1)):
  out=p/f'five_spin_diag_block{i}.csv'
  with out.open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=header);w.writeheader();w.writerows(rows)
  print('saved',str(out))