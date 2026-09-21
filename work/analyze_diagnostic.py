from pathlib import Path
import csv,json,statistics,collections
root=Path(__file__).parent
text=(root/"baseline_serial.log").read_text(encoding="utf-8")
blocks=text.split("# DIAG columns:")
if len(blocks)<2:raise SystemExit("No diagnostic dump yet")
block=blocks[-1]
rows=[]
for line in block.splitlines():
 if line.startswith("D,"):
  v=line.split(",")
  if len(v)!=16:continue
  r=dict(zip(("us","dt_us","raw","delta","counts","i2c_us","omega","window","cmd","fas","remaining","flags","state","stage","current_ma"),
             [int(x,16) if i==11 else int(x) for i,x in enumerate(v[1:])]))
  rows.append(r)
if not rows:raise SystemExit("Empty diagnostic dump")
start=next((r["us"] for r in rows if r["state"]==6),rows[0]["us"])
for r in rows:r["t_ms"]=((r["us"]-start+2**31)%2**32-2**31)/1000
out=root/"latest_diagnostic.csv"
with out.open("w",newline="") as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
print("samples",len(rows),"range_ms",[rows[0]["t_ms"],rows[-1]["t_ms"]])
print("dt_us median/max",statistics.median(r["dt_us"] for r in rows),max(r["dt_us"] for r in rows))
print("i2c_us median/max",statistics.median(r["i2c_us"] for r in rows),max(r["i2c_us"] for r in rows))
print("flags",dict(collections.Counter(hex(r["flags"]) for r in rows)))
print("stage transitions")
prev=None
for r in rows:
 key=(r["state"],r["stage"])
 if key!=prev:
  print(json.dumps(r));prev=key
print("20ms intervals around capture: t_ms, delta-derived rev/s, omega, command, fas, stage, current")
sel=[r for r in rows if r["t_ms"]>=-100]
last=sel[0]
for r in sel[1:]:
 dt=r["t_ms"]-last["t_ms"]
 if dt>=20:
  print(round(r["t_ms"],1),round((r["counts"]-last["counts"])/4096/(dt/1000),4),r["omega"]/1000,r["cmd"]/1000,r["fas"]/1000,r["stage"],r["current_ma"])
  last=r
print("largest raw increments")
for r in sorted(rows,key=lambda r:abs(r["delta"]),reverse=True)[:8]:print(json.dumps(r))
print("saved",out)
