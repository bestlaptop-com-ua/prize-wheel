from pathlib import Path
root=Path(__file__).parent.parent
p=root/'prize_wheel_gpt'/'prize_wheel_gpt.ino'
s=p.read_bytes().decode('utf-8').replace('\r\n','\n')
def edit(a,b):
 global s
 assert s.count(a)==1,repr(a[:100])
 s=s.replace(a,b,1)
edit('#include "pw_step_clock.h"','#include "pw_step_clock.h"\n#include <esp_heap_caps.h>')
edit('struct DiagnosticSample {      // 28 bytes; 3072 samples = ~86 KB, ~3 s at 1 kHz','struct DiagnosticSample {      // PSRAM trace; controller timing is unchanged')
edit('  uint8_t curMa10;             // commanded motor current / 10 mA\n};','''  uint8_t curMa10;             // commanded motor current / 10 mA
  int32_t stepCount;          // FAS hardware PCNT position, not speed estimate
  uint32_t stepUs;
  uint32_t drvStatus;
  uint32_t tstep;
  uint16_t driverAgeUs;
  uint8_t gstat;
  uint8_t pins;               // bit 0 EN, bit 1 DIR
};''')
a=s.index('// 2944 x 28 bytes =');b=s.index('uint16_t diagnosticHead =',a)
s=s[:a]+'''// Eight seconds at 1 kHz, allocated only in external RAM. No control data
// uses this buffer; allocation failure disables diagnostics, not the wheel.
const uint16_t DIAG_CAPACITY = 8192;
DiagnosticSample* diagnosticBuffer = nullptr;
bool diagnosticDumpActive = false;
uint16_t diagnosticDumpFirst = 0, diagnosticDumpIndex = 0;
uint8_t diagnosticDumpPhase = 0;
char diagnosticDumpLine[320];
size_t diagnosticDumpLength = 0, diagnosticDumpOffset = 0;
uint32_t diagnosticDriverUs = 0, diagnosticDrvStatus = 0, diagnosticTstep = 0;
uint8_t diagnosticGstat = 0;
uint8_t diagnosticDriverStage = 255;
'''+s[b:]
edit('  if (!diagnosticCapture || diagnosticFrozen) return;','  if (!diagnosticCapture || diagnosticFrozen || !diagnosticBuffer) return;')
edit('  s.curMa10 = (uint8_t)(g_currentMa / 10 > 255 ? 255 : g_currentMa / 10);','''  s.curMa10 = (uint8_t)(g_currentMa / 10 > 255 ? 255 : g_currentMa / 10);
  s.stepUs = micros();
  s.stepCount = stepper ? stepper->getCurrentPosition() : 0;
  s.pins = (digitalRead(PIN_EN) ? 1 : 0) | (digitalRead(PIN_DIR) ? 2 : 0);
  if (diagnosticDriverStage != (uint8_t)currentStage ||
      (uint32_t)(s.stepUs - diagnosticDriverUs) >= 50000UL) {
    // Read-only SPI snapshots, at stage changes or 20 Hz. dt_good_us records
    // any timing cost on the next sample; no driver registers are written.
    diagnosticDrvStatus = driver.DRV_STATUS();
    diagnosticTstep = driver.TSTEP();
    diagnosticGstat = (uint8_t)driver.GSTAT();
    diagnosticDriverUs = micros();
    diagnosticDriverStage = (uint8_t)currentStage;
  }
  s.drvStatus = diagnosticDrvStatus;
  s.tstep = diagnosticTstep;
  s.gstat = diagnosticGstat;
  uint32_t driverAge = (uint32_t)(micros() - diagnosticDriverUs);
  s.driverAgeUs = driverAge > 65535U ? 65535U : (uint16_t)driverAge;''')
edit('void closeSpin(SpinResult result) {\n  if (!spinOpen) return;','''void closeSpin(SpinResult result) {
  if (!spinOpen) return;
  if (diagnosticCapture && diagnosticSawMotion) diagnosticFrozen = true;''')
a=s.index('void dumpDiagnostics() {');b=s.index('\nvoid startDiagnosticCapture()',a)
s=s[:a]+'''void dumpDiagnostics() {
  if (diagnosticCount == 0) {
    Serial.println(F("# DIAG: no samples captured."));
    return;
  }
  diagnosticDumpFirst = (diagnosticHead + DIAG_CAPACITY - diagnosticCount) % DIAG_CAPACITY;
  diagnosticDumpIndex = 0;
  diagnosticDumpPhase = 0;
  diagnosticDumpLength = diagnosticDumpOffset = 0;
  diagnosticDumpActive = true;
}

// Drain the trace without blocking the 1 kHz controller or extending hold time.
// Start only after outputs are disabled; send at most 64 ready UART bytes/tick.
void serviceDiagnosticDump() {
  if (!diagnosticDumpActive) return;
  if (diagnosticDumpOffset == diagnosticDumpLength) {
    if (diagnosticDumpPhase == 0) {
      snprintf(diagnosticDumpLine, sizeof(diagnosticDumpLine),
        "# DIAG columns: done_us,dt_good_us,raw,delta,counts,i2c_us,"
        "omega_mrev,window_mrev,cmd_mrev,fas_mrev,remain_ddeg,"
        "flags_hex,state,stage,current_ma,step_count,step_us,drv_status_hex,"
        "tstep,driver_age_us,gstat_hex,pins_hex\\n");
      diagnosticDumpPhase = 1;
    } else if (diagnosticDumpIndex < diagnosticCount) {
      const DiagnosticSample& s = diagnosticBuffer[(diagnosticDumpFirst + diagnosticDumpIndex) % DIAG_CAPACITY];
      snprintf(diagnosticDumpLine, sizeof(diagnosticDumpLine),
        "D,%lu,%u,%u,%d,%ld,%u,%d,%d,%d,%d,%d,%02X,%u,%u,%u,%ld,%lu,%08lX,%lu,%u,%02X,%02X\\n",
        (unsigned long)s.doneUs, s.dtGoodUs, s.raw, s.delta,
        (long)s.counts, s.i2cUs, s.omegaMilliRevS, s.windowMilliRevS,
        s.cmdMilliRevS, s.fasMilliRevS, s.remainDeciDeg,
        s.flags, s.state, s.stage, (unsigned)s.curMa10 * 10,
        (long)s.stepCount, (unsigned long)s.stepUs, (unsigned long)s.drvStatus,
        (unsigned long)s.tstep, s.driverAgeUs, s.gstat, s.pins);
      ++diagnosticDumpIndex;
    } else if (diagnosticDumpPhase == 1) {
      snprintf(diagnosticDumpLine, sizeof(diagnosticDumpLine),
        "# DIAG n=%u wrapped=%d frozen=%d\\n", diagnosticCount, diagnosticWrapped, diagnosticFrozen);
      diagnosticDumpPhase = 2;
    } else {
      diagnosticDumpActive = false;
      return;
    }
    diagnosticDumpLength = strlen(diagnosticDumpLine);
    diagnosticDumpOffset = 0;
  }
  int available = Serial.availableForWrite();
  if (available <= 0) return;
  size_t count = diagnosticDumpLength - diagnosticDumpOffset;
  if (count > (size_t)available) count = available;
  if (count > 64) count = 64;
  diagnosticDumpOffset += Serial.write((const uint8_t*)diagnosticDumpLine + diagnosticDumpOffset, count);
}
''' + s[b:]
edit('void startDiagnosticCapture() {\n  diagnosticHead = 0;','''void startDiagnosticCapture() {
  if (!diagnosticBuffer) { Serial.println(F("# DIAG unavailable: PSRAM allocation failed")); return; }
  if (diagnosticDumpActive) { Serial.println(F("# DIAG dump still in progress")); return; }
  diagnosticDriverStage = 255;
  diagnosticHead = 0;''')
edit('  Serial.println(F("# DIAG armed: RAM-only 1 kHz capture; dumps after true stop or fault."));','  Serial.println(F("# DIAG armed: 8192 PSRAM samples, PCNT + driver status; dumps after outputs disabled."));')
edit('''  } else if (nowUs - diagnosticStillSinceUs >= 700000UL) {
    diagnosticCapture = false;
    dumpDiagnostics();
  }
}''','''  } else if (nowUs - diagnosticStillSinceUs >= 700000UL) {
    diagnosticFrozen = true;
    if (currentStage == CS_FREEWHEEL && digitalRead(PIN_EN) == HIGH) {
      diagnosticCapture = false;
      dumpDiagnostics();
    }
  }
}''')
edit('# build: v2-step-clock-fix-20260918; baseline control + fault capture','# build: v2-rattle-diag-20260918; clock fix + extended diagnostics; control unchanged')
edit('  randomSeed(esp_random());','''  randomSeed(esp_random());
  diagnosticBuffer = (DiagnosticSample*)heap_caps_malloc(
      sizeof(DiagnosticSample) * DIAG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  Serial.printf("# DIAG PSRAM samples=%u bytes=%lu allocated=%d\\n", DIAG_CAPACITY,
                (unsigned long)(sizeof(DiagnosticSample) * DIAG_CAPACITY), diagnosticBuffer != nullptr);''')
edit('SPIN#%lu SETTLE-DRAG: guest moved the wheel during settle','SPIN#%lu SETTLE-TRAVEL: excess motion during settle; source unknown')
edit('  serviceDiagnosticCapture();\n\n  // Party additions','  serviceDiagnosticCapture();\n  serviceDiagnosticDump();\n\n  // Party additions')
p.write_bytes(s.replace('\n','\r\n').encode('utf-8'))
print('Extended diagnostic patch applied; all motor/control constants retained.')
