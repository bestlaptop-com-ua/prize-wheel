/* ============================================================================
 * pw_party_impl.h - implementations for pw_party.h
 *
 * Included ONCE at the very BOTTOM of prize_wheel_gpt.ino: everything here can
 * see the frozen control core's globals and functions, and NOTHING here is
 * visible to the control core except the five entry points declared in
 * pw_party.h.  The control core never calls into FX/WiFi except through those
 * hooks; FX/WiFi never mutate control state (the only writes into core state
 * are the sanctioned S1 restore in pwPartyBegin and the sanctioned S2 fault
 * entry, both explicitly authorized by PARTY_TASK.md).
 *
 * FX events are derived by OBSERVING the state machine once per loop pass
 * (state / spin / spinOpen / wedge), so the frozen control functions carry no
 * event-emission edits at all.
 * ========================================================================== */
#ifndef PW_PARTY_IMPL_H
#define PW_PARTY_IMPL_H

/* --------------------------- mirror storage ------------------------------- */
char pwRing[PW_RING_BYTES];
volatile uint32_t pwRingTotal = 0;
PwMirrorSerial PwSerial;

/* --------------------------- budget tracker ------------------------------- */
static uint32_t pwMaxFxWifiUs = 0;      /* window max, reset by 't'            */
static uint32_t pwMaxFxWifiUsBoot = 0;  /* since boot                          */
static uint32_t pwMaxS2Us = 0;
static uint32_t pwMaxS2UsBoot = 0;
static uint32_t pwMaxLoopUs = 0;
static uint32_t pwMaxLoopUsBoot = 0;
static uint32_t pwOverBudgetPasses = 0; /* FX+WiFi passes > 2000 us, boot      */
static uint32_t pwBudgetPasses = 0;

/* ------------------------------ DFPlayer ---------------------------------- */
#define PW_DFP_CMD_PLAY_MP3   0x12  /* play /mp3/NNNN.mp3 by number            */
#define PW_DFP_CMD_LOOP_CUR   0x19  /* arg 0 = repeat current track, 1 = stop  */
#define PW_DFP_CMD_STOP       0x16
#define PW_DFP_CMD_VOLUME     0x06

struct PwDfpCmd { uint8_t cmd; uint16_t arg; };
static PwDfpCmd pwDfpQueueBuf[8];
static uint8_t pwDfpQueueHead = 0, pwDfpQueueCount = 0;
static uint32_t pwDfpLastSendMs = 0;
static uint32_t pwDfpReadyAtMs = 0;
static bool pwAudioEnabled = (PW_FX_AUDIO_ENABLE != 0);

static void pwDfpSendNow(uint8_t cmd, uint16_t arg) {
#if PW_FX_AUDIO_ENABLE
  /* 10-byte DFPlayer frame, feedback disabled: pure fire-and-forget.  The
   * 10 bytes land in the UART1 hardware FIFO and drain at 9600 baud in ~10 ms,
   * far inside the 120 ms command gap, so this write can never block. */
  uint8_t f[10];
  f[0] = 0x7E; f[1] = 0xFF; f[2] = 0x06; f[3] = cmd; f[4] = 0x00;
  f[5] = (uint8_t)(arg >> 8); f[6] = (uint8_t)(arg & 0xFF);
  uint16_t ck = (uint16_t)(0 - (0xFF + 0x06 + cmd + 0x00 + f[5] + f[6]));
  f[7] = (uint8_t)(ck >> 8); f[8] = (uint8_t)(ck & 0xFF);
  f[9] = 0xEF;
  Serial1.write(f, 10);
#else
  (void)cmd; (void)arg;
#endif
}

static void pwDfpFlush() { pwDfpQueueCount = 0; }

static void pwDfpQueue(uint8_t cmd, uint16_t arg, bool coalesce = false) {
  if (!pwAudioEnabled) return;
  if (coalesce && pwDfpQueueCount > 1) return;   /* drop surplus ticks        */
  if (pwDfpQueueCount >= 8) return;              /* overflow: drop, fail-silent */
  uint8_t slot = (uint8_t)((pwDfpQueueHead + pwDfpQueueCount) % 8);
  pwDfpQueueBuf[slot].cmd = cmd;
  pwDfpQueueBuf[slot].arg = arg;
  ++pwDfpQueueCount;
}

static void pwDfpService(uint32_t nowMs) {
  if (pwDfpQueueCount == 0) return;
  if ((int32_t)(nowMs - pwDfpReadyAtMs) < 0) return;         /* module booting */
  if (nowMs - pwDfpLastSendMs < PW_DFP_CMD_GAP_MS) return;   /* rate limit     */
  pwDfpSendNow(pwDfpQueueBuf[pwDfpQueueHead].cmd, pwDfpQueueBuf[pwDfpQueueHead].arg);
  pwDfpQueueHead = (uint8_t)((pwDfpQueueHead + 1) % 8);
  --pwDfpQueueCount;
  pwDfpLastSendMs = nowMs;
}

/* ------------------------------- LEDs ------------------------------------- */
#define PWL_STANDBY 0
#define PWL_SPIN    1
#define PWL_FAULT   2
#define PWL_OFF     3

#if PW_FX_LED_ENABLE
static CRGB pwLeds[PW_NUM_LEDS];
#endif
static volatile uint8_t pwFxMode = PWL_STANDBY;
static volatile float pwFxOmega = 0.0f;          /* signed rev/s, live encoder */
static volatile uint32_t pwFxCelebrateAtMs = 0;  /* celebrate window start     */
static volatile uint8_t  pwFxLandedWedge  = 255; /* landed wedge: celebration slams in its colour */
static volatile bool pwLedEnabled = (PW_FX_LED_ENABLE != 0);
static bool pwLedTaskRunning = false;

#if PW_FX_LED_ENABLE
/* All rendering below runs ONLY in the core-0 task; it must never print and
 * never touch control globals (it reads the volatile scalars above).          */
static void pwLedRenderStandby(uint32_t nowMs) {
  uint8_t t1 = (uint8_t)(nowMs / 7);   /* ~6x faster: helix wrap hides half the travel */
  uint8_t t2 = (uint8_t)(nowMs / 9);
  for (int i = 0; i < PW_NUM_LEDS; ++i) {
    uint8_t a = sin8((uint8_t)(i * 3 + t1));       /* two counter-drifting     */
    uint8_t b = sin8((uint8_t)(i * 2 - t2));       /* sine hue waves           */
    uint8_t v = qadd8(a / 2, b / 2);
    pwLeds[i] = CHSV((uint8_t)(160 + (v >> 2)), 180, scale8(v, PW_LED_STANDBY_BRIGHT));
  }
}

static void pwLedRenderSpin(float bandPhase) {
  /* 5 colour bands travelling along the helix; speed AND direction from the
   * live encoder omega so the lights can never contradict the wheel.          */
  uint16_t phase = (uint16_t)((int32_t)bandPhase & 0xFFFF);
  for (int i = 0; i < PW_NUM_LEDS; ++i) {
    uint8_t hue = (uint8_t)(((uint32_t)i * 5u * 256u / PW_NUM_LEDS) - phase);
    uint8_t v = sin8((uint8_t)(hue * 2));          /* band envelope            */
    pwLeds[i] = CHSV(hue, 255, scale8(v, 150));
  }
}

static void pwLedRenderCelebrate(uint32_t elapsedMs) {
  uint8_t bright = (uint8_t)(PW_LED_CELEBRATE_BRIGHT -
      (uint32_t)(PW_LED_CELEBRATE_BRIGHT - PW_LED_STANDBY_BRIGHT) * elapsedMs / PW_LED_CELEBRATE_MS);
  uint32_t phase = elapsedMs / 250;
  if (phase & 1) {                                 /* white sparkle frame      */
    fill_solid(pwLeds, PW_NUM_LEDS, CRGB::Black);
    for (int n = 0; n < PW_NUM_LEDS / 8; ++n)
      pwLeds[random16(PW_NUM_LEDS)] = CHSV(0, 0, bright);
  } else {                          /* full-strip slam: the landed wedge's colour */
    static const CRGB kWedgeColor[6] = {  /* wedge%6: green orange blue red yellow purple */
      CRGB(0,220,0), CRGB(255,60,0), CRGB(0,70,255),
      CRGB(255,0,0), CRGB(255,190,0), CRGB(140,0,255) };
    uint8_t w = pwFxLandedWedge;
    CRGB c = (w < 12) ? kWedgeColor[w % 6]
                      : CRGB(CHSV((uint8_t)(phase * 37), 255, 255));
    c.nscale8_video(bright);
    fill_solid(pwLeds, PW_NUM_LEDS, c);
  }
}

static void pwLedRenderFault() {
  /* FX_TASK.md: fault = dim steady, never blinking red (no alarm looks).      */
  fill_solid(pwLeds, PW_NUM_LEDS, CRGB(30, 12, 2));
}

static void pwFxLedTask(void*) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(1000 / PW_LED_FPS);
  float bandPhase = 0.0f;
  uint32_t prevMs = millis();
  for (;;) {
    vTaskDelayUntil(&lastWake, period);
    uint32_t nowMs = millis();
    float dt = (float)(nowMs - prevMs) * 0.001f;
    prevMs = nowMs;

    uint32_t celAt = pwFxCelebrateAtMs;
    bool celebrating = celAt != 0 && (int32_t)(nowMs - celAt) >= 0 &&
                       (nowMs - celAt) < PW_LED_CELEBRATE_MS;
    uint8_t mode = pwFxMode;
    if (!pwLedEnabled) {
      fill_solid(pwLeds, PW_NUM_LEDS, CRGB::Black);
    } else if (celebrating) {
      pwLedRenderCelebrate(nowMs - celAt);
    } else if (mode == PWL_FAULT) {
      pwLedRenderFault();
    } else if (mode == PWL_SPIN) {
      bandPhase += pwFxOmega * PW_LED_BAND_SPEED * dt * 256.0f;
      pwLedRenderSpin(bandPhase);
    } else {
      pwLedRenderStandby(nowMs);
    }
    FastLED.show();   /* ~10.6 ms at 300 LEDs - blocks THIS core-0 task only  */
  }
}
#endif /* PW_FX_LED_ENABLE */

/* ------------------------ FX observer + audio logic ------------------------ */
static uint8_t pwPrevState = 255;
static bool pwPrevSpinOpen = false;
static int pwPrevWedge = -1;
static bool pwRatchetOn = false;
static uint32_t pwFanfareAtMs = 0;      /* deadline for the post-landing fanfare */
static uint32_t pwLastTickMs = 0;
#if PW_FX_IDLE_AMBIENCE
static bool pwAmbienceOn = false;
static uint32_t pwQuietSinceMs = 0;
#endif

static void pwFxService(uint32_t nowMs) {
  float speed = encoderVelocityValid ? fabsf(omega) : 0.0f;
  bool faulted = (state == ST_FAULT_LATCHED);

  /* ---- spin-close observer: landing celebration / guest jingle ---------- */
  bool open = spinOpen;
  if (pwPrevSpinOpen && !open) {
    bool safeLanding = false;
    switch (spin.result) {
      case RES_CONTROLLED_SAFE:
      case RES_EDGE_SAFE:
      case RES_OFF_TARGET_SAFE:
        safeLanding = true;
        break;
      case RES_NO_REACHABLE_SAFE:
        /* A weak spin that coasted out on a safe wedge is a normal landing to
         * the guests: celebrate it.  A dare rest stays silent (honest).       */
        safeLanding = !isDare(spin.finalWedge);
        break;
      case RES_GUEST_STOPPED:
        pwDfpFlush();
        pwDfpQueue(PW_DFP_CMD_PLAY_MP3, PW_TRK_GUEST);
        pwRatchetOn = false;
        break;
      default:
        break;   /* GUEST_RESPUN / FAULTED / CONTROL_LOCKED: no sound          */
    }
    if (safeLanding && !faulted) {
      pwDfpFlush();
      pwDfpQueue(PW_DFP_CMD_STOP, 0);                /* short pause, then...   */
      pwRatchetOn = false;
      pwFanfareAtMs = nowMs + PW_FX_LANDED_PAUSE_MS; /* ...fanfare             */
      pwFxCelebrateAtMs = pwFanfareAtMs;             /* LEDs sync to fanfare   */
      pwFxLandedWedge = (uint8_t)spin.finalWedge;   /* slam in this wedge's colour */
    }
  }
  pwPrevSpinOpen = open;

  /* ---- fault edge: audio stops, no scary noises ------------------------- */
  if (faulted && pwPrevState != ST_FAULT_LATCHED) {
    pwDfpFlush();
    pwDfpQueue(PW_DFP_CMD_STOP, 0);
    pwRatchetOn = false;
    pwFanfareAtMs = 0;
  }

  /* ---- delayed fanfare --------------------------------------------------- */
  if (pwFanfareAtMs != 0 && (int32_t)(nowMs - pwFanfareAtMs) >= 0) {
    pwFanfareAtMs = 0;
    if (!faulted) pwDfpQueue(PW_DFP_CMD_PLAY_MP3, PW_TRK_FANFARE);
  }

  /* ---- ratchet <-> tick regime (the concealment core) -------------------- */
  if (!faulted && pwAudioEnabled) {
    if (!pwRatchetOn && speed >= PW_FX_RATCHET_ON_REV_S) {
      pwDfpFlush();
      pwDfpQueue(PW_DFP_CMD_PLAY_MP3, PW_TRK_RATCHET);
      pwDfpQueue(PW_DFP_CMD_LOOP_CUR, 0);            /* loop it                */
      pwRatchetOn = true;
    } else if (pwRatchetOn && speed < PW_FX_RATCHET_OFF_REV_S) {
      pwDfpFlush();
      pwDfpQueue(PW_DFP_CMD_STOP, 0);                /* hand over to ticks     */
      pwRatchetOn = false;
    }
    /* One tick per wedge crossing below the crossover; ticks continue
     * straight through capture and braking - silence would be a tell.         */
    int wedge = currentWedge();
    if (wedge != pwPrevWedge) {
      if (false && /* single-whirl mode: ticks disabled, whirl runs to stop (owner 2026-08-06) */
          !pwRatchetOn && speed > 0.03f && pwFanfareAtMs == 0 &&
          nowMs - pwLastTickMs >= PW_DFP_CMD_GAP_MS) {
        pwDfpQueue(PW_DFP_CMD_PLAY_MP3, PW_TRK_TICK, /*coalesce=*/true);
        pwLastTickMs = nowMs;
        Serial.printf("# clickq ms=%lu ang=%.1f w=%d om=%.3f\n",
                      (unsigned long)nowMs, wheelAngleDeg(), wedge, speed);
      }
      pwPrevWedge = wedge;
    }
  }

#if PW_FX_IDLE_AMBIENCE
  /* ---- optional idle ambience ------------------------------------------- */
  bool quiet = (state == ST_IDLE_STOPPED) && speed < 0.03f && !faulted;
  if (quiet) {
    if (pwQuietSinceMs == 0) pwQuietSinceMs = nowMs;
    if (!pwAmbienceOn && nowMs - pwQuietSinceMs > 8000) {
      pwDfpQueue(PW_DFP_CMD_PLAY_MP3, PW_TRK_AMBIENCE);
      pwDfpQueue(PW_DFP_CMD_LOOP_CUR, 0);
      pwAmbienceOn = true;
    }
  } else {
    pwQuietSinceMs = 0;
    if (pwAmbienceOn) {
      pwAmbienceOn = false;
      if (!pwRatchetOn) { /* regime logic owns the player otherwise */ }
    }
  }
#endif

  /* ---- LED mode scalars (volatile handoff to the core-0 task) ------------ */
  pwFxOmega = encoderVelocityValid ? omega : 0.0f;
  if (faulted) {
    pwFxMode = PWL_FAULT;
  } else if (speed > 0.05f ||
             (state >= ST_SPIN_PUSH && state <= ST_LANDING_SETTLE)) {
    pwFxMode = PWL_SPIN;
  } else {
    pwFxMode = PWL_STANDBY;
  }

  pwDfpService(nowMs);
  pwPrevState = (uint8_t)state;
}

/* -------------------------------- WiFi ------------------------------------ */
#if PW_WIFI_ENABLE
static WiFiServer pwServer(PW_TELNET_PORT);
static WiFiClient pwClients[PW_WIFI_MAX_CLIENTS];
static uint32_t pwClientCursor[PW_WIFI_MAX_CLIENTS];
static bool pwClientLive[PW_WIFI_MAX_CLIENTS] = {false};
static bool pwWifiUp = false;
static uint8_t pwWifiRetries = 0;
static uint32_t pwWifiNextRetryMs = 0;
static uint32_t pwNetBeatMs = 0;
static char pwSsid[16] = "PW-0000";

static void pwWifiTryStart() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(pwSsid, sizeof(pwSsid), "PW-%02X%02X", mac[4], mac[5]);
  bool ok = WiFi.softAP(pwSsid, PW_WIFI_PASSWORD, PW_WIFI_CHANNEL,
                        PW_WIFI_HIDDEN, PW_WIFI_MAX_CLIENTS);
  if (ok) {
    pwServer.begin();
    pwServer.setNoDelay(true);
    pwWifiUp = true;
    Serial.printf("# net: SoftAP %s up, telnet %s:%d\n", pwSsid,
                  WiFi.softAPIP().toString().c_str(), PW_TELNET_PORT);
  } else {
    ++pwWifiRetries;
    pwWifiNextRetryMs = millis() + PW_WIFI_RETRY_MS;
    Serial.printf("# net: SoftAP start failed (attempt %u/%u); wheel unaffected\n",
                  pwWifiRetries, (unsigned)PW_WIFI_MAX_RETRIES);
  }
}

static void pwWifiService(uint32_t nowMs) {
  if (!pwWifiUp) {
    if (pwWifiRetries > 0 && pwWifiRetries < PW_WIFI_MAX_RETRIES &&
        (int32_t)(nowMs - pwWifiNextRetryMs) >= 0) {
      pwWifiTryStart();
    }
    return;
  }

  /* accept (never blocks) */
  WiFiClient fresh = pwServer.accept();
  if (fresh) {
    int slot = -1;
    for (int i = 0; i < PW_WIFI_MAX_CLIENTS; ++i) {
      if (!pwClientLive[i] || !pwClients[i].connected()) { slot = i; break; }
    }
    if (slot < 0) {
      fresh.print("# PW: busy (2 clients max)\n");
      fresh.stop();
    } else {
      pwClients[slot] = fresh;
      pwClients[slot].setNoDelay(true);
      pwClientLive[slot] = true;
      uint32_t total = pwRingTotal;
      pwClientCursor[slot] = (total > PW_CATCHUP_BYTES) ? total - PW_CATCHUP_BYTES : 0;
      pwClients[slot].print("# PW telnet: connected; recent log follows; "
                            "commands as on serial (? for help)\n");
      Serial.printf("# net: client %d connected\n", slot);
    }
  }

  int connectedCount = 0;
  for (int i = 0; i < PW_WIFI_MAX_CLIENTS; ++i) {
    if (!pwClientLive[i]) continue;
    if (!pwClients[i].connected()) {
      pwClientLive[i] = false;
      pwClients[i].stop();
      Serial.printf("# net: client %d disconnected\n", i);
      continue;
    }
    ++connectedCount;

    /* inbound: same single-char parser as the serial console (bounded) */
    for (int n = 0; n < 16 && pwClients[i].available(); ++n) {
      int ch = pwClients[i].read();
      if (ch <= 0 || ch >= 0x80) continue;   /* strip telnet IAC noise */
#if PW_TELNET_COMMANDS
      if (!pwPartyCommandChar((char)ch)) handleCommandChar((char)ch);
#endif
    }

    /* outbound: ring catch-up, drop-oldest, never block */
    uint32_t cur = pwClientCursor[i];
    uint32_t total = pwRingTotal;
    uint32_t pending = total - cur;
    if (pending > PW_RING_BYTES) {          /* overrun: drop oldest */
      cur = total - PW_RING_BYTES;
      pending = PW_RING_BYTES;
    }
    if (pending > 0) {
      int room = pwClients[i].availableForWrite();
      uint32_t budget = pending;
      if (room >= 0 && (uint32_t)room < budget) budget = (uint32_t)room;
      if (budget > PW_NET_CHUNK) budget = PW_NET_CHUNK;
      while (budget > 0) {
        uint32_t idx = cur % PW_RING_BYTES;
        uint32_t span = PW_RING_BYTES - idx;
        if (span > budget) span = budget;
        size_t sent = pwClients[i].write((const uint8_t*)&pwRing[idx], span);
        cur += sent;
        budget -= sent;
        if (sent < span) break;             /* stack refused; retry next pass */
      }
      pwClientCursor[i] = cur;
    }
  }

  /* heartbeat through the normal log path (serial AND telnet) */
  if (nowMs - pwNetBeatMs >= PW_NET_HEARTBEAT_MS) {
    pwNetBeatMs = nowMs;
    Serial.printf("# net: ap=%s clients=%d(assoc %d) heap=%u\n", pwSsid,
                  connectedCount, (int)WiFi.softAPgetStationNum(),
                  (unsigned)ESP.getFreeHeap());
  }
}
#endif /* PW_WIFI_ENABLE */

/* ------------------------- S2: TMC config verify --------------------------- */
#if PW_S2_ENABLE
static uint32_t pwS2LastMs = 0;

static void pwS2Service(uint32_t nowMs) {
  /* Only while IDLE with the wheel quiet: the readback is a blocking UART
   * transaction (~1-2 ms) and must never touch the 1 kHz cadence in motion. */
  if (state != ST_IDLE_STOPPED || !tmcOk) return;
  if (encoderVelocityValid && fabsf(omega) > STILL_REV_S) return;
  if (nowMs - pwS2LastMs < 5000) return;
  pwS2LastMs = nowMs;
  uint16_t got = driver.microsteps();
  if (got == MICROSTEPS) return;
  uint16_t got2 = driver.microsteps();   /* tolerate a single line glitch */
  if (got2 == MICROSTEPS) {
    Serial.printf("# S2: transient CHOPCONF readback glitch (%u then %u); ignored\n",
                  got, got2);
    return;
  }
  /* Driver lost its config (or UART is down): this is exactly the "rattle
   * with test_connection still passing" failure from the bench.  Latch via
   * the existing TMC_UART fault path - no new fault types.                  */
  Serial.printf("# S2: TMC config verify FAILED (microsteps=%u,%u expected %u)\n",
                got, got2, (unsigned)MICROSTEPS);
  tmcOk = false;
  enterFault(FC_TMC_UART, "S2 config verify mismatch");
}
#endif /* PW_S2_ENABLE */

bool pwS2ReconfigVerify() {
#if !PW_S2_ENABLE
  return true;
#else
  /* Called from the 'r' handler after test_connection passed, wheel at rest,
   * coils floated: re-assert the full boot configuration (a driver that
   * power-cycled runs at defaults even though the UART answers), restore the
   * CS_FREEWHEEL register state, and prove the write stuck via readback.     */
  driverConfig();
  driver.freewheel(1);
  driver.ihold(0);
  uint16_t got = driver.microsteps();
  if (got == MICROSTEPS) {
    Serial.println(F("# S2: driver config re-applied and verified"));
    return true;
  }
  Serial.printf("# S2: config verify still failing after re-apply (microsteps=%u)\n", got);
  tmcOk = false;
  return false;
#endif
}

/* ------------------------------ commands ----------------------------------- */
static void pwPrintBudget() {
  Serial.printf("# budget: fx+wifi max=%luus (boot %luus) s2 max=%luus (boot %luus) "
                "loop max=%luus (boot %luus) passes=%lu over2ms=%lu\n",
                (unsigned long)pwMaxFxWifiUs, (unsigned long)pwMaxFxWifiUsBoot,
                (unsigned long)pwMaxS2Us, (unsigned long)pwMaxS2UsBoot,
                (unsigned long)pwMaxLoopUs, (unsigned long)pwMaxLoopUsBoot,
                (unsigned long)pwBudgetPasses, (unsigned long)pwOverBudgetPasses);
  pwMaxFxWifiUs = pwMaxS2Us = pwMaxLoopUs = 0;   /* window resets on print */
}

static void pwPrintNet() {
#if PW_WIFI_ENABLE
  if (pwWifiUp) {
    int live = 0;
    for (int i = 0; i < PW_WIFI_MAX_CLIENTS; ++i)
      if (pwClientLive[i] && pwClients[i].connected()) ++live;
    Serial.printf("# net: ap=%s ip=%s ch=%d clients=%d assoc=%d heap=%u\n",
                  pwSsid, WiFi.softAPIP().toString().c_str(), PW_WIFI_CHANNEL,
                  live, (int)WiFi.softAPgetStationNum(),
                  (unsigned)ESP.getFreeHeap());
  } else {
    Serial.printf("# net: SoftAP DOWN (retries %u/%u); wheel unaffected\n",
                  pwWifiRetries, (unsigned)PW_WIFI_MAX_RETRIES);
  }
#else
  Serial.println(F("# net: WiFi disabled at compile time"));
#endif
}

bool pwPartyCommandChar(char c) {
  static int volAccum = -1;
  static uint32_t volSinceMs = 0;
  uint32_t nowMs = millis();

  if (volAccum >= 0 && nowMs - volSinceMs > 3000) volAccum = -1;  /* stale V */
  if (volAccum >= 0) {
    if (c >= '0' && c <= '9') {
      volAccum = volAccum * 10 + (c - '0');
      if (volAccum > 30) volAccum = 30;
      volSinceMs = nowMs;
      return true;
    }
    pwDfpQueue(PW_DFP_CMD_VOLUME, (uint16_t)volAccum);
    Serial.printf("# audio volume -> %d\n", volAccum);
    volAccum = -1;
    return true;   /* terminator (Enter or any non-digit) consumed */
  }

  switch (c) {
    case 'V':
      volAccum = 0;
      volSinceMs = nowMs;
      return true;
    case 't':
      pwPrintBudget();
      return true;
    case 'a':
      pwAudioEnabled = !pwAudioEnabled;
      if (!pwAudioEnabled) {
        pwDfpFlush();
        pwDfpSendNow(PW_DFP_CMD_STOP, 0);   /* immediate, bypasses gate */
        pwRatchetOn = false;
      }
      Serial.printf("# audio %s\n", pwAudioEnabled ? "ON" : "OFF");
      return true;
    case 'l':
      pwLedEnabled = !pwLedEnabled;
      Serial.printf("# leds %s\n", pwLedEnabled ? "ON" : "OFF");
      return true;
    case 'L': {                       /* LED slam test: cycles wedge colours */
      static uint8_t testW = 0;
      pwFxLandedWedge = testW;
      pwFxCelebrateAtMs = nowMs;
      Serial.printf("# LED slam test wedge=%u\n", testW);
      testW = (uint8_t)((testW + 1) % 12);
      return true;
    }
    case 'w':
      pwPrintNet();
      return true;
    default:
      return false;
  }
}

void pwPartyHelpLines() {
  Serial.println(F(
    " --- party additions ---\n"
    " t  FX/WiFi loop-budget max-tracker (resets window)\n"
    " a  audio on/off   l  LEDs on/off   w  network status\n"
    " V<n>+Enter  DFPlayer volume 0-30 (e.g. V18)"));
}

/* ------------------------------ lifecycle ---------------------------------- */
void pwPartyBegin() {
  /* WIFI_TASK power caution: make a brownout visible in the boot log.        */
  if (esp_reset_reason() == ESP_RST_BROWNOUT) {
    Serial.println(F("# WARN: last reset was BROWNOUT - check 5V rail under WiFi+LED load"));
  }

#if PW_S1_ENABLE
  /* S1: a fault latched before power-off stays latched.  Only 'r' clears it
   * (and a passing attended probe for DIR_CAL, mirroring the RAM latch).     */
  uint8_t stored = preferences.getUChar(PW_S1_NVS_KEY, 0);
  if (stored != 0 && stored <= (uint8_t)FC_LANDING_UNSAFE &&
      state != ST_FAULT_LATCHED) {
    faultCode = (FaultCode)stored;
    state = ST_FAULT_LATCHED;
    stateEnteredMs = millis();
    Serial.printf("# S1: latched fault %s restored from NVS; takeover locked until r\n",
                  faultName(faultCode));
  }
#endif

#if PW_FX_AUDIO_ENABLE
  Serial1.begin(PW_DFP_BAUD, SERIAL_8N1, PW_DFP_RX_PIN, PW_DFP_TX_PIN);
  pwDfpReadyAtMs = millis() + PW_DFP_BOOT_DELAY_MS;
  pwDfpQueue(PW_DFP_CMD_VOLUME, PW_DFP_VOLUME);
  Serial.printf("# fx: DFPlayer on UART1 tx=%d rx=%d vol=%d (tracks /mp3/0001..0006)\n",
                PW_DFP_TX_PIN, PW_DFP_RX_PIN, (int)PW_DFP_VOLUME);
#endif

#if PW_FX_LED_ENABLE
  FastLED.addLeds<WS2812B, PW_LED_PIN, PW_LED_ORDER>(pwLeds, PW_NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, PW_FX_MAX_MA);
  FastLED.clear();   /* first show() happens in the core-0 task */
  BaseType_t ok = xTaskCreatePinnedToCore(pwFxLedTask, "pwfx", 4096, nullptr,
                                          1, nullptr, 0);
  pwLedTaskRunning = (ok == pdPASS);
  if (pwLedTaskRunning) {
    Serial.printf("# fx: WS2812B x%d on GPIO%d, %d fps task on core 0, cap %d mA "
                  "(FAS=MCPWM, LEDs=RMT: no channel conflict)\n",
                  (int)PW_NUM_LEDS, (int)PW_LED_PIN, (int)PW_LED_FPS, (int)PW_FX_MAX_MA);
  } else {
    pwLedEnabled = false;
    Serial.println(F("# fx: LED task create FAILED; LEDs disabled (wheel unaffected)"));
  }
#endif

#if PW_WIFI_ENABLE
  pwWifiTryStart();
#endif

  Serial.printf("# party build: S1=%d S2=%d S3=%d wifi=%d audio=%d leds=%d\n",
                (int)PW_S1_ENABLE, (int)PW_S2_ENABLE, (int)PW_S3_ENABLE,
                (int)PW_WIFI_ENABLE, (int)PW_FX_AUDIO_ENABLE, (int)PW_FX_LED_ENABLE);
}

void pwPartyService(uint32_t loopStartUs) {
  uint32_t nowMs = millis();
  uint32_t t1 = micros();
  pwFxService(nowMs);
#if PW_WIFI_ENABLE
  pwWifiService(nowMs);
#endif
  uint32_t t2 = micros();
#if PW_S2_ENABLE
  pwS2Service(nowMs);   /* sanctioned fix - tracked separately from FX/WiFi */
#endif
  uint32_t t3 = micros();

  uint32_t fxWifiUs = t2 - t1;
  uint32_t s2Us = t3 - t2;
  uint32_t loopUs = t3 - loopStartUs;
  if (fxWifiUs > pwMaxFxWifiUs) pwMaxFxWifiUs = fxWifiUs;
  if (fxWifiUs > pwMaxFxWifiUsBoot) pwMaxFxWifiUsBoot = fxWifiUs;
  if (s2Us > pwMaxS2Us) pwMaxS2Us = s2Us;
  if (s2Us > pwMaxS2UsBoot) pwMaxS2UsBoot = s2Us;
  if (loopUs > pwMaxLoopUs) pwMaxLoopUs = loopUs;
  if (loopUs > pwMaxLoopUsBoot) pwMaxLoopUsBoot = loopUs;
  if (fxWifiUs > 2000) ++pwOverBudgetPasses;
  ++pwBudgetPasses;
}

#endif /* PW_PARTY_IMPL_H */
