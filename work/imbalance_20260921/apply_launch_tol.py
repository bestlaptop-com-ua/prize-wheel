from pathlib import Path
ROOT = Path(__file__).resolve().parent / 'prize_wheel_gpt'
def patch(fname, edits, marker):
    p = ROOT / fname; s = p.read_text(encoding='utf-8')
    if marker in s: print(fname, 'already patched'); return
    for old, new in edits:
        assert s.count(old) == 1, f'{fname}: anchor not unique/found: {old[:70]!r}'
    for old, new in edits: s = s.replace(old, new)
    p.write_text(s, encoding='utf-8'); print(fname, 'patched', len(edits), 'edits')

patch('prize_wheel_gpt.ino', [
    # launch-side speed check: reserve already qualified the speed; encoder jitter
    # of a few mrev/s between the reserve tick and the launch tick must not abandon.
    ('  const bool preSpd = isfinite(forward) && forward >= 0.02f && forward <= CAPTURE_MAX_WHEEL_REV_S;\n',
     '  const bool preSpd = isfinite(forward) && forward >= 0.02f && forward <= CAPTURE_MAX_WHEEL_REV_S + 0.05f;  // +0.05 launch tolerance (encoder jitter); cmd is clamped at CAPTURE_MAX_CMD anyway\n'),
], 'launch tolerance')

patch('pw_party_impl.h', [
    ("    case 'a':\n      pwAudioEnabled = !pwAudioEnabled;\n",
     "    case 'P':\n"
     "      pwDfpFlush();\n"
     "      pwDfpSendNow(PW_DFP_CMD_PLAY_MP3, PW_TRK_FANFARE);   /* speaker test, bypasses gate */\n"
     "      Serial.printf(\"# audio test: fanfare (audio %s, i2s task %s, gain %.2f)\\n\",\n"
     "                    pwAudioEnabled ? \"ON\" : \"OFF\", pwI2sRunning ? \"running\" : \"NOT RUNNING\", (double)pwI2sGain);\n"
     "      return true;\n"
     "    case 'a':\n      pwAudioEnabled = !pwAudioEnabled;\n"),
    ('" a  audio on/off   l  LEDs on/off   w  network status\\n"',
     '" a  audio on/off   P  play fanfare (speaker test)   l  LEDs on/off   w  network status\\n"'),
], "case 'P':")
print('done')
