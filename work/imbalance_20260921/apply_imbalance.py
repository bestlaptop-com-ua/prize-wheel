"""Build work/imbalance_20260921/prize_wheel_gpt from the 2026-09-20 production-review
candidate plus the gravity-imbalance / AS5600-INL integration.  Every anchor must match
exactly once; nothing is written unless all of them do."""
import hashlib, json, shutil, sys
from pathlib import Path

ROOT = Path(r'C:\Users\Mill\Desktop\prize-wheel\work')
SRC = ROOT / 'production_review_20260920' / 'prize_wheel_gpt'
OUT = ROOT / 'imbalance_20260921'
DST = OUT / 'prize_wheel_gpt'

EDITS = []
def edit(name, old, new):
    EDITS.append((name, old, new))

edit('build string',
     '# build: plywood-capture-review-20260920; takeover defaults OFF',
     '# build: imbalance-model-20260921; based on plywood-capture-review-20260920; takeover defaults OFF')

edit('include',
     '#include "pw_friction_store.h"\n',
     '#include "pw_friction_store.h"\n#include "pw_imbalance.h"\n')

edit('globals',
     'int32_t encoderCountsMT = 0;\n',
     'int32_t encoderCountsMT = 0;\n'
     '// encoderCountsMT = uncorrected (rawZero - raw) accumulator + AS5600 INL offset.\n'
     '// With no stored model the offset is 0 and both are identical (legacy behaviour).\n'
     'int32_t encoderRawCountsMT = 0;\n'
     'static_assert(ENCODER_DIR_SIGN == -1, "INL offset sign assumes counts = rawZero - raw");\n'
     'PwEncoderInl encoderInl;\n'
     'PwGravity wheelGravity;\n'
     'PwImbalanceLine imbalanceLine;\n'
     'bool imbalanceLoaded = false;\n'
     '// Reach is judged at 96% speed: with gravity > friction the last crest is a\n'
     '// cliff (a 1% speed error can move the natural stop by most of a revolution).\n'
     'const float IMB_NATURAL_SPEED_MARGIN = 0.96f;\n'
     '// Mechanical-energy-equivalent speed at hand release; 0 = unknown (legacy re-push test).\n'
     'float releaseCompRevS = 0.0f;\n')

edit('fit sample struct',
     'struct FitSample { uint32_t ms; float omegaRadS; };',
     'struct FitSample { uint32_t ms; float omegaRadS; float angleRad; };')

edit('prime encoder',
     '  if (!encoderPrimed || !preserveNearestTurn) {\n'
     '    encoderCountsMT = staticCounts;\n'
     '  } else {\n'
     '    int32_t diff = encoderCountsMT - staticCounts;\n'
     '    int32_t turns = (int32_t)lroundf((float)diff / 4096.0f);\n'
     '    encoderCountsMT = staticCounts + turns * 4096;\n'
     '  }\n',
     '  if (!encoderPrimed || !preserveNearestTurn) {\n'
     '    encoderRawCountsMT = staticCounts;\n'
     '  } else {\n'
     '    int32_t diff = encoderRawCountsMT - staticCounts;\n'
     '    int32_t turns = (int32_t)lroundf((float)diff / 4096.0f);\n'
     '    encoderRawCountsMT = staticCounts + turns * 4096;\n'
     '  }\n'
     '  encoderCountsMT = encoderRawCountsMT + encoderInl.labelOffsetCounts(raw, rawZero);\n')

edit('update encoder',
     '  encoderCountsMT += ENCODER_DIR_SIGN * delta;   // invert to count clockwise\n',
     '  encoderRawCountsMT += ENCODER_DIR_SIGN * delta;   // invert to count clockwise\n'
     '  encoderCountsMT = encoderRawCountsMT + encoderInl.labelOffsetCounts(read.raw, rawZero);\n')

edit('natural stop',
     'float naturalStopDistanceDeg(float speedRevS, int dir) {\n'
     '  float c = (dir > 0) ? cw_c : ccw_c;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (speedRevS < 1e-3f) return 0.0f;\n',
     'float naturalStopDistanceDeg(float speedRevS, int dir) {\n'
     '  float c = (dir > 0) ? cw_c : ccw_c;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (speedRevS < 1e-3f) return 0.0f;\n'
     '  if (wheelGravity.active())  // from the CURRENT angle, conservative speed\n'
     '    return wheelGravity.stopTravelRad(speedRevS * IMB_NATURAL_SPEED_MARGIN, dir,\n'
     '        wheelAngleDeg() * DEG_TO_RAD, c, b, millis()) * RAD_TO_DEG;\n')

edit('braked stop',
     'float brakedStopDistanceDeg(float speedRevS, int dir, float extraRadS2) {\n'
     '  float c = ((dir > 0) ? cw_c : ccw_c) + extraRadS2;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (speedRevS < 1e-3f) return 0.0f;\n',
     'float brakedStopDistanceDeg(float speedRevS, int dir, float extraRadS2) {\n'
     '  float c = ((dir > 0) ? cw_c : ccw_c) + extraRadS2;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (speedRevS < 1e-3f) return 0.0f;\n'
     '  if (wheelGravity.active())\n'
     '    return wheelGravity.stopTravelRad(speedRevS, dir, wheelAngleDeg() * DEG_TO_RAD,\n'
     '        c, b, millis()) * RAD_TO_DEG;\n')

edit('natural decel',
     'float naturalDecelRevS2(float speedRevS, int dir) {\n'
     '  float c = (dir > 0) ? cw_c : ccw_c;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n',
     'float naturalDecelRevS2(float speedRevS, int dir) {\n'
     '  float c = (dir > 0) ? cw_c : ccw_c;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (wheelGravity.active() && speedRevS >= 1e-3f) {\n'
     '    // The instantaneous value swings through zero once per revolution, so use\n'
     '    // the energy-average over the natural runway, floored at 25% of friction.\n'
     '    float w = speedRevS * IMB_NATURAL_SPEED_MARGIN * TWO_PI;\n'
     '    float travel = wheelGravity.stopTravelRad(speedRevS * IMB_NATURAL_SPEED_MARGIN, dir,\n'
     '        wheelAngleDeg() * DEG_TO_RAD, c, b, millis());\n'
     '    float friction = c + b * speedRevS * TWO_PI;\n'
     '    float average = travel > 1e-3f ? (w * w) / (2.0f * travel) : friction;\n'
     '    return fmaxf(average, 0.25f * friction) / TWO_PI;\n'
     '  }\n')

edit('fit sample angle',
     '  fitSamples[fitSampleCount].omegaRadS = forward * TWO_PI;\n',
     '  fitSamples[fitSampleCount].omegaRadS = forward * TWO_PI;\n'
     '  fitSamples[fitSampleCount].angleRad = wheelAngleDeg() * DEG_TO_RAD;\n')

edit('fit gravity removal',
     '    float dropRadS = fitSamples[i].omegaRadS - fitSamples[j].omegaRadS;\n'
     '    if (dropRadS <= 0.0f) continue;          // reversal / speed-up: reject\n',
     '    float dropRadS = fitSamples[i].omegaRadS - fitSamples[j].omegaRadS;\n'
     '    if (wheelGravity.active()) {\n'
     '      // Give back what gravity did to the speed over this pair, so c/b stay\n'
     '      // friction-only.  Segments spanning > 1 rad are left alone: the trapezoid\n'
     '      // is meaningless there and whole revolutions cancel anyway.\n'
     '      for (uint8_t k = i; k < j; ++k) {\n'
     '        float segS = (float)(fitSamples[k + 1].ms - fitSamples[k].ms) / 1000.0f;\n'
     '        float segRad = 0.5f * (fitSamples[k].omegaRadS + fitSamples[k + 1].omegaRadS) * segS;\n'
     '        if (segRad > 1.0f) continue;\n'
     '        dropRadS += 0.5f * segS *\n'
     '            (wheelGravity.forwardAccel(fitSamples[k].angleRad, fitDir) +\n'
     '             wheelGravity.forwardAccel(fitSamples[k + 1].angleRad, fitDir));\n'
     '      }\n'
     '    }\n'
     '    if (dropRadS <= 0.0f) continue;          // reversal / speed-up: reject\n')

edit('abandon capture energy',
     '  fitLifecycle.discard();\n'
     '  if (faultCode == FC_NONE) {\n'
     '    state = ST_SPIN_RELEASED;\n',
     '  fitLifecycle.discard();\n'
     '  releaseCompRevS = 0.0f;  // the motor may have changed the energy: legacy re-push test\n'
     '  if (faultCode == FC_NONE) {\n'
     '    state = ST_SPIN_RELEASED;\n')

edit('release energy',
     '        spin.releaseRevS = speed;\n'
     '        resetFrictionCapture(spinDir);\n',
     '        spin.releaseRevS = speed;\n'
     '        releaseCompRevS = wheelGravity.compensatedRevS(speed, wheelAngleDeg() * DEG_TO_RAD);\n'
     '        resetFrictionCapture(spinDir);\n')

edit('re-push test',
     '      if (speed > spin.peakRevS * 1.02f && speed > SPIN_DETECT_REV_S) {\n',
     '      // A free coast can out-run its own peak on the heavy side coming down, but\n'
     '      // it can never gain mechanical energy: require both before calling it a push.\n'
     '      bool energyRose = !wheelGravity.active() || releaseCompRevS <= 0.0f ||\n'
     '          wheelGravity.compensatedRevS(speed, wheelAngleDeg() * DEG_TO_RAD) >\n'
     '              releaseCompRevS * 1.02f;\n'
     '      if (speed > spin.peakRevS * 1.02f && speed > SPIN_DETECT_REV_S && energyRose) {\n')

edit('help',
     '    " F  reset friction model to seeds\\n"\n',
     '    " F  reset friction model to seeds\\n"\n'
     '    " g  print imbalance + encoder INL model\\n"\n'
     '    " G<g>,<phiDeg>,<a1>,<b1>,<a2>,<b2>+Enter  set it (at rest; all zero clears)\\n"\n')

edit('serial line',
     '  char command = (char)Serial.read();\n'
     '  if (pwPartyCommandChar(command)) return;   // party commands: t/a/l/w/V<n>\n',
     '  char command = (char)Serial.read();\n'
     '  if (imbalanceLine.open()) {\n'
     '    PwImbalanceConfig parsed;\n'
     '    bool consumed = false, rejected = false;\n'
     '    bool complete = imbalanceLine.feed(command, millis(), &parsed, &consumed, &rejected);\n'
     '    if (rejected) Serial.println(F("# G rejected: need six finite in-range numbers; nothing changed"));\n'
     '    if (complete) applyImbalanceConfig(parsed);\n'
     '    if (consumed) return;\n'
     '  }\n'
     '  if (pwPartyCommandChar(command)) return;   // party commands: t/a/l/w/V<n>\n')

edit('commands',
     "    case 'F':\n"
     '      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {\n',
     "    case 'g':\n"
     '      printImbalance();\n'
     '      break;\n'
     "    case 'G':\n"
     '      if (state != ST_IDLE_STOPPED && state != ST_FAULT_LATCHED)\n'
     '        Serial.println(F("# G ignored: controller busy"));\n'
     '      else if (encoderVelocityValid && fabsf(omega) > STILL_REV_S)\n'
     '        Serial.println(F("# G ignored: wheel must be at rest"));\n'
     '      else imbalanceLine.begin(millis());\n'
     '      break;\n'
     "    case 'F':\n"
     '      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {\n')

edit('command helpers',
     'void handleSerial() {\n',
     'void printImbalance() {\n'
     '  const PwGravityModel& gm = wheelGravity.model();\n'
     '  const PwInlModel& im = encoderInl.model();\n'
     '  Serial.printf("# imbalance stored=%d g=%.4f rad/s2 phi=%.2f deg rest=%.1f deg | "\n'
     '                "inl a1=%.2f b1=%.2f a2=%.2f b2=%.2f counts | raw=%u inl_now=%.2f\\n",\n'
     '                imbalanceLoaded ? 1 : 0, gm.g, gm.phiRad * RAD_TO_DEG,\n'
     '                wheelGravity.active() ? wheelGravity.restAngleDeg() : 0.0f,\n'
     '                im.a1, im.b1, im.a2, im.b2, (unsigned)lastGoodRaw,\n'
     '                encoderInl.errorCounts(lastGoodRaw));\n'
     '}\n'
     '\n'
     '// Only reached at rest in IDLE/FAULT (the `G` gate), with a validated config.\n'
     'void applyImbalanceConfig(const PwImbalanceConfig& config) {\n'
     '  bool clear = config.gravity.g == 0.0f && config.inl.a1 == 0.0f && config.inl.b1 == 0.0f &&\n'
     '      config.inl.a2 == 0.0f && config.inl.b2 == 0.0f;\n'
     '  bool saved = preferencesAvailable &&\n'
     '      (clear ? pwClearImbalance(preferences) : pwSaveImbalance(preferences, config));\n'
     '  encoderInl.set(config.inl);\n'
     '  wheelGravity.set(config.gravity);\n'
     '  imbalanceLoaded = saved && !clear;\n'
     '  if (encoderPrimed) primeEncoder(lastGoodRaw, micros(), true);\n'
     '  Serial.printf("# imbalance model %s; persisted=%d\\n", clear ? "cleared" : "set", saved ? 1 : 0);\n'
     '  printImbalance();\n'
     '}\n'
     '\n'
     'void handleSerial() {\n')

edit('setup load',
     '  // rawZero/dir_ok/pos_sign are intentionally untouched: these legacy keys\n',
     '  if (preferencesAvailable) {\n'
     '    PwImbalanceConfig imbalance = pwLoadImbalance(preferences, &imbalanceLoaded);\n'
     '    encoderInl.set(imbalance.inl);\n'
     '    wheelGravity.set(imbalance.gravity);\n'
     '    printImbalance();\n'
     '  }\n'
     '  // rawZero/dir_ok/pos_sign are intentionally untouched: these legacy keys\n')


def main():
    ino = SRC / 'prize_wheel_gpt.ino'
    raw = ino.read_bytes()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8').replace('\r\n', '\n')
    problems = []
    for name, old, new in EDITS:
        count = text.count(old)
        if count != 1:
            problems.append('%s: anchor found %d times' % (name, count))
    if problems:
        print('\n'.join(problems))
        return 1
    for name, old, new in EDITS:
        text = text.replace(old, new)
    header = OUT / 'pw_imbalance.h'
    if not header.exists():
        print('missing', header)
        return 1
    if DST.exists():
        shutil.rmtree(DST)
    shutil.copytree(SRC, DST, ignore=shutil.ignore_patterns('build'))
    out = text.replace('\n', '\r\n') if crlf else text
    (DST / 'prize_wheel_gpt.ino').write_bytes(out.encode('utf-8'))
    shutil.copy2(header, DST / 'pw_imbalance.h')
    manifest = {str(p.relative_to(OUT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(DST.iterdir()) if p.is_file()}
    (OUT / 'source_manifest.json').write_text(json.dumps(manifest, indent=2))
    print('applied %d edits; crlf=%s; files=%d' % (len(EDITS), crlf, len(manifest)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
