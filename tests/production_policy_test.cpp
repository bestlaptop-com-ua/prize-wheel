#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "../prize_wheel_gpt/pw_fault_policy.h"
#include "../prize_wheel_gpt/pw_fit_lifecycle.h"
#include "../prize_wheel_gpt/pw_friction_store.h"

struct MemoryStore {
  std::map<std::string, float> floats;
  std::map<std::string, uint16_t> shorts;
  std::vector<std::string> writes;
  std::string failKey;
  float getFloat(const char* key, float fallback) const {
    auto found = floats.find(key);
    return found == floats.end() ? fallback : found->second;
  }
  uint16_t getUShort(const char* key, uint16_t fallback) const {
    auto found = shorts.find(key);
    return found == shorts.end() ? fallback : found->second;
  }
  size_t putFloat(const char* key, float value) {
    writes.emplace_back(key);
    if (failKey == key) return 0;
    floats[key] = value;
    return sizeof(value);
  }
  size_t putUShort(const char* key, uint16_t value) {
    writes.emplace_back(key);
    if (failKey == key) return 0;
    shorts[key] = value;
    return sizeof(value);
  }
};

const PwFrictionModel seed = {0.55f, 0.28f, 0};
const PwFrictionBounds bounds = {0.02f, 3.0f, 0.005f, 1.5f};

void testFrictionPersistence() {
  MemoryStore store;
  // Valid-looking legacy values have no hardware provenance: migrate once.
  store.floats = {{"cwC", 0.546f}, {"cwB", 0.15f}, {"ccwC", 0.680f}, {"ccwB", 0.15f}};
  store.shorts = {{"cwN", uint16_t(7)}, {"ccwN", uint16_t(9)}, {"fltLatch", uint16_t(255)},
      {"rawZero", uint16_t(3807)}, {"dir_ok", uint16_t(1)}, {"pos_sign", uint16_t(1)}};
  auto first = pwLoadFriction(store, seed, bounds);
  assert(first.migrated && first.resetCw && first.resetCcw && first.saved);
  assert(first.cw.c == seed.c && first.cw.fits == 0 && first.ccw.fits == 0);
  assert(store.writes.back() == PW_FRICTION_VERSION_KEY);
  for (const auto& key : store.writes)
    assert(key != "fltLatch" && key != "rawZero" && key != "dir_ok" && key != "pos_sign");
  assert(store.shorts["fltLatch"] == 255 && store.shorts["rawZero"] == 3807);
  assert(store.shorts["dir_ok"] == 1 && store.shorts["pos_sign"] == 1);

  assert(pwSaveFrictionDirection(store, true, {0.17f, 0.11f, 4}));
  assert(pwSaveFrictionDirection(store, false, {0.19f, 0.13f, 3}));
  store.writes.clear();
  auto reboot = pwLoadFriction(store, seed, bounds);
  assert(!reboot.migrated && !reboot.resetCw && !reboot.resetCcw && reboot.saved);
  assert(reboot.cw.c == 0.17f && reboot.cw.b == 0.11f && reboot.cw.fits == 4);
  assert(reboot.ccw.c == 0.19f && reboot.ccw.b == 0.13f && reboot.ccw.fits == 3);
  assert(store.writes.empty());

  // Invalid data resets only the affected direction, never the valid peer.
  store.floats["cwC"] = NAN;
  auto repaired = pwLoadFriction(store, seed, bounds);
  assert(!repaired.migrated && repaired.resetCw && !repaired.resetCcw && repaired.saved);
  assert(repaired.cw.fits == 0 && repaired.ccw.fits == 3);
  for (const auto& key : store.writes) assert(key != "ccwC" && key != "ccwB" && key != "ccwN");
  assert(!pwValidFriction({0.5f, INFINITY, 2}, bounds));
  assert(!pwValidFriction({0.5f, 0.0f, 2}, bounds));

  // Hardware/model change invalidates the prior fits, even if in range.
  store.shorts[PW_FRICTION_VERSION_KEY] = 0x0101;
  auto changedHardware = pwLoadFriction(store, seed, bounds);
  assert(changedHardware.migrated && changedHardware.cw.fits == 0 && changedHardware.ccw.fits == 0);
  assert(store.shorts["fltLatch"] == 255);

  // Failed migration cannot commit the version marker; next boot retries it.
  MemoryStore interrupted;
  interrupted.failKey = "ccwB";
  auto failed = pwLoadFriction(interrupted, seed, bounds);
  assert(failed.migrated && !failed.saved);
  assert(interrupted.getUShort(PW_FRICTION_VERSION_KEY, 0) == 0);
  interrupted.failKey.clear();
  auto retry = pwLoadFriction(interrupted, seed, bounds);
  assert(retry.migrated && retry.saved);
  assert(!pwLoadFriction(interrupted, seed, bounds).migrated);
}

void testFitLifecycle() {
  PwFitLifecycle fit;
  assert(!fit.canSample(true)); // push/boot are not free-coast samples
  fit.begin();
  // Early reservation fails with too few samples; longer coast stays usable.
  assert(!fit.beginAttempt(PwFitEnd::Preview, false, 3));
  assert(fit.open() && fit.canSample(true));
  assert(!fit.beginAttempt(PwFitEnd::Preview, false, 21));
  assert(fit.open());
  assert(fit.beginAttempt(PwFitEnd::Preview, true, 22));
  fit.completeAttempt(false); // ill-conditioned/bounds-rejected clean fit
  assert(fit.open());
  assert(!fit.beginAttempt(PwFitEnd::Preview, true, 22)); // no serial/CPU flood on same data
  assert(fit.beginAttempt(PwFitEnd::Preview, true, 26));
  fit.completeAttempt(true);
  assert(!fit.open());
  assert(!fit.beginAttempt(PwFitEnd::CoastEnd, true, 26)); // cannot double-apply

  fit.begin();
  assert(!fit.beginAttempt(PwFitEnd::Powered, false, 8));
  assert(!fit.canSample(true)); // failed takeover returning to RELEASED stays closed
  fit.begin();
  assert(fit.beginAttempt(PwFitEnd::Powered, true, 30)); // final clean pre-enable fit
  fit.completeAttempt(false);
  assert(!fit.open());
  fit.begin();
  assert(!fit.canSample(false)); // physical EN/current guard independently closes it
  assert(!fit.canSample(true));

  for (PwFitEnd end : {PwFitEnd::Contact, PwFitEnd::Reversal, PwFitEnd::Repush, PwFitEnd::Fault}) {
    fit.begin();
    assert(!fit.beginAttempt(end, true, 40)); // reject known contaminated segment
    assert(!fit.open());
  }
  fit.begin();
  assert(fit.beginAttempt(PwFitEnd::Preview, true, 30));
  fit.completeAttempt(false, true); // estimator also found contact
  assert(!fit.open());
  fit.begin();
  assert(!fit.beginAttempt(PwFitEnd::CoastEnd, false, 2));
  assert(!fit.open());
}

void testFaultCompatibility() {
  for (unsigned raw = 0; raw < 256; ++raw) {
    const bool knownId = raw <= 15 || raw == 17;
    auto decoded = pwDecodeStoredFault((uint8_t)raw, true, 17, 16);
    assert(decoded.raw == raw && decoded.locked == (raw != 0));
    assert(decoded.unknown == !knownId);
    assert(decoded.code == (knownId ? raw : 16));
    // Repeated/new boot faults never replace any saved nonzero byte.
    assert(pwFirstFaultByte((uint8_t)raw, 4) == (raw == 0 ? 4 : raw));
    assert(pwMayClearStoredFault(decoded, (uint8_t)raw) == knownId);
    assert(!pwMayClearStoredFault(decoded, (uint8_t)(raw ^ 1)));
  }
  auto unavailable = pwDecodeStoredFault(0, false, 15, 16);
  assert(unavailable.locked && unavailable.unknown && unavailable.raw == 255);
  assert(!pwMayClearStoredFault(unavailable, 255));
  auto diagnostic = pwDecodeStoredFault(15, true, 15, 16);
  assert(diagnostic.locked && !diagnostic.unknown && diagnostic.code == 15);

  // Every possible journal/primary pair: only both zero may boot unlocked.
  // Unknown primary bytes remain opaque, including when a guard also exists.
  for (unsigned primary = 0; primary < 256; ++primary) {
    for (unsigned guard = 0; guard < 256; ++guard) {
      uint8_t restored = pwRestoreFaultWithRecoveryGuard((uint8_t)primary, (uint8_t)guard, 15);
      auto journal = pwDecodeStoredFault(restored, true, 17, 16);
      assert(journal.locked == (primary != 0 || guard != 0));
      if (primary > 15 || guard == 0) assert(journal.raw == primary);
      else if (guard == 15) assert(journal.raw == (primary != 0 ? primary : 15));
      else assert(journal.unknown && journal.raw == 255);
      bool canClear = pwMayClearStoredFault(journal, (uint8_t)primary, (uint8_t)guard);
      assert(canClear == (guard == 0 && (primary <= 15 || primary == 17)));
    }
  }
  // Primary0/guard15 can be an interrupted primary clear, never permission to
  // clear the synthesized RAM fault. Even primary15 requires diagnostic recovery.
  auto interrupted = pwDecodeStoredFault(pwRestoreFaultWithRecoveryGuard(0, 15, 15), true, 15, 16);
  assert(interrupted.raw == 15 && interrupted.code == 15 && interrupted.locked);
  assert(!pwMayClearStoredFault(interrupted, 0, 15));
  assert(!pwMayClearStoredFault(interrupted, 15, 15));
  assert(!pwMayClearStoredFault(interrupted, 0, 0));
  assert(!pwMayClearStoredFault(diagnostic, 15, 255));
  auto overspeed = pwDecodeStoredFault(17, true, 17, 16);
  assert(overspeed.code == 17 && overspeed.locked && !overspeed.unknown);
  assert(pwMayClearStoredFault(overspeed, 17));
  auto reservedUnknown = pwDecodeStoredFault(16, true, 17, 16);
  assert(reservedUnknown.unknown && !pwMayClearStoredFault(reservedUnknown, 16));
}

int main() {
  testFrictionPersistence();
  testFitLifecycle();
  testFaultCompatibility();
  std::puts("production policy tests passed: migration/reboot, fit lifecycle, all 256 fault bytes and 65536 recovery journal pairs");
  return 0;
}
