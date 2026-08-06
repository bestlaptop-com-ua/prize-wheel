#include "party_addons.h"

#include <esp_mac.h>
#include <esp_system.h>

#include <math.h>
#include <string.h>

namespace {
const char kClientBanner[] =
    "\r\n# Prize Wheel telnet mirror (recent context follows)\r\n";
const char kOverflowBanner[] =
    "\r\n# net: client backlog overflow; oldest bytes dropped\r\n";

const uint8_t DF_CMD_VOLUME = 0x06;
const uint8_t DF_CMD_PLAY_MP3 = 0x12;
const uint8_t DF_CMD_STOP = 0x16;
const uint8_t DF_CMD_LOOP_CURRENT = 0x19;
const uint16_t DF_LOOP_ENABLE = 0;
const uint16_t DF_LOOP_DISABLE = 1;
}  // namespace

size_t PartyLog::write(uint8_t value) {
  size_t written = serial_.write(value);
  uint32_t started_us = micros();
  ring_[head_sequence_ % LOG_CAPACITY] = value;
  ++head_sequence_;
  mirror_time_us_ += micros() - started_us;
  return written;
}

size_t PartyLog::write(const uint8_t* buffer, size_t size) {
  size_t written = serial_.write(buffer, size);
  uint32_t started_us = micros();
  for (size_t i = 0; i < size; ++i) {
    ring_[head_sequence_ % LOG_CAPACITY] = buffer[i];
    ++head_sequence_;
  }
  mirror_time_us_ += micros() - started_us;
  return written;
}

uint32_t PartyLog::takeMirrorTimeUs() {
  uint32_t elapsed = mirror_time_us_;
  mirror_time_us_ = 0;
  return elapsed;
}

uint32_t PartyLog::tailSequence() const {
  return head_sequence_ > LOG_CAPACITY ? head_sequence_ - LOG_CAPACITY : 0;
}

size_t PartyLog::copyFromSequence(uint32_t sequence, uint8_t* destination,
                                  size_t maximum) const {
  uint32_t tail = tailSequence();
  if (sequence < tail) sequence = tail;
  uint32_t available = head_sequence_ - sequence;
  size_t count = available < maximum ? (size_t)available : maximum;
  for (size_t i = 0; i < count; ++i) {
    destination[i] = ring_[(sequence + i) % LOG_CAPACITY];
  }
  return count;
}

PartyAddons::PartyAddons()
    : df_serial_(1),
      wifi_server_(PARTY_TELNET_PORT, PARTY_TELNET_MAX_CLIENTS),
      leds_(PARTY_NUM_LEDS, PARTY_LED_DATA_PIN, NEO_GRB + NEO_KHZ800) {}

void PartyAddons::begin(Preferences& preferences,
                        PartyCommandSink command_sink, PartyLog& log) {
  preferences_ = &preferences;
  command_sink_ = command_sink;
  log_ = &log;

  if (preferences_) {
    led_zero_index_ = preferences_->getShort("ledZero", PARTY_LED_ZERO_INDEX);
    led_direction_sign_ =
        preferences_->getChar("ledSign", PARTY_LED_DIRECTION_SIGN);
  }
  if (led_direction_sign_ != 1 && led_direction_sign_ != -1) {
    led_direction_sign_ = PARTY_LED_DIRECTION_SIGN >= 0 ? 1 : -1;
  }
  led_zero_index_ = wrappedLed(led_zero_index_);

#if PARTY_AUDIO_ENABLED
  df_serial_.begin(9600, SERIAL_8N1, PARTY_DFPLAYER_RX_PIN,
                   PARTY_DFPLAYER_TX_PIN);
  audio_ok_ = true;  // fire-and-forget: absence is intentionally not a fault
  queueAudio(DF_CMD_VOLUME, audio_volume_);
#endif

#if PARTY_LED_ENABLED
  leds_.begin();
  leds_ok_ = leds_.getPixels() != nullptr;
  if (leds_ok_) {
    leds_.setBrightness(PARTY_LED_BRIGHTNESS);
    leds_.clear();
    leds_.show();
  } else if (log_) {
    log_->println(F("# fx: LED buffer allocation failed; LEDs disabled"));
  }
  // A 36-pixel WS2812B frame is about 1.1 ms. Long strips get half the frame
  // rate first; the measured command still exposes the true worst pass.
  uint32_t estimated_show_us = 100U + 30U * PARTY_NUM_LEDS;
  led_frame_interval_ms_ = estimated_show_us > 1500U ? 33 : 17;
#endif

#if PARTY_WIFI_ENABLED
  beginWifi();
#endif

  esp_reset_reason_t reason = esp_reset_reason();
  log_->printf("# party addons: wifi=%d audio=%d leds=%d reset=%d brownout=%d\n",
               wifi_ok_ ? 1 : 0, audio_ok_ ? 1 : 0, leds_ok_ ? 1 : 0,
               (int)reason, reason == ESP_RST_BROWNOUT ? 1 : 0);
  pushEvent(FX_BOOT, -1, 0, millis());
  // Setup output is outside the loop budget. Begin the tracker at loop zero.
  log_->takeMirrorTimeUs();
}

void PartyAddons::beginWifi() {
  uint8_t mac[6] = {0};
  if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
    if (log_) log_->println(F("# net: MAC read failed; WiFi disabled"));
    return;
  }
  snprintf(wifi_ssid_, sizeof(wifi_ssid_), "PW-%02X%02X", mac[4], mac[5]);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP);
  wifi_ok_ = WiFi.softAP(wifi_ssid_, PARTY_WIFI_PASSWORD, PARTY_WIFI_CHANNEL,
                         PARTY_WIFI_HIDDEN != 0, PARTY_TELNET_MAX_CLIENTS);
  if (!wifi_ok_) {
    if (log_) log_->println(F("# net: SoftAP start failed; continuing offline"));
    return;
  }
  wifi_server_.begin();
  wifi_server_.setNoDelay(true);
  if (log_) {
    log_->printf("# net: SoftAP ssid=%s channel=%d hidden=%d telnet=%d max=%d\n",
                 wifi_ssid_, PARTY_WIFI_CHANNEL, PARTY_WIFI_HIDDEN,
                 PARTY_TELNET_PORT, PARTY_TELNET_MAX_CLIENTS);
  }
}

void PartyAddons::service(const PartySnapshot& snapshot) {
  // Output mirroring happens at existing log sites earlier in loop(). Count
  // only the extra ring-copy time here; baseline USB write time is not WiFi.
  uint32_t mirror_us = log_ ? log_->takeMirrorTimeUs() : 0;
  uint32_t started_us = micros();
  uint32_t now_ms = millis();
  latest_ = snapshot;
  observe(snapshot, now_ms);
#if PARTY_FX_ENABLED
  serviceFx(now_ms);
#endif
#if PARTY_WIFI_ENABLED
  if (wifi_ok_) serviceWifi(now_ms);
#endif
  uint32_t elapsed_us = micros() - started_us;
  // Clear mirror timing generated inside service (for example heartbeat): it
  // is already included in elapsed_us and must not be counted again next pass.
  if (log_) log_->takeMirrorTimeUs();
  elapsed_us += mirror_us;
  if (elapsed_us > max_service_us_) max_service_us_ = elapsed_us;
  if (elapsed_us > PARTY_LOOP_BUDGET_US) ++over_budget_count_;
}

void PartyAddons::observe(const PartySnapshot& snapshot, uint32_t now_ms) {
  if (!observed_) {
    observed_ = true;
    last_phase_ = snapshot.phase;
    last_wedge_ = snapshot.wedge;
    if (snapshot.phase == PARTY_PHASE_IDLE) {
      pushEvent(FX_IDLE_ENTER, snapshot.wedge, snapshot.direction, now_ms);
    }
  }

  if (snapshot.phase != last_phase_) {
    switch (snapshot.phase) {
      case PARTY_PHASE_IDLE:
        pushEvent(FX_IDLE_ENTER, snapshot.wedge, snapshot.direction, now_ms);
        break;
      case PARTY_PHASE_PUSH:
        pushEvent(FX_SPIN_CONFIRMED, snapshot.wedge, snapshot.direction, now_ms);
        break;
      case PARTY_PHASE_COAST:
        if (last_phase_ == PARTY_PHASE_PUSH) {
          pushEvent(FX_RELEASE_DETECTED, snapshot.wedge, snapshot.direction,
                    now_ms);
        }
        break;
      case PARTY_PHASE_CAPTURE:
        pushEvent(FX_CAPTURE_START, snapshot.wedge, snapshot.direction, now_ms);
        break;
      case PARTY_PHASE_DECEL:
        pushEvent(FX_DECEL_PHASE, snapshot.wedge, snapshot.direction, now_ms);
        break;
      case PARTY_PHASE_LANDED:
        pushEvent(FX_LANDED_SAFE, snapshot.wedge, snapshot.direction, now_ms);
        break;
      case PARTY_PHASE_FAULT:
        pushEvent(FX_FAULT, snapshot.wedge, snapshot.direction, now_ms);
        break;
    }
    last_phase_ = snapshot.phase;
  }

  bool moving_phase = snapshot.phase >= PARTY_PHASE_PUSH &&
                      snapshot.phase <= PARTY_PHASE_DECEL;
  if (moving_phase && snapshot.wedge != last_wedge_) {
    pushEvent(FX_WEDGE_CROSS, snapshot.wedge, snapshot.direction, now_ms);
  }
  last_wedge_ = snapshot.wedge;

  if (!snapshot.spin_open && snapshot.spin_number != 0 &&
      snapshot.spin_number != last_closed_spin_) {
    last_closed_spin_ = snapshot.spin_number;
    if (snapshot.close_kind == PARTY_CLOSE_GUEST_STOPPED) {
      pushEvent(FX_GUEST_STOPPED, snapshot.wedge, snapshot.direction, now_ms);
    }
  }
}

void PartyAddons::pushEvent(FxEventType type, int wedge, int direction,
                            uint32_t now_ms) {
  if (event_count_ == EVENT_CAPACITY) {
    event_tail_ = (event_tail_ + 1) % EVENT_CAPACITY;
    --event_count_;
  }
  FxEvent& event = events_[event_head_];
  event.type = type;
  event.wedge = (int8_t)wedge;
  event.direction = (int8_t)direction;
  event.at_ms = now_ms;
  event_head_ = (event_head_ + 1) % EVENT_CAPACITY;
  ++event_count_;
}

bool PartyAddons::popEvent(FxEvent& event) {
  if (event_count_ == 0) return false;
  event = events_[event_tail_];
  event_tail_ = (event_tail_ + 1) % EVENT_CAPACITY;
  --event_count_;
  return true;
}

void PartyAddons::serviceFx(uint32_t now_ms) {
  FxEvent event;
  while (popEvent(event)) handleFxEvent(event, now_ms);
  serviceAudio(now_ms);
  serviceLeds(now_ms);
}

void PartyAddons::handleFxEvent(const FxEvent& event, uint32_t now_ms) {
  switch (event.type) {
    case FX_BOOT:
      break;
    case FX_IDLE_ENTER:
      audio_head_ = audio_tail_ = audio_count_ = 0;
      queueAudio(DF_CMD_STOP, 0);
      queueAudio(DF_CMD_VOLUME, audio_volume_);
      audio_mode_ = AUDIO_SILENT;
      break;
    case FX_SPIN_CONFIRMED:
      // The crossover service starts ratchet or ticks from measured speed.
      audio_mode_ = AUDIO_SILENT;
      break;
    case FX_WEDGE_CROSS:
      if (latest_.speed_rev_s < 0.55f && audio_mode_ != AUDIO_RATCHET) {
        requestAudioMode(AUDIO_TICKS);
      }
      break;
    case FX_RELEASE_DETECTED:
    case FX_CAPTURE_START:
      // Deliberately no audible transition: capture must not become a tell.
      break;
    case FX_DECEL_PHASE:
      break;
    case FX_LANDED_SAFE:
      requestAudioMode(AUDIO_SILENT);
      fanfare_due_ms_ = now_ms + 400;
      landed_until_ms_ = now_ms + 3000;
      break;
    case FX_GUEST_STOPPED:
      requestAudioMode(AUDIO_GUEST);
      break;
    case FX_FAULT:
      fanfare_due_ms_ = 0;
      audio_head_ = audio_tail_ = audio_count_ = 0;
      queueAudio(DF_CMD_STOP, 0);
      audio_mode_ = AUDIO_SILENT;
      break;
  }
}

void PartyAddons::queueAudio(uint8_t command, uint16_t parameter) {
  if (!audio_ok_ && command != DF_CMD_VOLUME) return;
  if (audio_count_ == AUDIO_CAPACITY) {
    audio_tail_ = (audio_tail_ + 1) % AUDIO_CAPACITY;
    --audio_count_;
  }
  audio_commands_[audio_head_].command = command;
  audio_commands_[audio_head_].parameter = parameter;
  audio_head_ = (audio_head_ + 1) % AUDIO_CAPACITY;
  ++audio_count_;
}

bool PartyAddons::popAudio(AudioCommand& command) {
  if (audio_count_ == 0) return false;
  command = audio_commands_[audio_tail_];
  audio_tail_ = (audio_tail_ + 1) % AUDIO_CAPACITY;
  --audio_count_;
  return true;
}

void PartyAddons::requestAudioMode(AudioMode mode) {
  if (!audio_ok_) return;
  if (mode == AUDIO_TICKS) {
    // A wedge crossing is an edge-trigger, so each request plays one tick.
    if (audio_mode_ == AUDIO_RATCHET) {
      queueAudio(DF_CMD_LOOP_CURRENT, DF_LOOP_DISABLE);
    }
    queueAudio(DF_CMD_PLAY_MP3, 1);
    audio_mode_ = AUDIO_TICKS;
    return;
  }
  if (mode == audio_mode_) return;
  switch (mode) {
    case AUDIO_SILENT:
      queueAudio(DF_CMD_STOP, 0);
      break;
    case AUDIO_RATCHET:
      queueAudio(DF_CMD_PLAY_MP3, 2);
      queueAudio(DF_CMD_LOOP_CURRENT, DF_LOOP_ENABLE);
      break;
    case AUDIO_FANFARE:
      queueAudio(DF_CMD_LOOP_CURRENT, DF_LOOP_DISABLE);
      queueAudio(DF_CMD_PLAY_MP3, 4);
      break;
    case AUDIO_GUEST:
      queueAudio(DF_CMD_LOOP_CURRENT, DF_LOOP_DISABLE);
      queueAudio(DF_CMD_PLAY_MP3, 6);
      break;
    case AUDIO_TICKS:
      break;
  }
  audio_mode_ = mode;
}

void PartyAddons::serviceAudio(uint32_t now_ms) {
#if PARTY_AUDIO_ENABLED
  bool spin_audio = latest_.phase >= PARTY_PHASE_PUSH &&
                    latest_.phase <= PARTY_PHASE_DECEL;
  if (spin_audio) {
    if (latest_.speed_rev_s >= 0.55f && audio_mode_ != AUDIO_RATCHET) {
      requestAudioMode(AUDIO_RATCHET);
    } else if (latest_.speed_rev_s <= 0.45f && audio_mode_ == AUDIO_RATCHET) {
      queueAudio(DF_CMD_LOOP_CURRENT, DF_LOOP_DISABLE);
      audio_mode_ = AUDIO_TICKS;
    }
  }
  if (fanfare_due_ms_ != 0 && (int32_t)(now_ms - fanfare_due_ms_) >= 0) {
    fanfare_due_ms_ = 0;
    requestAudioMode(AUDIO_FANFARE);
  }
  if (audio_count_ == 0 ||
      now_ms - last_audio_command_ms_ < PARTY_DFPLAYER_MIN_COMMAND_MS) {
    return;
  }
  AudioCommand command;
  if (popAudio(command)) {
    sendAudioFrame(command);
    last_audio_command_ms_ = now_ms;
  }
#else
  (void)now_ms;
#endif
}

void PartyAddons::sendAudioFrame(const AudioCommand& command) {
  uint8_t high = (uint8_t)(command.parameter >> 8);
  uint8_t low = (uint8_t)command.parameter;
  uint16_t checksum = (uint16_t)(0U -
      (0xFFU + 0x06U + command.command + high + low));
  uint8_t frame[10] = {0x7E, 0xFF, 0x06, command.command, 0x00,
                       high, low, (uint8_t)(checksum >> 8),
                       (uint8_t)checksum, 0xEF};
  df_serial_.write(frame, sizeof(frame));
}

void PartyAddons::setVolume(int volume) {
  if (volume < 0) volume = 0;
  if (volume > 30) volume = 30;
  audio_volume_ = (uint8_t)volume;
  queueAudio(DF_CMD_VOLUME, audio_volume_);
  if (log_) log_->printf("# fx: DFPlayer volume=%u/30\n", audio_volume_);
}

int PartyAddons::wrappedLed(int index) const {
  if (PARTY_NUM_LEDS <= 0) return 0;
  index %= PARTY_NUM_LEDS;
  if (index < 0) index += PARTY_NUM_LEDS;
  return index;
}

int PartyAddons::ledForAngle(float angle_deg) const {
  while (angle_deg < 0.0f) angle_deg += 360.0f;
  while (angle_deg >= 360.0f) angle_deg -= 360.0f;
  int offset = (int)lroundf(angle_deg * PARTY_NUM_LEDS / 360.0f);
  return wrappedLed(led_zero_index_ + led_direction_sign_ * offset);
}

void PartyAddons::nudgeLedZero(int delta) {
  led_zero_index_ = wrappedLed(led_zero_index_ + delta);
  if (preferences_) preferences_->putShort("ledZero", led_zero_index_);
  if (log_) log_->printf("# fx: LED zero index=%d persisted\n", led_zero_index_);
}

void PartyAddons::toggleLedDirection() {
  led_direction_sign_ = (int8_t)-led_direction_sign_;
  if (preferences_) preferences_->putChar("ledSign", led_direction_sign_);
  if (log_) log_->printf("# fx: LED direction sign=%+d persisted\n",
                         led_direction_sign_);
}

void PartyAddons::serviceLeds(uint32_t now_ms) {
#if PARTY_LED_ENABLED
  if (!leds_ok_ || now_ms - last_led_frame_ms_ < led_frame_interval_ms_) return;
  last_led_frame_ms_ = now_ms;
  leds_.clear();
  switch (latest_.phase) {
    case PARTY_PHASE_IDLE: renderIdle(now_ms); break;
    case PARTY_PHASE_PUSH:
    case PARTY_PHASE_COAST:
    case PARTY_PHASE_CAPTURE: renderSpin(now_ms); break;
    case PARTY_PHASE_DECEL: renderDecel(now_ms); break;
    case PARTY_PHASE_LANDED: renderLanded(now_ms); break;
    case PARTY_PHASE_FAULT: renderFault(); break;
  }
  leds_.show();
#else
  (void)now_ms;
#endif
}

void PartyAddons::renderIdle(uint32_t now_ms) {
  float wave = 0.5f + 0.5f * sinf((float)now_ms * TWO_PI / 4000.0f);
  uint8_t value = (uint8_t)lroundf(10.0f + 28.0f * wave);
  for (int i = 0; i < PARTY_NUM_LEDS; ++i) {
    leds_.setPixelColor(i, leds_.Color(value / 8, value / 3, value));
  }
}

void PartyAddons::renderSpin(uint32_t now_ms) {
  (void)now_ms;
  int head = ledForAngle(latest_.angle_deg);
  int direction = latest_.direction >= 0 ? 1 : -1;
  for (int trail = 0; trail < 7; ++trail) {
    int index = wrappedLed(head - direction * trail);
    uint8_t value = (uint8_t)(255 / (trail + 1));
    leds_.setPixelColor(index, leds_.Color(value, value / 5, value / 32));
  }
}

void PartyAddons::renderDecel(uint32_t now_ms) {
  (void)now_ms;
  float target_angle = latest_.target_wedge >= 0
      ? (latest_.target_wedge + 0.5f) * 30.0f
      : latest_.angle_deg;
  int center = ledForAngle(target_angle);
  int segment = max(1, PARTY_NUM_LEDS / 12);
  int span = (int)lroundf(fminf(180.0f, fmaxf(30.0f, latest_.remaining_deg)) *
                          PARTY_NUM_LEDS / 360.0f);
  if (span < segment) span = segment;
  for (int offset = -span; offset <= span; ++offset) {
    uint8_t value = (uint8_t)max(8, 180 - abs(offset) * 16);
    leds_.setPixelColor(wrappedLed(center + offset),
                        leds_.Color(value / 10, value, value / 5));
  }
  int wheel_head = ledForAngle(latest_.angle_deg);
  leds_.setPixelColor(wheel_head, leds_.Color(255, 255, 255));
}

void PartyAddons::renderLanded(uint32_t now_ms) {
  int winner = latest_.target_wedge >= 0 ? latest_.target_wedge : latest_.wedge;
  float center_angle = (winner + 0.5f) * 30.0f;
  int center = ledForAngle(center_angle);
  int half = max(1, PARTY_NUM_LEDS / 24);
  bool bright = ((now_ms / 150U) & 1U) == 0 || now_ms >= landed_until_ms_;
  for (int offset = -half; offset <= half; ++offset) {
    leds_.setPixelColor(wrappedLed(center + offset),
                        bright ? leds_.Color(255, 180, 24)
                               : leds_.Color(45, 20, 2));
  }
}

void PartyAddons::renderFault() {
  for (int i = 0; i < PARTY_NUM_LEDS; ++i) {
    leds_.setPixelColor(i, leds_.Color(12, 3, 18));
  }
}

void PartyAddons::acceptWifiClient() {
  WiFiClient incoming = wifi_server_.accept();
  if (!incoming) return;
  for (uint8_t i = 0; i < PARTY_TELNET_MAX_CLIENTS; ++i) {
    if (!wifi_clients_[i] || !wifi_clients_[i].connected()) {
      if (wifi_clients_[i]) wifi_clients_[i].stop();
      wifi_clients_[i] = incoming;
      wifi_clients_[i].setNoDelay(true);
      uint32_t head = log_ ? log_->headSequence() : 0;
      uint32_t tail = log_ ? log_->tailSequence() : 0;
      wifi_cursor_[i] = head > WIFI_CATCHUP_BYTES
                            ? head - WIFI_CATCHUP_BYTES : 0;
      if (wifi_cursor_[i] < tail) wifi_cursor_[i] = tail;
      wifi_banner_offset_[i] = 0;
      wifi_overflow_notice_[i] = false;
      return;
    }
  }
  incoming.stop();
}

void PartyAddons::serviceWifiClient(uint8_t index) {
  WiFiClient& client = wifi_clients_[index];
  if (!client || !client.connected()) {
    if (client) client.stop();
    return;
  }

  uint16_t read_count = 0;
  while (client.available() > 0 && read_count < WIFI_READ_LIMIT) {
    int value = client.read();
    if (value < 0) break;
    if (command_sink_) command_sink_((char)value);
    ++read_count;
  }

  int writable = client.availableForWrite();
  if (writable <= 0) return;
  uint16_t send_budget = writable < WIFI_SEND_LIMIT
      ? (uint16_t)writable : WIFI_SEND_LIMIT;

  if (wifi_banner_offset_[index] < sizeof(kClientBanner) - 1) {
    size_t remaining = sizeof(kClientBanner) - 1 - wifi_banner_offset_[index];
    size_t count = remaining < send_budget ? remaining : send_budget;
    size_t sent = client.write(
        (const uint8_t*)kClientBanner + wifi_banner_offset_[index], count);
    wifi_banner_offset_[index] += (uint16_t)sent;
    return;
  }

  if (!log_) return;
  uint32_t tail = log_->tailSequence();
  if (wifi_cursor_[index] < tail) {
    wifi_cursor_[index] = tail;
    wifi_overflow_notice_[index] = true;
  }
  if (wifi_overflow_notice_[index]) {
    size_t count = sizeof(kOverflowBanner) - 1;
    if (count > send_budget) count = send_budget;
    client.write((const uint8_t*)kOverflowBanner, count);
    wifi_overflow_notice_[index] = false;
    return;
  }

  uint8_t buffer[WIFI_SEND_LIMIT];
  size_t count = log_->copyFromSequence(wifi_cursor_[index], buffer, send_budget);
  if (count == 0) return;
  size_t sent = client.write(buffer, count);
  wifi_cursor_[index] += (uint32_t)sent;
}

void PartyAddons::serviceWifi(uint32_t now_ms) {
  acceptWifiClient();
  for (uint8_t i = 0; i < PARTY_TELNET_MAX_CLIENTS; ++i) {
    serviceWifiClient(i);
  }
  if (now_ms - last_net_heartbeat_ms_ >= 10000U) {
    last_net_heartbeat_ms_ = now_ms;
    if (log_) {
      log_->printf("# net: clients=%u rssi=%ld heap=%u\n",
                   connectedClientCount(), (long)WiFi.RSSI(),
                   (unsigned)ESP.getFreeHeap());
    }
  }
}

uint8_t PartyAddons::connectedClientCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < PARTY_TELNET_MAX_CLIENTS; ++i) {
    if (wifi_clients_[i] && wifi_clients_[i].connected()) ++count;
  }
  return count;
}

void PartyAddons::printStatus(Print& out) {
  out.printf("# addons: max_us=%lu budget_us=%u overruns=%lu wifi=%d ssid=%s "
             "clients=%u audio=%d vol=%u leds=%d ledZero=%d ledSign=%+d fps=%u\n",
             (unsigned long)max_service_us_, PARTY_LOOP_BUDGET_US,
             (unsigned long)over_budget_count_, wifi_ok_ ? 1 : 0,
             wifi_ok_ ? wifi_ssid_ : "OFF", connectedClientCount(),
             audio_ok_ ? 1 : 0, audio_volume_, leds_ok_ ? 1 : 0,
             led_zero_index_, led_direction_sign_,
             (unsigned)(1000U / led_frame_interval_ms_));
}

void PartyAddons::resetTiming() {
  max_service_us_ = 0;
  over_budget_count_ = 0;
  if (log_) log_->println(F("# addons: timing tracker reset"));
}
