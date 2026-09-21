from pathlib import Path
root=Path(__file__).parent.parent
p=root/'prize_wheel_gpt'/'prize_wheel_gpt.ino'
s=p.read_bytes().decode('utf-8').replace('\r\n','\n')
def edit(a,b):
 global s
 assert s.count(a)==1,repr(a[:100]);s=s.replace(a,b,1)
edit('const uint16_t HOLD1_MS               = 1500;\nconst uint16_t HOLD2_MS               = 1200;', '''const uint16_t HOLD_HEALTH_MS         = 1000;
const uint16_t HOLD_RELEASE_CONFIRM_MS = 40;
const uint16_t DIAG_HOLD_RECORD_MS    = 5000;
uint32_t holdReleaseSinceMs = 0;
uint32_t holdLastHealthMs = 0;
int8_t holdReleaseDir = 0;''')
edit('const uint16_t CUR_HOLD1_MA     = 550;  // fade...\nconst uint16_t CUR_HOLD2_MA     = 300;   // ...to freewheel','''const uint16_t CUR_HOLD1_MA     = 550;  // continuous hold until next spin
const uint16_t CUR_HOLD2_MA     = 550;  // legacy stage ID retained; no timed fade''')
edit('  if (diagnosticCapture && diagnosticSawMotion) diagnosticFrozen = true;','''  if (diagnosticCapture && diagnosticSawMotion &&
      result != RES_CONTROLLED_SAFE && result != RES_EDGE_SAFE &&
      result != RES_OFF_TARGET_SAFE) diagnosticFrozen = true;''')
edit('''  setCurrentStage(CS_HOLD1);
  state = ST_SOFT_HOLD;
  stateEnteredMs = millis();
}''','''  setCurrentStage(CS_HOLD1);
  state = ST_SOFT_HOLD;
  stateEnteredMs = millis();
  holdReleaseSinceMs = 0;
  holdReleaseDir = 0;
  holdLastHealthMs = stateEnteredMs;
  Serial.printf("# HOLD current=%u mA; retained until sustained spin motion\\n", g_currentMa);
}''')
a=s.index('    case ST_SOFT_HOLD: {\n      // Fade the hold torque');b=s.index('\n    case ST_DIR_PROBE:',a)
s=s[:a]+'''    case ST_SOFT_HOLD: {
      // Zero STEP pulses with continuous holding torque: no timed freewheel.
      if (!encoderPositionFresh()) {
        enterFault(FC_ENCODER_STALE, "encoder stale during persistent hold");
        break;
      }
      if (nowMs - holdLastHealthMs >= HOLD_HEALTH_MS) {
        holdLastHealthMs = nowMs;
        uint32_t drv = driver.DRV_STATUS();
        uint8_t gst = (uint8_t)driver.GSTAT();
        // Temperature warning/shutdown, shorts, driver/charge-pump error.
        // Open-load flags are unreliable at rest and are not used here.
        if (drv == 0xFFFFFFFFUL || (drv & 0x1E000000UL) || (gst & 0x06)) {
          enterFault(FC_TMC_UART, "driver health fault during persistent hold");
          break;
        }
      }
      // A single noisy sample must not unlock the wheel. This detects motion,
      // not human force: holding torque must first prevent imbalance drift.
      if (encoderMotionReady() && fabsf(omega) >= SPIN_DETECT_REV_S) {
        int8_t direction = omega >= 0.0f ? 1 : -1;
        if (holdReleaseSinceMs == 0 || direction != holdReleaseDir) {
          holdReleaseSinceMs = nowMs;
          holdReleaseDir = direction;
        } else if (nowMs - holdReleaseSinceMs >= HOLD_RELEASE_CONFIRM_MS) {
          driverFreewheel();
          Serial.printf("# HOLD released: sustained motion dir=%+d omega=%.3f\\n", direction, omega);
          candidateStartCounts = encoderCountsMT;
          spinArmMs = 0;
          settleStillSinceMs = 0;
          holdReleaseSinceMs = 0;
          state = ST_MOTION_CANDIDATE;
          stateEnteredMs = nowMs;
        }
      } else {
        holdReleaseSinceMs = 0;
        holdReleaseDir = 0;
      }
      break;
    }
''' + s[b:]
edit('''  if (count > (size_t)available) count = available;
  if (count > 64) count = 64;
  diagnosticDumpOffset += Serial.write((const uint8_t*)diagnosticDumpLine + diagnosticDumpOffset, count);''','''  // Enqueue complete rows so event messages cannot split a CSV record.
  if (count > (size_t)available) return;
  diagnosticDumpOffset += Serial.write((const uint8_t*)diagnosticDumpLine + diagnosticDumpOffset, count);''')
edit('// Start only after outputs are disabled; send at most 64 ready UART bytes/tick.','// Start only after safe settling; enqueue one complete line when UART has room.')
edit('''    diagnosticFrozen = true;
    if (currentStage == CS_FREEWHEEL && digitalRead(PIN_EN) == HIGH) {
      diagnosticCapture = false;
      dumpDiagnostics();
    }''','''    // Include five seconds of persistent hold before freezing this trace.
    bool held = state == ST_SOFT_HOLD && currentStage == CS_HOLD1 &&
                digitalRead(PIN_EN) == LOW;
    if (held && millis() - stateEnteredMs < DIAG_HOLD_RECORD_MS) return;
    diagnosticFrozen = true;
    if (held || (currentStage == CS_FREEWHEEL && digitalRead(PIN_EN) == HIGH)) {
      diagnosticCapture = false;
      dumpDiagnostics();
    }''')
edit('# DIAG armed: 8192 PSRAM samples, PCNT + driver status; dumps after outputs disabled.','# DIAG armed: 8192 PSRAM samples, PCNT + driver status; includes 5 seconds of hold.')
edit('      if (diagnosticCapture) Serial.println(F("# DIAG active: status suppressed"));','''      if (diagnosticCapture && state != ST_IDLE_STOPPED &&
          state != ST_SOFT_HOLD && state != ST_FAULT_LATCHED)
        Serial.println(F("# DIAG active: moving status suppressed"));''')
edit('                rawZero, (unsigned)currentStage, cmdRevS);','''                rawZero, (unsigned)currentStage, cmdRevS);
  Serial.printf("# motor fas=%.4f current=%u EN=%d hold_ms=%lu\\n", fasWheelRevS(),
                g_currentMa, digitalRead(PIN_EN),
                (unsigned long)(state == ST_SOFT_HOLD ? millis() - stateEnteredMs : 0));''')
edit('void setup() {\n  Serial.begin(115200);','void setup() {\n  Serial.setTxBufferSize(1024);\n  Serial.begin(115200);')
edit('# build: v2-rattle-diag-20260918; clock fix + extended diagnostics; control unchanged','# build: v2-persistent-hold-20260918; 550mA hold until next spin; STEP clock fixed')
p.write_bytes(s.replace('\n','\r\n').encode('utf-8'))
p=root/'prize_wheel_gpt'/'pw_party.h'
s=p.read_bytes().decode('utf-8');eol='\r\n' if '\r\n' in s else '\n'
a='  void begin(unsigned long baud) { ::Serial.begin(baud); }';assert s.count(a)==1
s=s.replace(a,'  size_t setTxBufferSize(size_t size) { return ::Serial.setTxBufferSize(size); }'+eol+a)
p.write_bytes(s.encode('utf-8'))
print('Persistent hold and diagnostic framing patch applied.')
