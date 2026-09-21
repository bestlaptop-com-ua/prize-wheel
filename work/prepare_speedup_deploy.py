from pathlib import Path
p=Path(__file__).parent
s=(p/'upload_verify_smooth16k.py').read_text().replace('smooth_wheel','speedup_wheel').replace('smooth_audit_verified.exit','speedup_replay_verified.exit').replace('v2-smooth-ramp-20260918','v2-speedup-filter-20260918')
(p/'upload_verify_speedup.py').write_text(s)
s=(p/'smooth_capture.py').read_text().replace('smooth','speedup').replace("b'# RAMP_STOP'","b'# RAMP_STOP',b'# SPEEDUP'")
(p/'speedup_capture.py').write_text(s)
(p/'speedup_commands.txt').write_text('?\ns\nd\n')
print('Prepared gated deployment and recorder')