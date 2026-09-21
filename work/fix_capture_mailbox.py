from pathlib import Path
root=Path(__file__).parent
p=root/'hold1650_capture.py'
s=p.read_text(encoding='utf-8')
a="   lines=mail.read_text(encoding='utf-8-sig').splitlines() if mail.exists() else []"
b="""   try:
    lines=mail.read_text(encoding='utf-8-sig').splitlines() if mail.exists() else []
   except (PermissionError,FileNotFoundError):
    # Windows mailbox append/read may briefly conflict; keep capturing serial.
    lines=[]"""
assert s.count(a)==1;s=s.replace(a,b)
p.write_text(s,encoding='utf-8')
(root/'hold1650_commands.txt').write_text('?\ns\n',encoding='utf-8')
print('Recorder now retries transient mailbox access conflicts without dropping serial.')
