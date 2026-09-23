from pathlib import Path
ROOT = Path(__file__).resolve().parent / 'prize_wheel_gpt'
def patch(fname, edits, marker):
    p = ROOT / fname; s = p.read_text(encoding='utf-8')
    if marker in s: print(fname, 'already patched'); return
    for old, new in edits:
        assert s.count(old) == 1, f'{fname}: anchor not unique/found: {old[:70]!r}'
    for old, new in edits: s = s.replace(old, new)
    p.write_text(s, encoding='utf-8'); print(fname, 'patched', len(edits), 'edits')

patch('pw_party_impl.h', [
    ('static volatile uint32_t pwFxCelebrateAtMs = 0;  /* celebrate window start     */\n',
     'static volatile uint32_t pwFxCelebrateAtMs = 0;  /* celebrate window start     */\n'
     'static volatile int8_t pwFxWedge = -1;           /* wedge index under pointer  */\n'
     'static volatile int8_t pwFxCelebrateWedge = -1;  /* landing wedge index        */\n'
     '#if PW_FX_LED_ENABLE\n'
     '/* Wedge colours by LABEL (index+1), from the physical wheel (2026-09-22):\n'
     ' * 1 G, 2 B, 3 Y, 4 R, 5 P, 6 LB, 7 G, 8 Y, 9 R, 10 G, 11 B, 12 Y, 13 R,\n'
     ' * 14 P, 15 B, 16 G, 17 Y, 18 R.                                            */\n'
     '#define PWC_G  CRGB(0, 255, 0)\n#define PWC_B  CRGB(0, 0, 255)\n#define PWC_Y  CRGB(255, 190, 0)\n'
     '#define PWC_R  CRGB(255, 0, 0)\n#define PWC_P  CRGB(255, 0, 110)\n#define PWC_LB CRGB(0, 140, 255)\n'
     'static const CRGB PW_WEDGE_RGB[18] = {\n'
     '  PWC_G, PWC_B, PWC_Y, PWC_R, PWC_P, PWC_LB, PWC_G, PWC_Y, PWC_R,\n'
     '  PWC_G, PWC_B, PWC_Y, PWC_R, PWC_P, PWC_B, PWC_G, PWC_Y, PWC_R };\n'
     'static inline CRGB pwWedgeColor(int8_t w) { return (w >= 0 && w < 18) ? PW_WEDGE_RGB[w] : CRGB(255, 255, 255); }\n'
     '#endif\n'),
    ('static void pwLedRenderSpin(float bandPhase) {\n'
     '  /* 5 colour bands travelling along the helix; speed AND direction from the\n'
     '   * live encoder omega so the lights can never contradict the wheel.          */\n'
     '  uint16_t phase = (uint16_t)((int32_t)bandPhase & 0xFFFF);\n'
     '  for (int i = 0; i < PW_NUM_LEDS; ++i) {\n'
     '    uint8_t hue = (uint8_t)(((uint32_t)i * 5u * 256u / PW_NUM_LEDS) - phase);\n'
     '    uint8_t v = sin8((uint8_t)(hue * 2));          /* band envelope            */\n'
     '    pwLeds[i] = CHSV(hue, 255, scale8(v, 150));\n'
     '  }\n}\n',
     'static void pwLedRenderSpin(float bandPhase, int8_t wedge) {\n'
     '  /* Strip shows the colour of the wedge under the pointer; brightness bands\n'
     '   * travel along the helix with speed AND direction from the live encoder. */\n'
     '  const CRGB c = pwWedgeColor(wedge);\n'
     '  uint16_t phase = (uint16_t)((int32_t)bandPhase & 0xFFFF);\n'
     '  for (int i = 0; i < PW_NUM_LEDS; ++i) {\n'
     '    uint8_t p = (uint8_t)(((uint32_t)i * 5u * 256u / PW_NUM_LEDS) - phase);\n'
     '    uint8_t v = qadd8(110, scale8(sin8((uint8_t)(p * 2)), 120));\n'
     '    pwLeds[i] = c; pwLeds[i].nscale8_video(v);\n'
     '  }\n}\n'),
    ('static void pwLedRenderCelebrate(uint32_t elapsedMs) {\n',
     'static void pwLedRenderCelebrate(uint32_t elapsedMs, int8_t wedge) {\n'),
    ('    fill_solid(pwLeds, PW_NUM_LEDS, CHSV((uint8_t)(phase * 37), 255, bright));\n',
     '    CRGB c = pwWedgeColor(wedge); c.nscale8_video(bright);   /* landing wedge colour */\n'
     '    fill_solid(pwLeds, PW_NUM_LEDS, c);\n'),
    ('      pwLedRenderCelebrate(nowMs - celAt);\n', '      pwLedRenderCelebrate(nowMs - celAt, pwFxCelebrateWedge);\n'),
    ('      pwLedRenderSpin(bandPhase);\n', '      pwLedRenderSpin(bandPhase, pwFxWedge);\n'),
    ('      pwFxCelebrateAtMs = pwFanfareAtMs;             /* LEDs sync to fanfare   */\n',
     '      pwFxCelebrateWedge = (int8_t)spin.finalWedge;\n'
     '      pwFxCelebrateAtMs = pwFanfareAtMs;             /* LEDs sync to fanfare   */\n'),
    ('  pwFxOmega = encoderVelocityValid ? omega : 0.0f;\n',
     '  pwFxOmega = encoderVelocityValid ? omega : 0.0f;\n  pwFxWedge = (int8_t)currentWedge();\n'),
], 'PW_WEDGE_RGB')
print('done')
