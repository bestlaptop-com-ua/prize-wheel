"""Flash 9 (2026-09-28): weak spins + dare recovery look natural; organic standby LEDs.
Applies to the flash-8 source (party8_20260928).  Exact-once anchors; aborts if any
drifts.  Universal newlines; the caller restores CRLF on MILL-PC.

Owner 18:38 (video): "Weak spins are not convincing enough.  The spin before last
one could've stopped before dare but it forcefully moved further which looked
unnatural.  Dare recovery is even worse.  It should somehow keep making minimal
speed until it is on safe wedge.  Can we add effect of bright random sparkles
during standby and this color wave is not organic."
Flash 8 spins 1-12: exact final ramp holds err within +/-2.2 deg; two dare stops:
spin 11 (shadow target 3.7 deg past dare 3, flapper drag stopped it 3.8 short, in
the dare -> P7) and spin 2 (weak q1 brake slipped 44 deg into dare 16 -> P7).
1. Shadow (q3): the landing itself must be >= 5 deg from dares (legacy only checked
   natural-4) - 6.5 deg; search natural-6..-12.5, then natural-5.5..-3 (lighter braking); no
   such landing -> no shadow (pass 5/3 decide).
2. Pass 5: nearest safe exit.  Besides carrying past the dare, consider stopping
   short of it (only <= 1.3x natural decel, 8 deg dare margin); take whichever is
   nearer the natural stop.  Carry clearance 10 -> 6 deg,
   so a carry ends just past the far peg instead of mid-wedge.
3. Dare recovery: first attempt leaves by the nearer peg when the entry peg is
   <= 5 deg behind (a roll-back), else goes on; stops RECOVERY_EXIT_DEG (6.5) past the exit peg (was the
   neighbour's centre) at a crawl (130 Hz ~0.04 rev/s, 150 sps^2, was 220/400) at
   3.0 A (was 2.2 A: 9/28 18:00 a 2.2 A push stalled on the flapper).  Settle
   stillness before the verdict in the dare zone 500 -> 150 ms, so the crawl
   starts almost as the wheel stops.
Independent review: blocking issue (hard 'before' brake) fixed as above, margins raised.
4. Standby LEDs: 2-D noise aurora (teal/blue/violet, breathing, no fixed period)
   + bright white glints (~5/s, random bursts, ~0.4 s fade)."""
from pathlib import Path
D = Path(__file__).resolve().parent / 'prize_wheel_gpt'


def once(name, old, new):
    p = D / name
    s = p.read_text(encoding='utf-8')
    n = s.count(old)
    assert n == 1, f'{name}: anchor count {n}: {old[:80]!r}'
    p.write_text(s.replace(old, new), encoding='utf-8', newline='\n')


INO = 'prize_wheel_gpt.ino'
IMPL = 'pw_party_impl.h'

# --- constants -------------------------------------------------------------
once(INO, '''const uint32_t RECOVERY_SPEED_HZ  = 220;   // ~0.07 rev/s
const uint32_t RECOVERY_ACCEL_SPS2 = 400;
const uint16_t RECOVERY_CURRENT_MA = 2200;
''', '''const uint32_t RECOVERY_SPEED_HZ  = 130;   // flash 9: ~0.04 rev/s crawl (was 220)
const uint32_t RECOVERY_ACCEL_SPS2 = 150;   // flash 9: gentle start (was 400)
const uint16_t RECOVERY_CURRENT_MA = 3000;  // flash 9: 2.2 A stalled on the flapper 9/28
const float RECOVERY_EXIT_DEG      = 6.5f;  // flash 9: rest this far past the exit peg
const uint16_t DARE_SETTLE_MS      = 150;   // flash 9: verdict wait in the dare zone
''')
once(INO, '''const float CARRY_CLEAR_DEG = 10.0f;      // carry target clearance from any dare edge
''', '''const float CARRY_CLEAR_DEG = 6.0f;       // flash 9: was 10 - stop just past (or short of) the dare
''')

# --- 1. shadow landing clearance ---------------------------------------------
once(INO, '''      for (float r = naturalDeg - 6.0f; r >= naturalDeg - 12.5f && r >= fmaxf(shadowMin, MIN_RESERVE_RUNWAY_DEG + 1.0f); r -= 0.5f) {
        float landAng = fmodf(curAngle + (float)dir * r, 360.0f);
        if (landAng < 0.0f) landAng += 360.0f;
        float within = fmodf(landAng, WEDGE_DEG);
        float lineDist = fminf(within, WEDGE_DEG - within);
        if (lineDist >= FLAPPER_KEEPOUT_DEG && !isDare(wedgeAtAngle(landAng)) &&
            dareDistanceDeg(landAng) >= DARE_PROXIMITY_FAULT_DEG + 3.0f) {
          out.runwayDeg = r;
          out.wedge = wedgeAtAngle(landAng);
          break;
        }
      }
''', '''      // flash 9: the landing itself must clear the dares too (9/28 spin 11 aimed
      // 3.7 deg past dare 3 and stopped in it); then try lighter braking up to
      // natural-3; nothing acceptable -> no shadow, passes 5/3 decide.
      bool shadowOk = false;
      const float rFloor = fmaxf(shadowMin, MIN_RESERVE_RUNWAY_DEG + 1.0f);
      for (int k = 0; k < 32 && !shadowOk; ++k) {
        float r = (k < 26) ? naturalDeg - 6.0f - 0.5f * (float)k       // -6 .. -18.5 (floored below)
                           : naturalDeg - 5.5f + 0.5f * (float)(k - 26); // -5.5 .. -3
        if (r < rFloor || r < naturalDeg - 12.5f) continue;
        float landAng = fmodf(curAngle + (float)dir * r, 360.0f);
        if (landAng < 0.0f) landAng += 360.0f;
        float within = fmodf(landAng, WEDGE_DEG);
        float lineDist = fminf(within, WEDGE_DEG - within);
        if (lineDist >= FLAPPER_KEEPOUT_DEG && !isDare(wedgeAtAngle(landAng)) &&
            dareDistanceDeg(landAng) >= DARE_PROXIMITY_FAULT_DEG + 4.5f) {   // 6.5: covers a 3.8 deg flapper undershoot
          out.runwayDeg = r;
          out.wedge = wedgeAtAngle(landAng);
          shadowOk = true;
        }
      }
      if (!shadowOk) out.found = false;
''')

# --- 2. pass 5: nearest safe exit ---------------------------------------------
once(INO, '''    if (isDare(wedgeAtAngle(natAng)) || dareDistanceDeg(natAng) < CARRY_TRIGGER_DEG) {
      for (float d = fmaxf(naturalDeg, MIN_RESERVE_RUNWAY_DEG); d <= naturalDeg + 2.5f * WEDGE_DEG;
           d += 1.0f) {
        float ang = fmodf(curAngle + (float)dir * d, 360.0f);
        if (ang < 0.0f) ang += 360.0f;
        if (!isDare(wedgeAtAngle(ang)) && dareDistanceDeg(ang) >= CARRY_CLEAR_DEG) {
          out.found = true;
          out.wedge = wedgeAtAngle(ang);
          out.runwayDeg = d;
          out.decelCapSps2 = ASSIST_DECEL_MAX_SPS2;
          out.quality = Q_CARRY;
          break;
        }
      }
    }
''', '''    if (isDare(wedgeAtAngle(natAng)) || dareDistanceDeg(natAng) < CARRY_TRIGGER_DEG) {
      float dAfter = -1.0f, dBefore = -1.0f;
      for (float d = fmaxf(naturalDeg, MIN_RESERVE_RUNWAY_DEG); d <= naturalDeg + 2.5f * WEDGE_DEG;
           d += 1.0f) {
        float ang = fmodf(curAngle + (float)dir * d, 360.0f);
        if (ang < 0.0f) ang += 360.0f;
        if (!isDare(wedgeAtAngle(ang)) && dareDistanceDeg(ang) >= CARRY_CLEAR_DEG) { dAfter = d; break; }
      }
      // flash 9 (owner: "could've stopped before dare but it forcefully moved
      // further"): or stop short of the dare, whichever is nearer the natural stop.
      // Only a gentle brake (<= 1.3x the natural decel: harder ones slipped on 9/23
      // and 9/28 spin 2), and the certified dare-facing margin.
      const float beforeFloor = fmaxf(fmaxf(assistMin, naturalDeg / 1.3f), MIN_RESERVE_RUNWAY_DEG + 1.0f);
      for (float d = naturalDeg - 1.0f; d >= beforeFloor && d >= naturalDeg - 2.5f * WEDGE_DEG; d -= 1.0f) {
        float ang = fmodf(curAngle + (float)dir * d, 360.0f);
        if (ang < 0.0f) ang += 360.0f;
        if (!isDare(wedgeAtAngle(ang)) && dareDistanceDeg(ang) >= DARE_EDGE_MARGIN_DEG) { dBefore = d; break; }
      }
      const bool useBefore = dBefore > 0.0f &&
          (dAfter < 0.0f || naturalDeg - dBefore <= dAfter - naturalDeg);
      if (useBefore || dAfter > 0.0f) {
        const float d = useBefore ? dBefore : dAfter;
        float ang = fmodf(curAngle + (float)dir * d, 360.0f);
        if (ang < 0.0f) ang += 360.0f;
        out.found = true;
        out.wedge = wedgeAtAngle(ang);
        out.runwayDeg = d;
        out.decelCapSps2 = ASSIST_DECEL_MAX_SPS2;
        out.quality = useBefore ? 1 : Q_CARRY;   // short of the dare = a brake (pass-3 kind)
      }
    }
''')

# --- 3. dare recovery: crawl just past the exit peg -----------------------------
once(INO, '''  wt %= NUM_WEDGES; if (wt < 0) wt += NUM_WEDGES;
  float center = wt * WEDGE_DEG + 0.5f * WEDGE_DEG;
  float dist = forwardDistanceDeg(dir, angle, center);
''', '''  wt %= NUM_WEDGES; if (wt < 0) wt += NUM_WEDGES;
  float center = wt * WEDGE_DEG + 0.5f * WEDGE_DEG;
  if (isDare(wedge) && dareRecoveryAttempts == 0 && fabsf(wheelGravity.forwardAccel(angle * DEG_TO_RAD, 1)) < 0.08f) {
    // flash 9: just over the entry peg -> roll back over it (reads as a flapper
    // bounce) instead of crawling through the whole dare.
    float backEdge = (dir > 0) ? (float)wedge * WEDGE_DEG : (float)(wedge + 1) * WEDGE_DEG;
    int back = -dir;
    int wb = ((wedge + back) % NUM_WEDGES + NUM_WEDGES) % NUM_WEDGES;
    if (!isDare(wb) && forwardDistanceDeg(back, angle, backEdge) <= 5.0f) { dir = back; wt = wb; }
  }
  if (isDare(wedge)) {
    // flash 9: out of a dare, crawl over the exit peg and rest just past it -
    // a wheel that barely made it - instead of walking to the neighbour's centre.
    float exitEdge = (dir > 0) ? (float)(wedge + 1) * WEDGE_DEG : (float)wedge * WEDGE_DEG;
    center = fmodf(exitEdge + (float)dir * RECOVERY_EXIT_DEG + 360.0f, 360.0f);
  }
  float dist = forwardDistanceDeg(dir, angle, center);
''')
once(INO, '''        } else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          landingVerdict();
''', '''        } else if (nowMs - settleStillSinceMs >=
                   ((isDare(currentWedge()) || dareDistanceDeg(wheelAngleDeg()) < DARE_PROXIMITY_FAULT_DEG)
                        ? DARE_SETTLE_MS : SETTLE_MS)) {   // flash 9: no visible pause on a dare
          landingVerdict();
''')

# --- 4. standby LEDs --------------------------------------------------------------
once(IMPL, '''static void pwLedRenderStandby(uint32_t nowMs) {
  uint8_t t1 = (uint8_t)(nowMs / 40);
  uint8_t t2 = (uint8_t)(nowMs / 57);
  for (int i = 0; i < PW_NUM_LEDS; ++i) {
    uint8_t a = sin8((uint8_t)(i * 3 + t1));       /* two counter-drifting     */
    uint8_t b = sin8((uint8_t)(i * 2 - t2));       /* sine hue waves           */
    uint8_t v = qadd8(a / 2, b / 2);
    pwLeds[i] = CHSV((uint8_t)(160 + (v >> 2)), 180, scale8(v, PW_LED_STANDBY_BRIGHT));
  }
}
''', '''/* flash 9 (owner 9/28: "bright random sparkles during standby and this color
 * wave is not organic"): two layers of slow 2-D noise (strip position x time)
 * - a drifting aurora of teal / blue / violet whose brightness breathes, with
 * no fixed period - plus bright white glints that strike random pixels and
 * fade over ~0.4 s (~5 per second, sometimes in small bursts).               */
static uint8_t pwSparkle[PW_NUM_LEDS];
static void pwLedRenderStandby(uint32_t nowMs) {
  const uint16_t tHue = (uint16_t)(nowMs / 11);
  const uint16_t tVal = (uint16_t)(nowMs / 7);
  for (int i = 0; i < PW_NUM_LEDS; ++i) {
    uint8_t h = inoise8((uint16_t)(i * 22), tHue);
    uint8_t b = inoise8((uint16_t)(i * 37 + 20000), tVal);
    b = qsub8(b, 70);
    b = qadd8(b, b);                                  /* deep troughs, soft crests */
    uint8_t hue = (uint8_t)(128 + scale8(h, 96));     /* 128 teal .. 224 violet    */
    CRGB c = CHSV(hue, 230, scale8(PW_LED_STANDBY_BRIGHT, qadd8(35, b)));
    uint8_t s = pwSparkle[i];
    if (s) {
      c += CRGB(s, s, s);
      s = scale8(s, 215);
      pwSparkle[i] = (s < 8) ? 0 : s;
    }
    pwLeds[i] = c;
  }
  if (random8() < 20) pwSparkle[random16(PW_NUM_LEDS)] = 255;
  if (random8() < 3)
    for (uint8_t k = 0; k < 3; ++k) pwSparkle[random16(PW_NUM_LEDS)] = (uint8_t)(200 + random8(56));
}
''')

# --- banner -------------------------------------------------------------------------
once(INO, 'Serial.println(F("# build: party8-20260928 (flash 8:',
     'Serial.println(F("# build: party9-20260928 (flash 9: nearest safe exit, crawl dare recovery, aurora + glints standby) on party8-20260928 (flash 8:')
print('flash9 applied')
