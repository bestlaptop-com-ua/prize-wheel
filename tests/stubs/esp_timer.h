#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr int ESP_TIMER_TASK = 0;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
struct esp_timer_create_args_t {
    void (*callback)(void*) = nullptr;
    void* arg = nullptr;
    int dispatch_method = ESP_TIMER_TASK;
    const char* name = nullptr;
};
struct FakeEspTimer { esp_timer_create_args_t args; std::uint64_t period = 0; };
using esp_timer_handle_t = FakeEspTimer*;
extern std::uint64_t fakeNowUs;
extern FakeEspTimer fakeTimer;
inline std::int64_t esp_timer_get_time() { return static_cast<std::int64_t>(fakeNowUs); }
inline esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out) {
    fakeTimer.args = *args; *out = &fakeTimer; return ESP_OK;
}
inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, std::uint64_t period) {
    timer->period = period; return ESP_OK;
}
