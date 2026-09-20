#pragma once
#include <esp_timer.h>
#include <driver/gpio.h>

// Loop-independent ESP task timer, not a hard hardware cutoff. The callback
// never touches SPI, NVS, Serial or FastAccelStepper. An expired/aborted lease
// can only remove torque; it cannot enable or replenish a capture attempt.
class PwCaptureLease {
 public:
  static constexpr uint64_t ARM_US = 150000;
  bool begin(gpio_num_t pin) {
    pin_ = pin;
    esp_timer_create_args_t args = {};
    args.callback = &PwCaptureLease::timerCallback;
    args.arg = this;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "capture_lease";
    ready_ = esp_timer_create(&args, &timer_) == ESP_OK &&
             esp_timer_start_periodic(timer_, 1000) == ESP_OK;
    return ready_;
  }
  bool arm() {
    portENTER_CRITICAL(&mux_);
    bool ok = ready_ && !active_;
    if (ok) { active_ = true; expired_ = false; deadline_ = esp_timer_get_time() + ARM_US; }
    portEXIT_CRITICAL(&mux_);
    return ok;
  }
  void cancel() {
    portENTER_CRITICAL(&mux_);
    active_ = false; expired_ = true;
    gpio_set_level(pin_, 1);
    portEXIT_CRITICAL(&mux_);
  }
  bool expired() const { return expired_; }
  // The callback is GPIO-only in the pinned FAS version with ordinary EN GPIO.
  // Proof and prerequisites are rechecked by the caller immediately beforehand.
  bool enable(bool validProof, uint32_t proofUs, bool (*enableOutput)()) {
    portENTER_CRITICAL(&mux_);
    const uint64_t now = esp_timer_get_time();
    const bool allowed = active_ && !expired_ && now < deadline_ && validProof &&
                         uint32_t(uint32_t(now) - proofUs) <= 2000;
    bool ok = allowed && enableOutput();
    active_ = false; // one EN edge per lease, even if the output call failed
    expired_ = !ok;
    if (!ok) gpio_set_level(pin_, 1);
    portEXIT_CRITICAL(&mux_);
    return ok;
  }
 private:
  static void timerCallback(void* arg) {
    static_cast<PwCaptureLease*>(arg)->checkDeadline();
  }
  void checkDeadline() {
    const uint64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&mux_);
    if (active_ && now >= deadline_) {
      expired_ = true;
      gpio_set_level(pin_, 1);
    }
    portEXIT_CRITICAL(&mux_);
  }
  gpio_num_t pin_ = (gpio_num_t)7;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  esp_timer_handle_t timer_ = nullptr;
  bool ready_ = false;
  volatile bool active_ = false, expired_ = true;
  uint64_t deadline_ = 0;
};
