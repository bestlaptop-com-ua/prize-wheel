#pragma once
#include <TMCStepper.h>

#if TMCSTEPPER_VERSION != 0x000703
#error "Restricted GSTAT acknowledgement must be re-audited for this TMCStepper version"
#endif

class PwTmc5160 : public TMC5160Stepper {
 public:
  using TMC5160Stepper::TMC5160Stepper;
  bool acknowledgeStartupFlags(uint8_t observed) {
    if (observed != 0x01 && observed != 0x05) return false;
    // TMCStepper 0.7.3 GSTAT(uint8_t) ignores its argument and writes 0b111.
    // Bypass only that setter via the inherited SPI write implementation.
    // Never acknowledge drv_err (bit1), even if it races the preceding read.
    TMC2130Stepper::write(0x01, uint32_t(observed));
    return true;
  }
};
