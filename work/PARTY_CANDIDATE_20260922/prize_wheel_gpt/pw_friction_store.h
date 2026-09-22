#pragma once
#include <math.h>
#include <stdint.h>

// 0x02 = direct-drive NEMA23 hardware; 0x01 = c+b*omega model schema.
// This tags friction only. It does not certify mechanical calibration.
static const uint16_t PW_FRICTION_VERSION = 0x0201;
static const char PW_FRICTION_VERSION_KEY[] = "fricVer";
struct PwFrictionModel { float c, b; uint16_t fits; };
struct PwFrictionBounds { float minC, maxC, minB, maxB; };
struct PwFrictionLoad {
  PwFrictionModel cw, ccw;
  bool migrated, resetCw, resetCcw, saved;
};

inline bool pwValidFriction(const PwFrictionModel& value, const PwFrictionBounds& bounds) {
  return isfinite(value.c) && isfinite(value.b) &&
      value.c >= bounds.minC && value.c <= bounds.maxC &&
      value.b >= bounds.minB && value.b <= bounds.maxB;
}

// Store is Preferences on-device and an in-memory store in host tests.
// These helpers have no access to the fault or calibration keys.
template <class Store>
bool pwSaveFrictionDirection(Store& store, bool cw, const PwFrictionModel& model) {
  const char* cKey = cw ? "cwC" : "ccwC";
  const char* bKey = cw ? "cwB" : "ccwB";
  const char* nKey = cw ? "cwN" : "ccwN";
  if (store.putFloat(cKey, model.c) != sizeof(float)) return false;
  if (store.putFloat(bKey, model.b) != sizeof(float)) return false;
  if (store.putUShort(nKey, model.fits) != sizeof(uint16_t)) return false;
  return store.getFloat(cKey, NAN) == model.c &&
      store.getFloat(bKey, NAN) == model.b &&
      store.getUShort(nKey, (uint16_t)~model.fits) == model.fits;
}

template <class Store>
bool pwSaveFrictionVersion(Store& store) {
  return store.putUShort(PW_FRICTION_VERSION_KEY, PW_FRICTION_VERSION) == sizeof(uint16_t) &&
      store.getUShort(PW_FRICTION_VERSION_KEY, 0) == PW_FRICTION_VERSION;
}

template <class Store>
PwFrictionLoad pwLoadFriction(Store& store, const PwFrictionModel& seed,
                              const PwFrictionBounds& bounds) {
  PwFrictionLoad result;
  result.migrated = store.getUShort(PW_FRICTION_VERSION_KEY, 0) != PW_FRICTION_VERSION;
  result.cw = {store.getFloat("cwC", NAN), store.getFloat("cwB", NAN), store.getUShort("cwN", 0)};
  result.ccw = {store.getFloat("ccwC", NAN), store.getFloat("ccwB", NAN), store.getUShort("ccwN", 0)};
  result.resetCw = result.migrated || !pwValidFriction(result.cw, bounds);
  result.resetCcw = result.migrated || !pwValidFriction(result.ccw, bounds);
  if (result.resetCw) result.cw = seed;
  if (result.resetCcw) result.ccw = seed;
  result.saved = true;
  if (result.resetCw) result.saved = pwSaveFrictionDirection(store, true, result.cw);
  if (result.resetCcw) {
    bool savedCcw = pwSaveFrictionDirection(store, false, result.ccw);
    result.saved = result.saved && savedCcw;
  }
  // Commit the marker last: interrupted migration is retried on the next boot.
  if (result.migrated && result.saved) result.saved = pwSaveFrictionVersion(store);
  return result;
}

