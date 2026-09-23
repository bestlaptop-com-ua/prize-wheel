from pathlib import Path
ROOT = Path(__file__).resolve().parent / 'prize_wheel_gpt'
def patch(fname, edits, marker):
    p = ROOT / fname; s = p.read_text(encoding='utf-8')
    if marker in s: print(fname, 'already patched'); return
    for old, new in edits:
        assert s.count(old) == 1, f'{fname}: anchor not unique/found: {old[:70]!r}'
    for old, new in edits: s = s.replace(old, new)
    p.write_text(s, encoding='utf-8'); print(fname, 'patched', len(edits), 'edits')

# gain: volume 30 -> 1.0 (no clipping possible at <=1.0); default volume 30
patch('pw_audio_i2s.h', [
    ('#define PW_I2S_GAIN_MAX 0.75f      /* volume 30 -> 0.75; default 20 -> 0.50 */',
     '#define PW_I2S_GAIN_MAX 1.0f       /* volume 30 -> 1.0 (full scale); V<n> live */'),
], 'PW_I2S_GAIN_MAX 1.0f')
patch('pw_party.h', [
    ('#define PW_DFP_VOLUME 20       /* 0..30; live-adjust with V<n> + Enter        */',
     '#define PW_DFP_VOLUME 30       /* 0..30; live-adjust with V<n> + Enter; 30 = gain 1.0 */'),
], 'PW_DFP_VOLUME 30')

patch('prize_wheel_gpt.ino', [
    # SPI health: retry transient bad reads (marginal ribbon during spin)
    ('bool captureDriverHealthy() {\n  uint32_t drv = driver.DRV_STATUS();\n  uint8_t gst = (uint8_t)driver.GSTAT();\n',
     'static uint32_t drvHealthRetries = 0;\nbool captureDriverHealthy() {\n'
     '  uint32_t drv = driver.DRV_STATUS();\n  uint8_t gst = (uint8_t)driver.GSTAT();\n'
     '  // 2026-09-22: a marginal SPI link glitches under vibration; one bad\n'
     '  // transaction (all-ones) must not abandon the capture. Re-read up to 2x.\n'
     '  for (int i = 0; i < 2 && (drv == 0xFFFFFFFFUL || drv == 0 || gst == 0xFF); ++i) {\n'
     '    ++drvHealthRetries; delayMicroseconds(200);\n'
     '    drv = driver.DRV_STATUS(); gst = (uint8_t)driver.GSTAT();\n'
     '  }\n'),
    # abandon diagnostics
    ('  if (!fasSign || !stepper || stepper->isRunning() ||\n'
     '      digitalRead(PIN_EN) != HIGH || !encoderMotionReady() ||\n'
     '      !isfinite(forward) || forward < 0.02f || forward > CAPTURE_MAX_WHEEL_REV_S ||\n'
     '      !captureDriverHealthy()) {\n'
     '    abandonCapture("capture arming prerequisites"); return false;\n',
     '  const bool preRun = stepper && stepper->isRunning();\n'
     '  const bool preEn = digitalRead(PIN_EN) == HIGH;\n'
     '  const bool preEnc = encoderMotionReady();\n'
     '  const bool preSpd = isfinite(forward) && forward >= 0.02f && forward <= CAPTURE_MAX_WHEEL_REV_S;\n'
     '  const bool preDrv = fasSign && stepper && !preRun && preEn && preEnc && preSpd && captureDriverHealthy();\n'
     '  if (!fasSign || !stepper || preRun || !preEn || !preEnc || !preSpd || !preDrv) {\n'
     '    Serial.printf("# prereq fail: sign=%d run=%d en=%d enc=%d spd=%d(%.3f) drv=%d drvStatus=%08lX gstat=%02X ver=%02X retries=%lu\\n",\n'
     '                  fasSign, (int)preRun, (int)preEn, (int)preEnc, (int)preSpd, forward, (int)preDrv,\n'
     '                  (unsigned long)driver.DRV_STATUS(), (unsigned)(uint8_t)driver.GSTAT(),\n'
     '                  (unsigned)driver.version(), (unsigned long)drvHealthRetries);\n'
     '    abandonCapture("capture arming prerequisites"); return false;\n'),
], 'prereq fail')
print('done')
