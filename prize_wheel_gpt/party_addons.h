#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

// Party feature switches. The three sanctioned control fixes live in the
// main sketch; these switches cover only the fail-silent WiFi/FX consumers.
#ifndef PARTY_WIFI_ENABLED
#define PARTY_WIFI_ENABLED 1
#endif
#ifndef PARTY_WIFI_PASSWORD
#define PARTY_WIFI_PASSWORD "change-me-2479"
#endif
#ifndef PARTY_WIFI_CHANNEL
#define PARTY_WIFI_CHANNEL 6
#endif
#ifndef PARTY_WIFI_HIDDEN
#define PARTY_WIFI_HIDDEN 0
#endif
#ifndef PARTY_TELNET_PORT
#define PARTY_TELNET_PORT 23
#endif
#ifndef PARTY_TELNET_MAX_CLIENTS
#define PARTY_TELNET_MAX_CLIENTS 2
#endif

#ifndef PARTY_FX_ENABLED
#define PARTY_FX_ENABLED 1
#endif
#ifndef PARTY_AUDIO_ENABLED
#define PARTY_AUDIO_ENABLED 1
#endif
// GPIO16/17 are already the TMC2209 UART. The DFPlayer therefore uses the
// other hardware UART on isolated pins; DFPlayer TX is optional/unread.
#ifndef PARTY_DFPLAYER_TX_PIN
#define PARTY_DFPLAYER_TX_PIN 32
#endif
#ifndef PARTY_DFPLAYER_RX_PIN
#define PARTY_DFPLAYER_RX_PIN 33
#endif
#ifndef PARTY_DFPLAYER_VOLUME
#define PARTY_DFPLAYER_VOLUME 20
#endif
#ifndef PARTY_DFPLAYER_MIN_COMMAND_MS
#define PARTY_DFPLAYER_MIN_COMMAND_MS 120
#endif

#ifndef PARTY_LED_ENABLED
#define PARTY_LED_ENABLED 1
#endif
#ifndef PARTY_LED_DATA_PIN
#define PARTY_LED_DATA_PIN 13
#endif
#ifndef PARTY_NUM_LEDS
#define PARTY_NUM_LEDS 36
#endif
#ifndef PARTY_LED_BRIGHTNESS
#define PARTY_LED_BRIGHTNESS 72
#endif
#ifndef PARTY_LED_ZERO_INDEX
#define PARTY_LED_ZERO_INDEX 0
#endif
#ifndef PARTY_LED_DIRECTION_SIGN
#define PARTY_LED_DIRECTION_SIGN 1
#endif

#ifndef PARTY_LOOP_BUDGET_US
#define PARTY_LOOP_BUDGET_US 2000
#endif

class PartyLog : public Print {
 public:
  explicit PartyLog(HardwareSerial& serial) : serial_(serial) {}

  void begin(unsigned long baud) { serial_.begin(baud); }
  int available() { return serial_.available(); }
  int read() { return serial_.read(); }
  void flush() { serial_.flush(); }
  operator bool() const { return true; }

  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  using Print::write;

  uint32_t headSequence() const { return head_sequence_; }
  uint32_t tailSequence() const;
  size_t copyFromSequence(uint32_t sequence, uint8_t* destination,
                          size_t maximum) const;
  uint32_t takeMirrorTimeUs();

 private:
  static const size_t LOG_CAPACITY = 4096;
  HardwareSerial& serial_;
  uint8_t ring_[LOG_CAPACITY] = {0};
  uint32_t head_sequence_ = 0;
  uint32_t mirror_time_us_ = 0;
};

enum PartyPhase : uint8_t {
  PARTY_PHASE_IDLE = 0,
  PARTY_PHASE_PUSH,
  PARTY_PHASE_COAST,
  PARTY_PHASE_CAPTURE,
  PARTY_PHASE_DECEL,
  PARTY_PHASE_LANDED,
  PARTY_PHASE_FAULT
};

enum PartyCloseKind : uint8_t {
  PARTY_CLOSE_NONE = 0,
  PARTY_CLOSE_SAFE,
  PARTY_CLOSE_GUEST_STOPPED,
  PARTY_CLOSE_OTHER
};

struct PartySnapshot {
  PartyPhase phase;
  int8_t wedge;
  int8_t direction;
  int8_t target_wedge;
  float angle_deg;
  float speed_rev_s;
  float remaining_deg;
  uint32_t spin_number;
  bool spin_open;
  PartyCloseKind close_kind;
};

typedef void (*PartyCommandSink)(char value);

class PartyAddons {
 public:
  PartyAddons();

  void begin(Preferences& preferences, PartyCommandSink command_sink,
             PartyLog& log);
  void service(const PartySnapshot& snapshot);

  void setVolume(int volume);
  void nudgeLedZero(int delta);
  void toggleLedDirection();
  void printStatus(Print& out);
  void resetTiming();

 private:
  enum FxEventType : uint8_t {
    FX_BOOT = 0,
    FX_IDLE_ENTER,
    FX_SPIN_CONFIRMED,
    FX_WEDGE_CROSS,
    FX_RELEASE_DETECTED,
    FX_CAPTURE_START,
    FX_DECEL_PHASE,
    FX_LANDED_SAFE,
    FX_GUEST_STOPPED,
    FX_FAULT
  };

  struct FxEvent {
    FxEventType type;
    int8_t wedge;
    int8_t direction;
    uint32_t at_ms;
  };

  struct AudioCommand {
    uint8_t command;
    uint16_t parameter;
  };

  enum AudioMode : uint8_t {
    AUDIO_SILENT = 0,
    AUDIO_RATCHET,
    AUDIO_TICKS,
    AUDIO_FANFARE,
    AUDIO_GUEST
  };

  static const uint8_t EVENT_CAPACITY = 16;
  static const uint8_t AUDIO_CAPACITY = 12;
  static const uint16_t WIFI_SEND_LIMIT = 256;
  static const uint16_t WIFI_READ_LIMIT = 32;
  static const uint16_t WIFI_CATCHUP_BYTES = 2048;

  void observe(const PartySnapshot& snapshot, uint32_t now_ms);
  void pushEvent(FxEventType type, int wedge, int direction, uint32_t now_ms);
  bool popEvent(FxEvent& event);
  void serviceFx(uint32_t now_ms);
  void handleFxEvent(const FxEvent& event, uint32_t now_ms);

  void queueAudio(uint8_t command, uint16_t parameter);
  bool popAudio(AudioCommand& command);
  void requestAudioMode(AudioMode mode);
  void serviceAudio(uint32_t now_ms);
  void sendAudioFrame(const AudioCommand& command);

  void serviceLeds(uint32_t now_ms);
  int ledForAngle(float angle_deg) const;
  int wrappedLed(int index) const;
  void renderIdle(uint32_t now_ms);
  void renderSpin(uint32_t now_ms);
  void renderDecel(uint32_t now_ms);
  void renderLanded(uint32_t now_ms);
  void renderFault();

  void beginWifi();
  void serviceWifi(uint32_t now_ms);
  void acceptWifiClient();
  void serviceWifiClient(uint8_t index);
  uint8_t connectedClientCount();

  Preferences* preferences_ = nullptr;
  PartyCommandSink command_sink_ = nullptr;
  PartyLog* log_ = nullptr;

  HardwareSerial df_serial_;
  WiFiServer wifi_server_;
  WiFiClient wifi_clients_[PARTY_TELNET_MAX_CLIENTS];
  uint32_t wifi_cursor_[PARTY_TELNET_MAX_CLIENTS] = {0};
  uint16_t wifi_banner_offset_[PARTY_TELNET_MAX_CLIENTS] = {0};
  bool wifi_overflow_notice_[PARTY_TELNET_MAX_CLIENTS] = {false};
  bool wifi_ok_ = false;
  char wifi_ssid_[16] = {0};
  uint32_t last_net_heartbeat_ms_ = 0;

  Adafruit_NeoPixel leds_;
  bool leds_ok_ = false;
  int16_t led_zero_index_ = PARTY_LED_ZERO_INDEX;
  int8_t led_direction_sign_ = PARTY_LED_DIRECTION_SIGN;
  uint32_t last_led_frame_ms_ = 0;
  uint16_t led_frame_interval_ms_ = 17;

  FxEvent events_[EVENT_CAPACITY];
  uint8_t event_head_ = 0;
  uint8_t event_tail_ = 0;
  uint8_t event_count_ = 0;

  AudioCommand audio_commands_[AUDIO_CAPACITY];
  uint8_t audio_head_ = 0;
  uint8_t audio_tail_ = 0;
  uint8_t audio_count_ = 0;
  bool audio_ok_ = false;
  AudioMode audio_mode_ = AUDIO_SILENT;
  uint8_t audio_volume_ = PARTY_DFPLAYER_VOLUME;
  uint32_t last_audio_command_ms_ = 0;
  uint32_t fanfare_due_ms_ = 0;
  uint32_t landed_until_ms_ = 0;

  PartySnapshot latest_ = {};
  bool observed_ = false;
  int8_t last_wedge_ = -1;
  PartyPhase last_phase_ = PARTY_PHASE_IDLE;
  uint32_t last_closed_spin_ = 0;

  uint32_t max_service_us_ = 0;
  uint32_t over_budget_count_ = 0;
};
