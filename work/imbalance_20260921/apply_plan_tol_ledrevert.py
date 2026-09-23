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
    ('  if (!isfinite(forward) || forward < 0.02f || forward > CAPTURE_MAX_WHEEL_REV_S ||\n'
     '      hz < 40 || hz > 2400 || !isfinite(remaining) || remaining < 7.0f ||\n',
     '  if (!isfinite(forward) || forward < 0.02f || forward > CAPTURE_MAX_WHEEL_REV_S + 0.05f ||  // same launch tolerance as launchCapture\n'
     '      hz < 40 || hz > 2400 || !isfinite(remaining) || remaining < 7.0f ||\n'),
], 'same launch tolerance')

# LEDs: spin pattern back to the original rainbow bands; wedge colour only at the stop.
patch('pw_party_impl.h', [
    ('static void pwLedRenderSpin(float bandPhase, int8_t wedge) {\n'
     '  /* Strip shows the colour of the wedge under the pointer; brightness bands\n'
     '   * travel along the helix with speed AND direction from the live encoder. */\n'
     '  const CRGB c = pwWedgeColor(wedge);\n'
     '  uint16_t phase = (uint16_t)((int32_t)bandPhase & 0xFFFF);\n'
     '  for (int i = 0; i < PW_NUM_LEDS; ++i) {\n'
     '    uint8_t p = (uint8_t)(((uint32_t)i * 5u * 256u / PW_NUM_LEDS) - phase);\n'
     '    uint8_t v = qadd8(110, scale8(sin8((uint8_t)(p * 2)), 120));\n'
     '    pwLeds[i] = c; pwLeds[i].nscale8_video(v);\n'
     '  }\n}\n',
     'static void pwLedRenderSpin(float bandPhase, int8_t wedge) {\n'
     '  /* 5 colour bands travelling along the helix; speed AND direction from the\n'
     '   * live encoder omega so the lights can never contradict the wheel.          */\n'
     '  (void)wedge;\n'
     '  uint16_t phase = (uint16_t)((int32_t)bandPhase & 0xFFFF);\n'
     '  for (int i = 0; i < PW_NUM_LEDS; ++i) {\n'
     '    uint8_t hue = (uint8_t)(((uint32_t)i * 5u * 256u / PW_NUM_LEDS) - phase);\n'
     '    uint8_t v = sin8((uint8_t)(hue * 2));          /* band envelope            */\n'
     '    pwLeds[i] = CHSV(hue, 255, scale8(v, 150));\n'
     '  }\n}\n'),
], '(void)wedge;')
print('done')
