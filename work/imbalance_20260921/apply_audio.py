"""Party audio + watchdog patch over the current candidate (in place, guarded)."""
from pathlib import Path
ROOT = Path(__file__).resolve().parent / 'prize_wheel_gpt'
def patch(fname, edits, marker):
    p = ROOT / fname; s = p.read_text(encoding='utf-8')
    if marker in s: print(fname, 'already patched'); return
    for old, new in edits:
        assert s.count(old) == 1, f'{fname}: anchor not unique/found: {old[:60]!r}'
    for old, new in edits: s = s.replace(old, new)
    p.write_text(s, encoding='utf-8'); print(fname, 'patched', len(edits), 'edits')

patch('pw_party.h', [
    ('#define PW_FX_AUDIO_ENABLE 0 /* v2: DFPlayer removed; pending I2S/PCM5102A port  */',
     '#define PW_FX_AUDIO_ENABLE 1 /* v2: I2S -> PCM5102A (MSB/LJ, 32-bit slots) -> TPA3116 */'),
], 'PW_FX_AUDIO_ENABLE 1')

patch('pw_party_impl.h', [
    ('/* ------------------------------ DFPlayer ---------------------------------- */\n',
     '/* ------------------------------ DFPlayer ---------------------------------- */\n'
     '/* Transport replaced by the I2S sample player (pw_audio_i2s.h); the command  */\n'
     '/* queue and cue logic below are unchanged.                                   */\n'
     '#include "pw_audio_i2s.h"\n'),
    ('  uint8_t f[10];\n  f[0] = 0x7E; f[1] = 0xFF; f[2] = 0x06; f[3] = cmd; f[4] = 0x00;\n'
     '  f[5] = (uint8_t)(arg >> 8); f[6] = (uint8_t)(arg & 0xFF);\n'
     '  uint16_t ck = (uint16_t)(0 - (0xFF + 0x06 + cmd + 0x00 + f[5] + f[6]));\n'
     '  f[7] = (uint8_t)(ck >> 8); f[8] = (uint8_t)(ck & 0xFF);\n'
     '  f[9] = 0xEF;\n  Serial1.write(f, 10);\n',
     '  switch (cmd) {\n'
     '    case PW_DFP_CMD_PLAY_MP3: pwI2sPlay(arg); break;\n'
     '    case PW_DFP_CMD_LOOP_CUR: pwI2sSetLoop(arg == 0); break;\n'
     '    case PW_DFP_CMD_STOP:     pwI2sStop(); break;\n'
     '    case PW_DFP_CMD_VOLUME:   pwI2sSetVolume(arg); break;\n'
     '    default: break;\n  }\n'),
    ('  Serial1.begin(PW_DFP_BAUD, SERIAL_8N1, PW_DFP_RX_PIN, PW_DFP_TX_PIN);\n'
     '  pwDfpReadyAtMs = millis() + PW_DFP_BOOT_DELAY_MS;\n'
     '  pwDfpQueue(PW_DFP_CMD_VOLUME, PW_DFP_VOLUME);\n'
     '  Serial.printf("# fx: DFPlayer on UART1 tx=%d rx=%d vol=%d (tracks /mp3/0001..0006)\\n",\n'
     '                PW_DFP_TX_PIN, PW_DFP_RX_PIN, (int)PW_DFP_VOLUME);\n',
     '  if (pwI2sBegin()) {\n'
     '    pwDfpReadyAtMs = millis();\n'
     '    pwDfpQueue(PW_DFP_CMD_VOLUME, PW_DFP_VOLUME);\n'
     '    Serial.printf("# fx: I2S audio bck=15 lrck=16 dout=17, %d Hz, MSB/32-bit, vol=%d, %d samples in flash\\n",\n'
     '                  (int)PW_SAMPLE_RATE, (int)PW_DFP_VOLUME, (int)PW_NUM_SAMPLES);\n'
     '  } else {\n'
     '    pwAudioEnabled = false;\n'
     '    Serial.println(F("# fx: I2S init FAILED; audio disabled (wheel unaffected)"));\n'
     '  }\n'),
    ('" V<n>+Enter  DFPlayer volume 0-30 (e.g. V18)"', '" V<n>+Enter  audio volume 0-30 (e.g. V18)"'),
], 'pw_audio_i2s.h')

patch('prize_wheel_gpt.ino', [
    ('const float SPEEDUP_NOISE_REV_S       = 0.050f;', 'const float SPEEDUP_NOISE_REV_S       = 0.150f;  // was 0.050: rotor hunting tripped it'),
    ('const uint16_t SPEEDUP_FAULT_MS       = 400;', 'const uint16_t SPEEDUP_FAULT_MS       = 800;    // was 400'),
    ('        if (spinOpen && !encoderPositionFresh() &&\n'
     '            nowMs - stateEnteredMs > 5000) {\n'
     '          closeSpin(spinOpenedDuringFault ? RES_CONTROL_LOCKED : RES_FAULTED);\n'
     '        }\n'
     '      }\n'
     '      break;\n',
     '        if (spinOpen && !encoderPositionFresh() &&\n'
     '            nowMs - stateEnteredMs > 5000) {\n'
     '          closeSpin(spinOpenedDuringFault ? RES_CONTROL_LOCKED : RES_FAULTED);\n'
     '        }\n'
     '      }\n'
     '      // Auto-clear: SUSTAINED_SPEEDUP is a control-loop trip, not a hardware\n'
     '      // fault.  Once the wheel has rested 3 s with no open spin record, take\n'
     '      // the normal \'r\' path (driver check + reconfig verify + S1 clear).\n'
     '      {\n'
     '        static uint32_t autoRestSinceMs = 0;\n'
     '        static uint32_t autoClearCount = 0;\n'
     '        bool resting = encoderMotionReady() && fabsf(omega) <= STILL_REV_S &&\n'
     '                       !(stepper && stepper->isRunning()) && !spinOpen;\n'
     '        if (faultCode != FC_SUSTAINED_SPEEDUP || !resting) autoRestSinceMs = 0;\n'
     '        else if (autoRestSinceMs == 0) autoRestSinceMs = nowMs;\n'
     '        else if (nowMs - autoRestSinceMs >= 3000) {\n'
     '          autoRestSinceMs = 0;\n'
     '          Serial.printf("# auto-clear #%lu: SUSTAINED_SPEEDUP after 3 s rest\\n", (unsigned long)++autoClearCount);\n'
     '          handleCommandChar(\'r\');\n'
     '        }\n'
     '      }\n'
     '      break;\n'),
], 'auto-clear #%lu')
print('done')
