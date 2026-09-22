#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Wheel-fixed gravity imbalance + AS5600 harmonic nonlinearity (2026-09-21).
//
// Owner decision: the 36in wheel's heavy spot is NOT being balanced out, so the
// firmware models it.  Separately, the hand-spin audit of 2026-09-21 measured a
// 2x/rev AS5600 angle error of ~1.5 deg amplitude (3.8 deg p-p) that reproduces
// the step-vs-encoder "slip" of the 2026-09-20 powered traces.
//
// Pure logic: no Arduino, no globals, host-testable.  A zeroed model reproduces
// the legacy behaviour exactly (every entry point short-circuits).
//
// Frames
//   INL is a property of sensor+magnet, so it lives in the RAW frame:
//     measured_raw = true_raw + e(measured_raw)
//     e(raw) = a1 sin x + b1 cos x + a2 sin 2x + b2 cos 2x,  x = 2*pi*raw/4096   [counts]
//   Imbalance is a property of the wheel, so it lives in the LABEL frame
//   (A = label angle, clockwise positive, the frame rawZero anchors):
//     A'' = g * sin(A - phi)    [rad/s^2]      stable rest at A = phi + pi
//   Re-seating the magnet invalidates the INL terms but not g/phi (after `z`).

static const uint16_t PW_IMBALANCE_VERSION = 0x0101;
static const char PW_IMBALANCE_VERSION_KEY[] = "imbVer";
static const float PW_IMB_TWO_PI = 6.28318530718f;

struct PwInlModel { float a1, b1, a2, b2; };        // counts
struct PwGravityModel { float g, phiRad; };         // rad/s^2, rad (label frame)
struct PwImbalanceConfig { PwGravityModel gravity; PwInlModel inl; };

static const float PW_INL_MAX_COUNTS = 64.0f;       // 5.6 deg: beyond this, fix the magnet
static const float PW_GRAVITY_MAX_RAD_S2 = 1.5f;

inline bool pwValidImbalance(const PwImbalanceConfig& c) {
  const float v[4] = {c.inl.a1, c.inl.b1, c.inl.a2, c.inl.b2};
  for (uint8_t i = 0; i < 4; ++i)
    if (!isfinite(v[i]) || fabsf(v[i]) > PW_INL_MAX_COUNTS) return false;
  return isfinite(c.gravity.g) && isfinite(c.gravity.phiRad) &&
      c.gravity.g >= 0.0f && c.gravity.g <= PW_GRAVITY_MAX_RAD_S2 &&
      fabsf(c.gravity.phiRad) <= 2.0f * PW_IMB_TWO_PI;
}

/* ------------------------------ encoder INL ------------------------------- */
class PwEncoderInl {
 public:
  void set(const PwInlModel& m) {
    model_ = m;
    active_ = (m.a1 != 0.0f || m.b1 != 0.0f || m.a2 != 0.0f || m.b2 != 0.0f);
    for (uint16_t i = 0; i <= 256; ++i) {
      float x = PW_IMB_TWO_PI * (float)(i & 255) / 256.0f;
      lut_[i] = m.a1 * sinf(x) + m.b1 * cosf(x) + m.a2 * sinf(2.0f * x) + m.b2 * cosf(2.0f * x);
    }
  }
  bool active() const { return active_; }
  const PwInlModel& model() const { return model_; }
  // Reading error in counts at a measured raw value (16-count LUT, linear interpolation).
  float errorCounts(uint16_t raw) const {
    if (!active_) return 0.0f;
    raw &= 0x0FFF;
    uint16_t i = raw >> 4;
    float f = (float)(raw & 15) * (1.0f / 16.0f);
    return lut_[i] + (lut_[i + 1] - lut_[i]) * f;
  }
  // Offset to ADD to a label-frame count built as (rawZero - raw):
  //   (rawZero - e(rawZero)) - (raw - e(raw)) = (rawZero - raw) + e(raw) - e(rawZero)
  // rawZero stays in the uncorrected sensor frame, so changing the INL terms
  // never moves the physical anchor.
  int32_t labelOffsetCounts(uint16_t raw, uint16_t rawZero) const {
    if (!active_) return 0;
    return (int32_t)lroundf(errorCounts(raw) - errorCounts(rawZero));
  }
 private:
  PwInlModel model_ = {0, 0, 0, 0};
  bool active_ = false;
  float lut_[257] = {};
};

/* --------------------------- gravity imbalance ---------------------------- */
class PwGravity {
 public:
  void set(const PwGravityModel& m) {
    model_ = m; active_ = m.g > 0.0f; invalidate();
    for (uint16_t i = 0; i <= 256; ++i)
      sinLut_[i] = sinf(PW_IMB_TWO_PI * (float)(i & 255) / 256.0f);
  }
  bool active() const { return active_; }
  const PwGravityModel& model() const { return model_; }
  float restAngleDeg() const {
    float a = fmodf((model_.phiRad + 0.5f * PW_IMB_TWO_PI) * 360.0f / PW_IMB_TWO_PI, 360.0f);
    return a < 0.0f ? a + 360.0f : a;
  }
  // Acceleration along the direction of travel (dir = +1 cw / -1 ccw), rad/s^2.
  float forwardAccel(float angleRad, int dir) const {
    return active_ ? (float)dir * model_.g * fastSin(angleRad - model_.phiRad) : 0.0f;
  }
  // Speed the wheel would carry at the bottom of the gravity well with the
  // same mechanical energy.  Non-increasing in ANY free coast, so it can stand
  // in for raw speed wherever "speed rose" means "somebody pushed".
  float compensatedRevS(float speedRevS, float angleRad) const {
    if (!active_) return speedRevS;
    float w = speedRevS * PW_IMB_TWO_PI;
    float cosTerm = fastSin(angleRad - model_.phiRad + 0.25f * PW_IMB_TWO_PI);
    float w2 = w * w + 2.0f * model_.g * (1.0f + cosTerm);
    return sqrtf(w2) / PW_IMB_TWO_PI;
  }

  // Closed-form coast travel for alpha = c + b*w (legacy model), rad.
  static float legacyTravelRad(float w, float c, float b) {
    if (w <= 0.0f) return 0.0f;
    if (b < 1e-6f) return w * w / (2.0f * c);
    return (1.0f / b) * w - (c / (b * b)) * logf(1.0f + b * w / c);
  }

  // Forward travel until the wheel first stops, rad.  Callers fold any motor
  // braking authority into c.  The first zero crossing is the stop: on
  // 2026-09-21 the unpowered wheel held at near-worst-case gravity torque.
  // A free coast keeps its stop POSITION, so within 10 ms the cached answer is
  // just shortened by the angle travelled (keeps the 1 kHz loop cheap).
  float stopTravelRad(float speedRevS, int dir, float angleRad, float c, float b,
                      uint32_t nowMs) {
    float w = speedRevS * PW_IMB_TWO_PI;
    if (w < 6.3e-3f) return 0.0f;
    if (!active_) return legacyTravelRad(w, c, b);
    Slot* slot = nullptr;
    for (uint8_t i = 0; i < SLOTS; ++i) {
      Slot& k = slots_[i];
      if (k.valid && k.dir == dir && k.c == c && k.b == b) { slot = &k; break; }
    }
    if (slot && nowMs - slot->ms < 10 && fabsf(w - slot->w) <= 0.02f * slot->w) {
      float moved = (float)dir * (angleRad - slot->angle);
      if (moved > -0.2f && moved < 0.2f) {
        float left = slot->travel - moved;
        return left > 0.0f ? left : 0.0f;
      }
    }
    if (!slot) { slot = &slots_[next_]; next_ = (uint8_t)((next_ + 1) % SLOTS); }

    float travel = integrate(w, dir, angleRad, c, b);
    slot->valid = true; slot->dir = dir; slot->c = c; slot->b = b;
    slot->w = w; slot->angle = angleRad; slot->travel = travel; slot->ms = nowMs;
    return travel;
  }
  void invalidate() { for (uint8_t i = 0; i < SLOTS; ++i) slots_[i].valid = false; }

  float integrate(float w, int dir, float angleRad, float c, float b) const {
    float travel = 0.0f;
    // Whole revolutions are gravity-neutral, so cover them in closed form while
    // the wheel is fast, then integrate only the last few revolutions.
    float wSwitch = sqrtf(25.0f * model_.g);
    if (wSwitch < 2.5f) wSwitch = 2.5f;
    if (w > wSwitch) {
      float full = legacyTravelRad(w, c, b);
      float revs = floorf((full - legacyTravelRad(wSwitch, c, b)) / PW_IMB_TWO_PI);
      if (revs >= 1.0f) {
        float want = full - revs * PW_IMB_TWO_PI;
        float w1 = wSwitch;                       // Newton on S(w1) = want, S' = w/(c+b w)
        for (uint8_t i = 0; i < 8; ++i) {
          float err = legacyTravelRad(w1, c, b) - want;
          float slope = w1 / (c + b * w1);
          if (slope < 1e-4f) break;
          w1 -= err / slope;
          if (w1 < wSwitch) w1 = wSwitch;
        }
        travel = revs * PW_IMB_TWO_PI;
        w = w1;
      }
    }
    // Midpoint integration of d(w^2)/ds = 2*a(s, w) in 5 degree steps.
    const float ds = 0.0872665f;
    float w2 = w * w;
    float s = 0.0f;
    for (uint16_t n = 0; n < 4000; ++n) {
      float a1 = -(c + b * sqrtf(w2)) + forwardAccel(angleRad + (float)dir * s, dir);
      float w2h = w2 + a1 * ds;                   // half step: 2*a1*(ds/2)
      if (w2h <= 0.0f) { if (a1 < 0.0f) s += w2 / (-2.0f * a1); break; }
      float a2 = -(c + b * sqrtf(w2h)) + forwardAccel(angleRad + (float)dir * (s + 0.5f * ds), dir);
      float w2n = w2 + 2.0f * a2 * ds;
      if (w2n <= 0.0f) { s += ds * w2 / (w2 - w2n); break; }
      w2 = w2n;
      s += ds;
    }
    return travel + s;
  }

 private:
  float fastSin(float x) const {
    float t = x * (256.0f / PW_IMB_TWO_PI);
    float fl = floorf(t);
    float f = t - fl;
    uint16_t i = (uint16_t)((int32_t)fl & 255);
    return sinLut_[i] + (sinLut_[i + 1] - sinLut_[i]) * f;
  }
  static const uint8_t SLOTS = 4;
  struct Slot { bool valid; int dir; float c, b, w, angle, travel; uint32_t ms; };
  PwGravityModel model_ = {0, 0};
  bool active_ = false;
  float sinLut_[257] = {};
  Slot slots_[SLOTS] = {};
  uint8_t next_ = 0;
};

/* ------------------------------ persistence ------------------------------- */
// Store is Preferences on-device and an in-memory store in host tests.
template <class Store>
bool pwSaveImbalance(Store& store, const PwImbalanceConfig& c) {
  if (!pwValidImbalance(c)) return false;
  const char* keys[6] = {"imbG", "imbPhi", "inlA1", "inlB1", "inlA2", "inlB2"};
  const float vals[6] = {c.gravity.g, c.gravity.phiRad, c.inl.a1, c.inl.b1, c.inl.a2, c.inl.b2};
  for (uint8_t i = 0; i < 6; ++i) {
    if (store.putFloat(keys[i], vals[i]) != sizeof(float)) return false;
    if (store.getFloat(keys[i], NAN) != vals[i]) return false;
  }
  // Marker last: an interrupted save reads back as "no model" on the next boot.
  return store.putUShort(PW_IMBALANCE_VERSION_KEY, PW_IMBALANCE_VERSION) == sizeof(uint16_t) &&
      store.getUShort(PW_IMBALANCE_VERSION_KEY, 0) == PW_IMBALANCE_VERSION;
}

template <class Store>
bool pwClearImbalance(Store& store) {
  return store.putUShort(PW_IMBALANCE_VERSION_KEY, 0) == sizeof(uint16_t) &&
      store.getUShort(PW_IMBALANCE_VERSION_KEY, 1) == 0;
}

// Returns a zeroed (legacy-equivalent) config unless a complete valid one is stored.
template <class Store>
PwImbalanceConfig pwLoadImbalance(Store& store, bool* loaded) {
  PwImbalanceConfig zero = {{0, 0}, {0, 0, 0, 0}};
  if (loaded) *loaded = false;
  if (store.getUShort(PW_IMBALANCE_VERSION_KEY, 0) != PW_IMBALANCE_VERSION) return zero;
  PwImbalanceConfig c = {
      {store.getFloat("imbG", NAN), store.getFloat("imbPhi", NAN)},
      {store.getFloat("inlA1", NAN), store.getFloat("inlB1", NAN),
       store.getFloat("inlA2", NAN), store.getFloat("inlB2", NAN)}};
  if (!pwValidImbalance(c)) return zero;
  if (loaded) *loaded = true;
  return c;
}

/* ------------------------------ serial entry ------------------------------ */
// `G` opens a line: G<g>,<phiDeg>,<a1>,<b1>,<a2>,<b2><Enter>.  Six finite
// numbers or nothing; a stale or malformed line changes no state.
class PwImbalanceLine {
 public:
  bool open() const { return open_; }
  void begin(uint32_t nowMs) { open_ = true; len_ = 0; sinceMs_ = nowMs; }
  // Returns true when a complete valid line was parsed into `out`.
  // `consumed` reports whether the character belonged to the line.
  bool feed(char ch, uint32_t nowMs, PwImbalanceConfig* out, bool* consumed, bool* rejected) {
    *consumed = false; *rejected = false;
    if (!open_) return false;
    if (nowMs - sinceMs_ > 5000) { open_ = false; *rejected = true; return false; }
    *consumed = true;
    sinceMs_ = nowMs;
    if (ch != '\n' && ch != '\r') {
      bool ok = (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' ||
          ch == ',' || ch == 'e' || ch == 'E' || ch == ' ';
      if (!ok || (size_t)len_ + 1 >= sizeof(buf_)) { open_ = false; *rejected = true; return false; }
      buf_[len_++] = ch;
      return false;
    }
    open_ = false;
    buf_[len_] = 0;
    float v[6];
    char* p = buf_;
    for (uint8_t i = 0; i < 6; ++i) {
      char* end = nullptr;
      v[i] = strtof(p, &end);
      if (end == p || !isfinite(v[i])) { *rejected = true; return false; }
      p = end;
      while (*p == ' ') ++p;
      if (i < 5) { if (*p != ',') { *rejected = true; return false; } ++p; }
    }
    while (*p == ' ') ++p;
    if (*p != 0) { *rejected = true; return false; }
    PwImbalanceConfig c = {{v[0], v[1] * PW_IMB_TWO_PI / 360.0f}, {v[2], v[3], v[4], v[5]}};
    if (!pwValidImbalance(c)) { *rejected = true; return false; }
    *out = c;
    return true;
  }
 private:
  bool open_ = false;
  uint8_t len_ = 0;
  uint32_t sinceMs_ = 0;
  char buf_[96] = {};
};
