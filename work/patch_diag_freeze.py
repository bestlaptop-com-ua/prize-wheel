from pathlib import Path
p=Path(r"C:\Users\Mill\Desktop\prize-wheel\prize_wheel_gpt\prize_wheel_gpt.ino")
s=(p.parent.parent/"work"/"baseline-2dbc15e"/"prize_wheel_gpt.ino").read_bytes().decode("utf-8")
edits=[
("bool diagnosticCapture = false;", "bool diagnosticCapture = false;\n// Retain the lead-up to a fault instead of overwriting it during free coast.\nbool diagnosticFrozen = false;"),
("  if (!diagnosticCapture) return;\n  const EncoderRead& read = encoderRead;", "  if (!diagnosticCapture || diagnosticFrozen) return;\n  const EncoderRead& read = encoderRead;"),
("  faultCode = code;\n  PW_S1_PERSIST(code);", "  faultCode = code;\n  if (diagnosticCapture) diagnosticFrozen = true;  // before NVS/serial latency\n  PW_S1_PERSIST(code);"),
('  Serial.printf("# DIAG n=%u wrapped=%d\\n", diagnosticCount, diagnosticWrapped);', '  Serial.printf("# DIAG n=%u wrapped=%d frozen=%d\\n", diagnosticCount, diagnosticWrapped, diagnosticFrozen);'),
("  diagnosticWrapped = false;\n  diagnosticCapture = true;", "  diagnosticWrapped = false;\n  diagnosticFrozen = false;\n  diagnosticCapture = true;"),
('    "\\n=== PRIZE WHEEL (correctness redesign) ===\\n"', '    "\\n=== PRIZE WHEEL (correctness redesign) ===\\n"\n    "# build: v2-diag-freeze-20260918; control parameters unchanged\\n"'),
]
for old,new in edits:
 old=old.replace('\n','\r\n');new=new.replace('\n','\r\n')
 assert s.count(old)==1,repr(old)
 s=s.replace(old,new,1)
p.write_bytes(s.encode("utf-8"))
print("Applied diagnostic-only edits:",len(edits))

