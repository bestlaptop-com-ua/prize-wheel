from pathlib import Path
import difflib
p=Path(__file__).parent
old=(p/'before-gentle6/prize_wheel_gpt.ino').read_text();new=(p.parent/'prize_wheel_gpt/prize_wheel_gpt.ino').read_text()
print(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='deployed brake2200',tofile='gentle6 candidate')))