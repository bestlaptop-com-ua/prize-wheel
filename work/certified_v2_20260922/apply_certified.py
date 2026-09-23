"""Certified party firmware (2dbc15e = party-certified state machine on the S3/TMC5160 board)
+ the minimal hardware/config deltas for the 2026-09-22 wheel. Every anchor must match exactly once."""
import hashlib, json, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent
INO = ROOT / 'prize_wheel_gpt' / 'prize_wheel_gpt.ino'
E = []
def edit(n, a, b): E.append((n, a, b))

edit('build string anchor', '#include "pw_party.h"\n', '#include "pw_party.h"\n#include "pw_step_clock.h"\n')
edit('step clock fix',
     '    stepper->setAutoEnable(false);\n  } else {\n    Serial.println(F("# FATAL: stepperConnectToPin failed; control locked"));\n',
     '    stepper->setAutoEnable(false);\n    if (!pwFixStepperClock()) Serial.println(F("# WARN: STEP clock layout unexpected; correction not applied"));\n  } else {\n    Serial.println(F("# FATAL: stepperConnectToPin failed; control locked"));\n')
edit('18 wedges', '#define NUM_WEDGES 12\n', '#define NUM_WEDGES 18   // 2026-09-22 wheel: labels 1-18, index = label-1, index 0 at the 18|1 line\n')
edit('dare mask', 'uint16_t dare_mask = (1 << 1) | (1 << 5);  // wedges 1 and 5 are never targets\n',
     'uint32_t dare_mask = (1UL << 2) | (1UL << 7) | (1UL << 12) | (1UL << 15);  // labels 3, 8, 13, 16 (8 is the hard one)\n')
edit('dare print', 'Serial.printf("# dare_mask=0x%03X; dare wedges: 1 5\\n", dare_mask);\n',
     'Serial.printf("# dare_mask=0x%05lX; dare indices 2 7 12 15 = labels 3 8 13 16\\n", (unsigned long)dare_mask);\n')
edit('z help', '" z  set current raw as wedge-0 anchor (wheel at rest, pointer on 11|0 line)\\n"\n',
     '" z  set current raw as wedge-0 anchor (wheel at rest, pointer on 18|1 line)\\n"\n" D  print driver registers\\n"\n')
edit('probe margin', '  return edgeMargin >= 11.0f;\n', '  return edgeMargin >= 0.3f * WEDGE_DEG;\n')
edit('gstat clear', '  driver.iholddelay(0);\n  driver.TPOWERDOWN(255);\n}\n',
     '  driver.iholddelay(0);\n  driver.TPOWERDOWN(255);\n  driver.GSTAT(0x07);   // TMC5160 GSTAT is write-to-clear; the power-up reset flag otherwise stays forever\n}\n')
edit('hold currents', 'const uint16_t CUR_HOLD1_MA     = 550;  // fade...\n', 'const uint16_t CUR_HOLD1_MA     = 800;  // fade...\n')
edit('takeover persist', "    case 'e':\n      takeoverEnabled = !takeoverEnabled;\n      Serial.printf(\"# takeoverEnabled=%d\\n\", takeoverEnabled ? 1 : 0);\n",
     "    case 'e':\n      takeoverEnabled = !takeoverEnabled;\n      preferences.putBool(\"takeover\", takeoverEnabled);\n      Serial.printf(\"# takeoverEnabled=%d (persisted)\\n\", takeoverEnabled ? 1 : 0);\n")
edit('takeover restore', '  rawZero = preferences.getUShort("rawZero", rawZero);  // label-true anchor\n',
     '  rawZero = preferences.getUShort("rawZero", rawZero);  // label-true anchor\n  takeoverEnabled = preferences.getBool("takeover", true);\n')
edit('driver readout', 'void printStatus() {\n',
     'void printDriver() {\n  uint32_t drv = driver.DRV_STATUS();\n  Serial.printf("# drv DRV_STATUS=%08lX GSTAT=%02X IOIN=%08lX version=%02X usteps=%u\\n",\n      (unsigned long)drv, (unsigned)(uint8_t)driver.GSTAT(), (unsigned long)driver.IOIN(),\n      (unsigned)driver.version(), (unsigned)driver.microsteps());\n}\n\nvoid printStatus() {\n')
edit('D command', "    case 'F':\n", "    case 'D':\n      printDriver();\n      break;\n    case 'F':\n")

def main():
    raw = INO.read_bytes(); crlf = b'\r\n' in raw
    t = raw.decode('utf-8').replace('\r\n', '\n')
    bad = ['%s: %d' % (n, t.count(a)) for n, a, b in E if t.count(a) != 1]
    if bad: print('\n'.join(bad)); return 1
    for n, a, b in E: t = t.replace(a, b)
    t = t.replace('// build:', '// build: CERTIFIED-V2-PARTY-20260922 //')
    INO.write_bytes((t.replace('\n', '\r\n') if crlf else t).encode('utf-8'))
    print('applied', len(E), 'edits')
    return 0
if __name__ == '__main__': sys.exit(main())
