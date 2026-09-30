#pragma once
#include <stdint.h>

static constexpr const char* PW_RECOVERY_GUARD_KEY = "recovGuard";

struct PwStoredFault {
  uint8_t raw;
  uint8_t code;
  bool locked;
  bool unknown;
};

inline PwStoredFault pwDecodeStoredFault(uint8_t raw, bool storageAvailable,
                                        uint8_t lastKnown, uint8_t unknownCode) {
  if (!storageAvailable) return {255, unknownCode, true, true};
  if (raw == 0) return {0, 0, false, false};
  // The diagnostic compatibility sentinel is never an ordinary saved fault,
  // even when a later known ID has been appended beyond it.
  bool unknown = raw > lastKnown || raw == unknownCode;
  return {raw, unknown ? unknownCode : raw, true, unknown};
}

// Diagnostic recovery journals its clear. A nonzero guard must lock even when
// the primary clear committed before an interrupted recovery/reboot. Preserve
// unknown primary IDs exactly; an unfamiliar guard is itself an unknown fault.
inline uint8_t pwRestoreFaultWithRecoveryGuard(uint8_t primary, uint8_t guard,
                                              uint8_t selfspin) {
  if (primary > selfspin || guard == 0) return primary;
  if (guard != selfspin) return 255;
  return primary != 0 ? primary : selfspin;
}

// A new fault must never replace the first saved fault, including a newer ID.
inline uint8_t pwFirstFaultByte(uint8_t stored, uint8_t requested) {
  return stored != 0 ? stored : requested;
}

inline bool pwMayClearStoredFault(const PwStoredFault& fault, uint8_t reread,
                                 uint8_t recoveryGuard = 0) {
  // Production does not implement the diagnostic journal recovery procedure.
  // Even an otherwise-known primary must not bypass an unfinished recovery.
  return recoveryGuard == 0 && !fault.unknown && fault.raw == reread;
}
