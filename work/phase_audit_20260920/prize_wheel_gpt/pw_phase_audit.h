#pragma once

#if PW_SELFSPIN_MOTION_ENABLE
#error "Phase audit is read-only and must compile with PW_SELFSPIN_MOTION_ENABLE=0"
#endif

namespace PwPhaseAudit {
struct I2cRead {
  uint32_t startedUs = 0, doneUs = 0;
  uint16_t value = 0;
  uint8_t reg = 0, length = 0, tx = 255, requested = 0, available = 0;
  bool ok = false;
};

static I2cRead readRegister(uint8_t reg, uint8_t length) {
  I2cRead out;
  out.reg = reg; out.length = length; out.startedUs = micros();
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(reg); // address pointer only; no sensor configuration/OTP write
  out.tx = Wire.endTransmission(false);
  if (out.tx == 0) {
    size_t got = Wire.requestFrom((uint8_t)AS5600_ADDR, (size_t)length);
    int available = Wire.available();
    out.requested = got > 255 ? 255 : (uint8_t)got;
    out.available = available > 255 ? 255 : (available < 0 ? 0 : (uint8_t)available);
    if (got == length && available >= length) {
      for (uint8_t i = 0; i < length; ++i) out.value = (out.value << 8) | (uint8_t)Wire.read();
      out.ok = true;
    }
    while (Wire.available()) (void)Wire.read();
  }
  out.doneUs = micros();
  return out;
}

static void printRead(const char* name, const I2cRead& r) {
  Serial.printf("# AUDIT_AS name=%s reg=%02X bytes=%u start_us=%lu done_us=%lu "
                "ok=%d value=%d tx=%u requested=%u available=%u\n",
      name, r.reg, r.length, (unsigned long)r.startedUs, (unsigned long)r.doneUs,
      r.ok, r.ok ? (int)r.value : -1, r.tx, r.requested, r.available);
}

static int16_t signed9(uint32_t x) {
  const int16_t value = (int16_t)(x & 0x1FF);
  return (value & 0x100) ? value - 512 : value;
}
} // namespace PwPhaseAudit

void pwPhaseAuditSnapshot() {
  using namespace PwPhaseAudit;
  if (PW_SELFSPIN_MOTION_ENABLE || PwSelfspin::active || PwSelfspin::recoveryPending ||
      !PwSelfspin::inhibit || !stepper || stepper->isRunning() ||
      digitalRead(PIN_EN) != HIGH || currentStage != CS_FREEWHEEL || g_currentMa != 0 ||
      !encoderMotionReady() || !isfinite(omega) || fabsf(omega) > STILL_REV_S ||
      diagnosticDumpActive || (state != ST_IDLE_STOPPED && state != ST_FAULT_LATCHED)) {
    Serial.println(F("# AUDIT refused: fresh stationary encoder, EN-high, queue-empty, current0 and inactive disabled controller required"));
    return;
  }
  const uint32_t beginUs = micros();
  const int enBefore = digitalRead(PIN_EN), dirBefore = digitalRead(PIN_DIR);
  const int32_t pcntBefore = stepper->getCurrentPosition();
  const int32_t encoderBefore = encoderCountsMT;
  const uint32_t encoderSampleUs = lastGoodUs;
  const uint8_t faultBefore = (uint8_t)faultCode;
  const uint32_t tmcStartUs = micros();
  const uint32_t ioin = driver.IOIN();
  const uint16_t mscnt = driver.MSCNT();
  const uint32_t mscuract = driver.MSCURACT();
  const uint32_t chop = driver.CHOPCONF();
  const uint32_t gconf = driver.GCONF();
  const uint32_t drv = driver.DRV_STATUS();
  const uint8_t gstat = driver.GSTAT(); // read only: never acknowledge or clear
  const uint32_t tmcDoneUs = micros();
  const I2cRead status = readRegister(0x0B, 1);
  const I2cRead raw = readRegister(0x0C, 2);
  const I2cRead agc = readRegister(0x1A, 1);
  const I2cRead magnitude = readRegister(0x1B, 2);
  const uint32_t doneUs = micros();
  const int enAfter = digitalRead(PIN_EN), dirAfter = digitalRead(PIN_DIR);
  const int32_t pcntAfter = stepper->getCurrentPosition();
  const bool stillDisabled = enAfter == HIGH && !stepper->isRunning() &&
      pcntBefore == pcntAfter && dirBefore == dirAfter &&
      faultBefore == (uint8_t)faultCode && currentStage == CS_FREEWHEEL && g_currentMa == 0;
  // Read all registers before printing, so serial backpressure cannot separate
  // their acquisition. These sequential snapshots are not simultaneous samples.
  Serial.printf("# AUDIT_BEGIN start_us=%lu done_us=%lu motion_compiled=0 read_only=1 "
                "EN_before=%d EN_after=%d DIR_before=%d DIR_after=%d PCNT_before=%ld PCNT_after=%ld "
                "encoder_counts=%ld encoder_sample_us=%lu current_request_ma=%u fault=%u "
                "saved_fault_raw=%u recovery_guard=%u disabled_unchanged=%d\n",
      (unsigned long)beginUs, (unsigned long)doneUs, enBefore, enAfter, dirBefore, dirAfter,
      (long)pcntBefore, (long)pcntAfter, (long)encoderBefore, (unsigned long)encoderSampleUs,
      g_currentMa, (unsigned)faultCode, persistedFaultRaw, recoveryGuardRaw, stillDisabled);
  Serial.printf("# AUDIT_TMC start_us=%lu done_us=%lu IOIN=%08lX MSCNT=%u MSCURACT=%08lX "
                "CUR_A=%d CUR_B=%d CHOPCONF=%08lX MRES=%u INTPOL=%u DEDGE=%u "
                "GCONF=%08lX DRV_STATUS=%08lX GSTAT=%02X\n",
      (unsigned long)tmcStartUs, (unsigned long)tmcDoneUs, (unsigned long)ioin,
      mscnt, (unsigned long)mscuract, signed9(mscuract >> 16), signed9(mscuract),
      (unsigned long)chop, (unsigned)((chop >> 24) & 15),
      (unsigned)((chop >> 28) & 1), (unsigned)((chop >> 29) & 1),
      (unsigned long)gconf, (unsigned long)drv, gstat);
  printRead("STATUS", status); printRead("RAW_ANGLE", raw);
  printRead("AGC", agc); printRead("MAGNITUDE", magnitude);
  Serial.printf("# AUDIT_END MD=%d ML=%d MH=%d basic_magnet_status_ok=%d "
                "all_i2c_reads_ok=%d phase_lock_unverified=1 read_only=1\n",
      status.ok ? (int)((status.value >> 5) & 1) : -1,
      status.ok ? (int)((status.value >> 4) & 1) : -1,
      status.ok ? (int)((status.value >> 3) & 1) : -1,
      status.ok && (status.value & 0x38) == 0x20,
      status.ok && raw.ok && agc.ok && magnitude.ok);
}
