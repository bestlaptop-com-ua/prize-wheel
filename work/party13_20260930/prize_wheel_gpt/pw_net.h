/* ============================================================================
 * pw_net.h - flash 13: WiFi for the wheel (owner 2026-09-30: "Enable wifi part
 * of the project. Connectivity, log reading, changing settings"; chose lab WiFi
 * with hotspot fallback, a phone web page + telnet, and firmware over WiFi).
 *
 * Included from pw_party_impl.h (bottom of the sketch) only when
 * PW_WIFI_ENABLE.  Replaces the flash-2 in-loop SoftAP/telnet code.
 *
 * Isolation rules (same spirit as WIFI_TASK.md):
 *  - ALL network work runs in its own FreeRTOS task pinned to core 0.  The
 *    control loop (core 1) never calls a socket or WiFi API.
 *  - The net task never touches control state.  It talks to the loop through
 *    three things only: a char queue (commands -> the SAME parser as the USB
 *    serial console, executed in loop context), a line queue (its own log lines
 *    -> printed by the loop, so only core 1 ever writes the log ring), and a
 *    5 Hz status snapshot the loop publishes (seqlock).
 *  - The log ring (the serial mirror) is read-only from the net task.
 *  - Anything that writes flash (saved WiFi settings, password, firmware) is
 *    refused unless the wheel is at rest; a firmware upload also pauses steering
 *    (takeoverEnabled=false in RAM, restored if the upload fails).
 *  - Reading (status, log) is open to anyone on the network; commands,
 *    settings and firmware need the wheel password (also the hotspot WPA2 key).
 *  - Firmware updates are rollback-protected: a new image must keep the control
 *    loop alive for 30 s after boot before it is confirmed; if it crashes or
 *    the loop stalls first, the bootloader returns to the previous firmware.
 * ========================================================================== */
#ifndef PW_NET_H
#define PW_NET_H
#if PW_WIFI_ENABLE

#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <freertos/queue.h>
#include <lwip/sockets.h>
#include <errno.h>

#define PW_NET_HOSTNAME      "prizewheel"      /* http://prizewheel.local       */
#define PW_NET_STA_WAIT_MS   20000   /* boot: lab WiFi this long, then hotspot   */
#define PW_NET_STA_LOST_MS   30000   /* lab WiFi lost this long -> hotspot       */
#define PW_NET_STA_RETRY_MS  120000  /* hotspot up: retry lab WiFi (no phone on) */
#define PW_NET_CMDQ_LEN      512
#define PW_NET_LOGQ_LEN      16
#define PW_NET_LOGLINE       128
#define PW_NET_SNAP_MS       200
#define PW_NET_OTA_CONFIRM_MS 30000
#define PW_NET_OTA_STALL_MS  20000
#define PW_NET_TX_POWER      WIFI_POWER_17dBm   /* review: gentler RF current peaks */
#define PW_NET_TASK_STACK    12288

/* An uploaded image must contain this marker (i.e. be prize-wheel firmware with
 * this network module); anything else is refused, because a sketch without
 * the module could not be updated over WiFi again or confirm itself.         */
#define PW_FW_MARKER "PWFW:net-ota-v1:7d3a91c4"
__attribute__((used)) static const char pwFwMarker[] = PW_FW_MARKER;

/* Rollback: the core must not auto-confirm a freshly uploaded image in
 * initArduino(); pwNetRollbackService() confirms it once the loop is healthy. */
extern "C" bool verifyRollbackLater() { return true; }

/* ------------------------------ shared state ------------------------------ */
struct PwNetSnap {
  uint32_t uptimeS;
  uint8_t state, fault, lastResult, lastQuality;
  bool atRest, armed, takeover, audio, leds, tmc, encoder, maint;
  float angle, omega, brakeGain, lastErr;
  int wedge, lastFinal, lastTarget;
  uint8_t volume;
  uint32_t spinNo;
  uint16_t counts[NUM_WEDGES];
};
static PwNetSnap pwSnapBuf;
static volatile uint32_t pwSnapSeq = 0;

static QueueHandle_t pwCmdQ = nullptr;     /* net -> loop, one char per item    */
static QueueHandle_t pwLogQ = nullptr;     /* net -> loop, one line per item    */
static volatile uint32_t pwLoopBeat = 0;   /* loop liveness for rollback        */
static volatile uint8_t pwMaintReq = 0;    /* 1 enter / 2 leave firmware upload */
static volatile bool pwMaintActive = false;
static volatile bool pwMaintRefused = false;
static bool pwMaintSavedTakeover = false;  /* loop-owned                        */
static bool pwMaintSavedAudio = true;      /* loop-owned                        */
static uint32_t pwSnapLastMs = 0;          /* loop-owned                        */

/* net-task-owned */
static Preferences pwNetPrefs;
static String pwStaSsid, pwStaPass, pwWheelPw;
static char pwStaSsidShow[33] = "";        /* copy for the loop's 'w' print     */
static char pwSsid[20] = "PW-0000";
enum PwNetMode : uint8_t { PWN_STA_TRY, PWN_STA_UP, PWN_AP };
static volatile PwNetMode pwNetMode = PWN_AP;
static volatile bool pwApUp = false;
static uint32_t pwModeSinceMs = 0, pwStaLostMs = 0, pwStaNextRetryMs = 0;
static volatile bool pwStaRestart = false;
static bool pwNetServersUp = false;
static WebServer pwHttp(80);
static WiFiServer pwTel(PW_TELNET_PORT);
static WiFiClient pwTc[PW_WIFI_MAX_CLIENTS];
static bool pwTcLive[PW_WIFI_MAX_CLIENTS] = {false};
static bool pwTcAuth[PW_WIFI_MAX_CLIENTS] = {false};
static uint32_t pwTcCur[PW_WIFI_MAX_CLIENTS];
static char pwTcLine[PW_WIFI_MAX_CLIENTS][72];
static uint8_t pwTcLen[PW_WIFI_MAX_CLIENTS] = {0};
static bool pwOtaPending = false;          /* booted an unconfirmed OTA image   */
static uint32_t pwOtaBeatSeen = 0, pwOtaBeatMs = 0;
static bool pwOtaBusy = false, pwOtaOk = false;
static String pwOtaErr;
static bool pwNetTaskRunning = false;
static TaskHandle_t pwNetTaskHandle = nullptr;
static uint32_t pwBootId = 0;
static uint32_t pwStaNextReconnMs = 0;
static uint8_t pwTcIac[PW_WIFI_MAX_CLIENTS] = {0};
static size_t pwOtaMarkPos = 0;
static bool pwOtaMarkFound = false;

/* ------------------------------ helpers ----------------------------------- */
static void pwNetLog(const char* fmt, ...) {        /* net task -> log (via loop) */
  if (!pwLogQ) return;
  char line[PW_NET_LOGLINE];
  va_list ap; va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  xQueueSend(pwLogQ, line, 0);                       /* full: drop, never wait   */
}

static bool pwSnapRead(PwNetSnap* out) {
  for (int i = 0; i < 8; ++i) {
    uint32_t s1 = pwSnapSeq;
    if (s1 & 1u) { vTaskDelay(1); continue; }
    __sync_synchronize();
    memcpy(out, &pwSnapBuf, sizeof(PwNetSnap));
    __sync_synchronize();
    if (pwSnapSeq == s1 && s1 != 0) return true;
  }
  return false;
}

/* Copy log bytes [from, ...) out of the mirror ring.  The ring is written only
 * by core 1; bytes that were overwritten while copying are trimmed off. */
static size_t pwRingCopy(uint32_t from, char* out, size_t max, uint32_t* next, bool* skipped) {
  uint32_t total = pwRingTotal;
  __sync_synchronize();
  uint32_t oldest = (total > PW_RING_BYTES) ? total - PW_RING_BYTES : 0;
  *skipped = false;
  if (from > total) from = oldest;                  /* stale cursor (wheel rebooted) */
  if (from < oldest) { *skipped = true; from = oldest; }
  size_t n = total - from;
  if (n > max) n = max;
  for (size_t i = 0; i < n; ++i) out[i] = pwRing[(from + i) % PW_RING_BYTES];
  __sync_synchronize();
  uint32_t total2 = pwRingTotal;
  uint32_t oldest2 = (total2 >= PW_RING_BYTES) ? total2 - PW_RING_BYTES + 1 : 0;   /* +1: byte in flight */
  if (from < oldest2) {
    uint32_t lost = oldest2 - from;
    *skipped = true;
    if (lost >= n) { n = 0; from = oldest2; }
    else { memmove(out, out + lost, n - lost); n -= lost; from += lost; }
  }
  *next = from + n;
  return n;
}

static void pwCmdPush(const char* s) {
  if (!pwCmdQ) return;
  for (; *s; ++s) xQueueSend(pwCmdQ, s, pdMS_TO_TICKS(20));
}

static void pwSetSta(const String& ssid, const String& pass) {
  pwStaSsid = ssid; pwStaPass = pass;
  strlcpy(pwStaSsidShow, ssid.c_str(), sizeof(pwStaSsidShow));
}

static String pwJsonEsc(const String& in) {
  String o;
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

static bool pwPwOk(const String& k) {
  if (k.length() != pwWheelPw.length()) return false;
  uint8_t d = 0;
  for (size_t i = 0; i < k.length(); ++i) d |= (uint8_t)(k[i] ^ pwWheelPw[i]);
  return d == 0;
}

static const char* pwNetModeName() {
  switch (pwNetMode) {
    case PWN_STA_TRY: return "joining lab WiFi";
    case PWN_STA_UP:  return "lab WiFi";
    default:          return "hotspot";
  }
}

/* --------------------------------- radio ---------------------------------- */
static void pwStartAp() {
  WiFi.mode(pwStaSsid.length() ? WIFI_AP_STA : WIFI_AP);
  bool ok = WiFi.softAP(pwSsid, pwWheelPw.c_str(), PW_WIFI_CHANNEL,
                        PW_WIFI_HIDDEN, PW_WIFI_MAX_CLIENTS);
  WiFi.setTxPower(PW_NET_TX_POWER);
  pwApUp = ok;
  pwNetMode = PWN_AP;
  pwModeSinceMs = millis();
  pwStaNextRetryMs = millis() + PW_NET_STA_RETRY_MS;
  if (ok) pwNetLog("# net: hotspot %s up at %s (password = wheel password)\n",
                   pwSsid, WiFi.softAPIP().toString().c_str());
  else    pwNetLog("# net: hotspot start FAILED; will retry (wheel unaffected)\n");
}

static void pwStartSta() {
  WiFi.mode(pwApUp ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(pwStaSsid.c_str(), pwStaPass.c_str());
  WiFi.setTxPower(PW_NET_TX_POWER);
  if (!pwApUp) { pwNetMode = PWN_STA_TRY; pwModeSinceMs = millis(); }
  pwNetLog("# net: joining lab WiFi \"%s\"\n", pwStaSsid.c_str());
}

static void pwStopAp() {
  WiFi.softAPdisconnect(true);
  pwApUp = false;
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(PW_NET_TX_POWER);
}

static void pwNetFsm(uint32_t now) {
  if (pwStaRestart) {                      /* new/forgotten lab WiFi settings */
    pwStaRestart = false;
    WiFi.disconnect(false, true);
    if (pwStaSsid.length()) pwStartSta(); else if (!pwApUp) pwStartAp();
    return;
  }
  const bool staUp = WiFi.status() == WL_CONNECTED;
  switch (pwNetMode) {
    case PWN_STA_TRY:
      if (staUp) {
        pwNetMode = PWN_STA_UP; pwModeSinceMs = now; pwStaLostMs = 0;
        pwNetLog("# net: lab WiFi up, http://%s  (http://%s.local)  rssi %d\n",
                 WiFi.localIP().toString().c_str(), PW_NET_HOSTNAME, (int)WiFi.RSSI());
      } else if (now - pwModeSinceMs > PW_NET_STA_WAIT_MS) {
        pwNetLog("# net: lab WiFi not reachable; starting hotspot\n");
        WiFi.disconnect(false, false);
        pwStartAp();
      }
      break;
    case PWN_STA_UP:
      if (staUp) { pwStaLostMs = 0; break; }
      if (pwStaLostMs == 0) {
        pwStaLostMs = now; pwStaNextReconnMs = now + 10000;
        pwNetLog("# net: lab WiFi lost; reconnecting\n"); WiFi.reconnect();
      } else if (now - pwStaLostMs > PW_NET_STA_LOST_MS) {
        pwNetLog("# net: lab WiFi gone %lus; starting hotspot\n", (unsigned long)(PW_NET_STA_LOST_MS / 1000));
        WiFi.disconnect(false, false);
        pwStartAp();
      } else if ((int32_t)(now - pwStaNextReconnMs) >= 0) {
        pwStaNextReconnMs = now + 10000;
        WiFi.reconnect();
      }
      break;
    case PWN_AP:
      if (!pwApUp && now - pwModeSinceMs > 30000) { pwStartAp(); break; }
      if (staUp) {
        if (WiFi.softAPgetStationNum() == 0) {      /* nobody on the hotspot */
          pwStopAp();
          pwNetMode = PWN_STA_UP; pwModeSinceMs = now; pwStaLostMs = 0;
          pwNetLog("# net: lab WiFi up, hotspot off, http://%s\n", WiFi.localIP().toString().c_str());
        }
      } else if (pwStaSsid.length() && (int32_t)(now - pwStaNextRetryMs) >= 0) {
        pwStaNextRetryMs = now + PW_NET_STA_RETRY_MS;
        if (WiFi.softAPgetStationNum() == 0) {      /* never disturb a phone */
          WiFi.begin(pwStaSsid.c_str(), pwStaPass.c_str());
        }
      }
      break;
  }
}

/* ------------------------------- telnet ----------------------------------- */
static void pwTelnetService() {
  WiFiClient fresh = pwTel.accept();
  if (fresh) {
    int slot = -1;
    for (int i = 0; i < PW_WIFI_MAX_CLIENTS; ++i)
      if (!pwTcLive[i] || !pwTc[i].connected()) { slot = i; break; }
    if (slot < 0) { fresh.print("# PW: busy (2 clients max)\r\n"); fresh.stop(); }
    else {
      pwTc[slot] = fresh; pwTc[slot].setNoDelay(true);
      pwTcLive[slot] = true; pwTcAuth[slot] = false; pwTcLen[slot] = 0; pwTcIac[slot] = 0;
      uint32_t total = pwRingTotal;
      pwTcCur[slot] = (total > PW_CATCHUP_BYTES) ? total - PW_CATCHUP_BYTES : 0;
      pwTc[slot].print("# PW telnet: log follows.  Type the wheel password + Enter to "
                       "send commands.\r\n");
      pwNetLog("# net: telnet client %d connected\n", slot);
    }
  }
  static char buf[512];
  for (int i = 0; i < PW_WIFI_MAX_CLIENTS; ++i) {
    if (!pwTcLive[i]) continue;
    if (!pwTc[i].connected()) {
      pwTcLive[i] = false; pwTc[i].stop();
      pwNetLog("# net: telnet client %d disconnected\n", i);
      continue;
    }
    for (int n = 0; n < 64 && pwTc[i].available(); ++n) {
      int ch = pwTc[i].read();
      if (ch < 0) break;
      /* telnet option negotiation: IAC cmd opt, IAC SB ... IAC SE */
      uint8_t& st = pwTcIac[i];
      if (st == 1) { st = (ch >= 0xFB && ch <= 0xFE) ? 2 : (ch == 0xFA ? 3 : 0); continue; }
      if (st == 2) { st = 0; continue; }
      if (st == 3) { if (ch == 0xFF) st = 4; continue; }
      if (st == 4) { st = (ch == 0xF0) ? 0 : 3; continue; }
      if (ch == 0xFF) { st = 1; continue; }
      if (ch >= 0x80 || (ch < 0x20 && ch != '\r' && ch != '\n')) continue;
      if (pwTcAuth[i]) {
#if PW_TELNET_COMMANDS
        char c = (char)ch; xQueueSend(pwCmdQ, &c, 0);
#endif
        continue;
      }
      if (ch == '\r' || ch == '\n') {
        if (pwTcLen[i] == 0) continue;
        pwTcLine[i][pwTcLen[i]] = 0;
        String k(pwTcLine[i]);
        pwTcLen[i] = 0;
        if (pwPwOk(k)) { pwTcAuth[i] = true; pwTc[i].print("# commands enabled (? for help)\r\n"); }
        else pwTc[i].print("# wrong password: log only\r\n");
      } else if (pwTcLen[i] < sizeof(pwTcLine[i]) - 1) {
        pwTcLine[i][pwTcLen[i]++] = (char)ch;
      }
    }
    bool skipped;
    uint32_t next;
    size_t n = pwRingCopy(pwTcCur[i], buf, sizeof(buf), &next, &skipped);
    if (n > 0) {
      /* never block on a slow/stalled client (review: write() retries 10 x 1 s) */
      const int fd = pwTc[i].fd();
      int r = (fd >= 0) ? send(fd, buf, n, MSG_DONTWAIT) : -1;
      if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) r = 0;
        else { pwTc[i].stop(); pwTcLive[i] = false; pwNetLog("# net: telnet client %d dropped\n", i); continue; }
      }
      pwTcCur[i] = next - (n - (size_t)r);
    } else {
      pwTcCur[i] = next;
    }
  }
}

/* ----------------------------- web page ----------------------------------- */
static const char PW_PAGE[] PROGMEM = R"PWHTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Prize Wheel</title>
<style>
:root{--bg:#0f1115;--card:#181b22;--line:#2a2f3a;--txt:#e7e9ee;--dim:#8b93a3;--acc:#f5b83d;--ok:#3ecf8e;--bad:#ff5d5d}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.4 -apple-system,system-ui,Segoe UI,Roboto,sans-serif}
header{position:sticky;top:0;z-index:2;background:var(--bg);border-bottom:1px solid var(--line);padding:10px 16px;display:flex;gap:10px;align-items:center}
header b{font-size:17px}#dot{width:10px;height:10px;border-radius:50%;background:var(--dim)}#hdr{color:var(--dim);font-size:13px;margin-left:auto;text-align:right}
main{max-width:760px;margin:0 auto;padding:12px 16px 40px}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
h2{margin:0 0 10px;font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--dim)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px}
.kv{background:var(--bg);border-radius:8px;padding:8px 10px}.kv span{display:block;color:var(--dim);font-size:12px}.kv b{font-size:16px}
button,input,select{font:inherit;color:var(--txt);background:#232834;border:1px solid var(--line);border-radius:8px;padding:9px 12px}
button{cursor:pointer}button:active{transform:scale(.98)}button.on{background:#2d3b2f;border-color:var(--ok)}button.warn{border-color:var(--acc)}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:6px 0}.row>*{flex:0 0 auto}.grow{flex:1 1 160px!important;min-width:0}
input[type=range]{flex:1 1 160px;padding:0}
#bars{display:grid;grid-template-columns:repeat(18,1fr);gap:3px;align-items:end;height:110px}
.bar{background:var(--acc);border-radius:3px 3px 0 0;min-height:2px;position:relative}.bar.dare{background:var(--bad);opacity:.5}
#lbl{display:grid;grid-template-columns:repeat(18,1fr);gap:3px;font-size:10px;color:var(--dim);text-align:center;margin-top:4px}
#log{background:#07080a;border-radius:8px;height:300px;overflow:auto;padding:8px;font:12px/1.35 ui-monospace,Menlo,Consolas,monospace;white-space:pre-wrap;word-break:break-word}
.dim{color:var(--dim);font-size:13px}#msg{min-height:1.2em;color:var(--acc);font-size:13px}
</style></head><body>
<header><div id="dot"></div><b>Prize Wheel</b><div id="hdr">connecting…</div></header>
<main>
<section><h2>Status</h2><div class="grid" id="st"></div></section>
<section><h2>Landings since power-on</h2><div id="bars"></div><div id="lbl"></div></section>
<section><h2>Controls</h2>
<div class="row"><input id="key" class="grow" type="password" placeholder="Wheel password (needed for changes)" autocomplete="current-password"><button onclick="saveKey()">Remember</button></div>
<div class="row"><span class="dim">Braking</span><button onclick="cmd('-')">−</button><b id="bg">–</b><button onclick="cmd('+')">+</button><span class="dim">(at rest only)</span></div>
<div class="row"><span class="dim">Volume</span><input id="vol" type="range" min="0" max="30" onchange="cmd('V'+this.value+'\n')"><b id="volv">–</b><button onclick="cmd('P')">Test</button></div>
<div class="row"><button id="bA" onclick="cmd('a')">Sound</button><button id="bL" onclick="cmd('l')">LEDs</button><button id="bT" class="warn" onclick="if(confirm('Toggle steering (takeover)? Off = the wheel spins freely and CAN land on a dare.'))cmd('e')">Steering</button></div>
<div class="row"><button onclick="cmd('s')">State</button><button onclick="cmd('f')">Friction</button><button onclick="cmd('t')">Timing</button><button onclick="cmd('w')">Network</button><button onclick="cmd('?')">Help</button></div>
<div class="row"><input id="raw" class="grow" placeholder="Any serial command, e.g. s" onkeydown="if(event.key==='Enter')sendRaw()"><button onclick="sendRaw()">Send</button></div>
<div id="msg"></div></section>
<section><h2>Live log</h2><div id="log"></div><div class="row"><label class="dim"><input type="checkbox" id="follow" checked> follow</label><button onclick="L.textContent=''">Clear view</button></div></section>
<section><h2>Network</h2><div class="dim" id="net"></div>
<div class="row"><input id="ssid" class="grow" placeholder="Lab WiFi name (SSID)"><input id="wpass" class="grow" type="password" placeholder="Lab WiFi password"></div>
<div class="row"><button onclick="setWifi()">Save &amp; join</button><button onclick="post('/wifi/forget',{})">Forget lab WiFi</button></div>
<div class="row"><input id="npw" class="grow" type="password" placeholder="New wheel password (8+ chars)"><button onclick="setPw()">Change password</button></div>
<div class="dim">The wheel password is also the hotspot password. Changing it drops phones on the hotspot.</div></section>
<section><h2>Firmware</h2><div class="dim" id="fw"></div>
<div class="row"><input id="bin" type="file" accept=".bin" class="grow"><button onclick="upload()">Upload</button></div>
<div class="dim">Only at rest; steering pauses during the upload. A new version that fails to start returns to the current one automatically.</div>
<div id="up" class="dim"></div><div id="fwnote" style="color:var(--acc);font-size:13px"></div></section>
</main>
<script>
const $=id=>document.getElementById(id),L=$('log');let from=0,S={},boot=null;
const esc=t=>String(t).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const DARE=[3,8,13,16];
$('key').value=localStorage.getItem('pwkey')||'';
function saveKey(){localStorage.setItem('pwkey',$('key').value);msg('Password remembered on this phone')}
function msg(t){$('msg').textContent=t;clearTimeout(msg.t);msg.t=setTimeout(()=>$('msg').textContent='',5000)}
async function post(u,o){try{const b=new URLSearchParams(o);const r=await fetch(u,{method:'POST',headers:{'X-Key':$('key').value},body:b});const t=await r.text();msg(r.ok?t:('Refused: '+t));return r.ok}catch(e){msg('Wheel not reachable');return false}}
function cmd(c){post('/cmd',{c})}
function sendRaw(){const v=$('raw').value;if(!v)return;cmd(v+'\n');$('raw').value=''}
function setWifi(){post('/wifi',{ssid:$('ssid').value,pass:$('wpass').value});$('wpass').value=''}
function setPw(){const n=$('npw').value;if(n.length<8){msg('8+ characters');return}post('/password',{new:n}).then(ok=>{if(ok){$('key').value=n;localStorage.setItem('pwkey',n)}});$('npw').value=''}
function kv(k,v){return `<div class="kv"><span>${k}</span><b>${v}</b></div>`}
function upload(){const f=$('bin').files[0];if(!f){msg('Pick a .bin file');return}
 const x=new XMLHttpRequest(),fd=new FormData();fd.append('fw',f,f.name);x.open('POST','/update');x.setRequestHeader('X-Key',$('key').value);
 x.upload.onprogress=e=>{if(e.lengthComputable)$('up').textContent='Uploading '+Math.round(100*e.loaded/e.total)+'%'};
 x.onload=()=>{$('up').textContent=x.responseText};x.onerror=()=>{$('up').textContent='Upload failed (connection)'};x.send(fd)}
function bars(c){if(!$('bars').children.length){for(let i=1;i<=18;i++){$('bars').insertAdjacentHTML('beforeend',`<div class="bar${DARE.includes(i)?' dare':''}"></div>`);$('lbl').insertAdjacentHTML('beforeend',`<div>${i}</div>`)}}
 const m=Math.max(1,...c);[...$('bars').children].forEach((b,i)=>{b.style.height=(DARE.includes(i+1)?4:Math.max(2,100*c[i]/m))+'%';b.title=`${i+1}: ${c[i]}`})}
async function status(){try{const r=await fetch('/status');S=await r.json();$('dot').style.background=S.fault==='NONE'?'var(--ok)':'var(--bad)';
 $('hdr').innerHTML=`${S.net.mode} · ${S.net.ip}<br>${S.fw}`;
 $('st').innerHTML=kv('State',S.state)+kv('Steering',S.armed?'armed':(S.maint?'paused (upload)':'NOT armed'))+kv('Fault',S.fault)+kv('Wedge',S.wedge)+kv('Speed',S.rps.toFixed(2)+' rev/s')+kv('Spins',S.spin)+kv('Last landing',S.last.final>0?(S.last.final+' ('+S.last.result.toLowerCase().replace(/_/g,' ')+')'):'–')+kv('Uptime',Math.floor(S.up/3600)+'h '+Math.floor(S.up%3600/60)+'m');
 $('bg').textContent=S.brake.toFixed(1);$('volv').textContent=S.vol;if(document.activeElement!==$('vol'))$('vol').value=S.vol;
 $('bA').className=S.audio?'on':'';$('bL').className=S.leds?'on':'';$('bT').className=S.takeover?'on warn':'warn';
 $('net').innerHTML=`Mode: <b>${S.net.mode}</b> · ${S.net.ip} · lab WiFi: ${esc(S.net.ssid)||'not set'}${S.net.rssi?(' ('+S.net.rssi+' dBm)'):''} · hotspot: ${S.net.ap}${S.net.defpw?' · <b style="color:var(--acc)">default password – please change</b>':''}`;
 $('fwnote').textContent=S.pending?'New firmware on trial: keep the wheel powered for 1 minute so it is confirmed.':'';
 $('fw').textContent=`${S.fw} · running ${S.part} · free heap ${Math.round(S.heap/1024)} KB`;bars(S.counts)}catch(e){$('dot').style.background='var(--bad)';$('hdr').textContent='wheel not reachable'}}
async function logs(){try{const r=await fetch('/log?from='+from);const b0=r.headers.get('X-Boot');if(boot!==null&&b0!==boot){L.textContent+='\n--- wheel restarted ---\n';from=0;boot=b0;return}boot=b0;const t=await r.text();from=+r.headers.get('X-Next')||from;if(t){const b=L.scrollTop+L.clientHeight>=L.scrollHeight-20;L.textContent+=t;if(L.textContent.length>60000)L.textContent=L.textContent.slice(-40000);if($('follow').checked&&b)L.scrollTop=L.scrollHeight}}catch(e){}}
status();logs();setInterval(status,1000);setInterval(logs,700);
</script></body></html>)PWHTML";

static void pwHttpDeny(int code, const char* why) { pwHttp.send(code, "text/plain", why); }

static bool pwHttpAuth() {
  if (pwPwOk(pwHttp.header("X-Key"))) return true;
  pwHttpDeny(403, "wrong or missing wheel password");
  return false;
}

static bool pwAtRestNow() {
  PwNetSnap s;
  return pwSnapRead(&s) && s.atRest;
}

static void pwHttpStatus() {
  PwNetSnap s;
  if (!pwSnapRead(&s)) { pwHttpDeny(503, "busy"); return; }
  static char js[1400];
  char counts[18 * 6 + 4];
  size_t p = 0;
  for (int i = 0; i < NUM_WEDGES && p < sizeof(counts) - 8; ++i)
    p += snprintf(counts + p, sizeof(counts) - p, "%s%u", i ? "," : "", (unsigned)s.counts[i]);
  counts[p] = 0;
  const esp_partition_t* run = esp_ota_get_running_partition();
  String ip = (pwNetMode == PWN_STA_UP) ? WiFi.localIP().toString()
                                        : (pwApUp ? WiFi.softAPIP().toString() : String("-"));
  snprintf(js, sizeof(js),
    "{\"fw\":\"%s\",\"part\":\"%s\",\"up\":%lu,\"heap\":%u,"
    "\"state\":\"%s\",\"fault\":\"%s\",\"armed\":%s,\"maint\":%s,\"takeover\":%s,"
    "\"tmc\":%s,\"encoder\":%s,\"wedge\":%d,\"angle\":%.1f,\"rps\":%.3f,"
    "\"brake\":%.2f,\"vol\":%u,\"audio\":%s,\"leds\":%s,\"spin\":%lu,"
    "\"last\":{\"final\":%d,\"target\":%d,\"result\":\"%s\",\"err\":%.1f,\"q\":%u},"
    "\"counts\":[%s],"
    "\"pending\":%s,\"net\":{\"mode\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,\"ap\":\"%s\",\"apClients\":%d,\"defpw\":%s}}",
    PW_FW_TAG, run ? run->label : "?", (unsigned long)s.uptimeS, (unsigned)ESP.getFreeHeap(),
    stateName((State)s.state), faultName((FaultCode)s.fault), s.armed ? "true" : "false",
    s.maint ? "true" : "false", s.takeover ? "true" : "false",
    s.tmc ? "true" : "false", s.encoder ? "true" : "false", s.wedge + 1, (double)s.angle,
    (double)s.omega, (double)s.brakeGain, (unsigned)s.volume, s.audio ? "true" : "false",
    s.leds ? "true" : "false", (unsigned long)s.spinNo,
    s.lastFinal >= 0 ? s.lastFinal + 1 : 0, s.lastTarget >= 0 ? s.lastTarget + 1 : 0,
    resultName((SpinResult)s.lastResult), (double)s.lastErr, (unsigned)s.lastQuality,
    counts, pwOtaPending ? "true" : "false", pwNetModeName(), ip.c_str(), pwJsonEsc(pwStaSsid).c_str(),
    (pwNetMode == PWN_STA_UP) ? (int)WiFi.RSSI() : 0,
    pwApUp ? pwSsid : "off", pwApUp ? (int)WiFi.softAPgetStationNum() : 0,
    pwWheelPw == PW_WIFI_PASSWORD ? "true" : "false");
  pwHttp.sendHeader("Cache-Control", "no-store");
  pwHttp.send(200, "application/json", js);
}

static void pwHttpLog() {
  static char buf[PW_RING_BYTES + 32];
  uint32_t from = (uint32_t)strtoul(pwHttp.arg("from").c_str(), nullptr, 10);
  uint32_t next; bool skipped;
  size_t off = 0;
  size_t n = pwRingCopy(from, buf + 16, PW_RING_BYTES, &next, &skipped);
  if (skipped && from != 0) { memcpy(buf, "[... skipped]\n", 14); off = 14; memmove(buf + 14, buf + 16, n); }
  else memmove(buf, buf + 16, n);
  buf[off + n] = 0;
  pwHttp.sendHeader("X-Next", String(next));
  pwHttp.sendHeader("X-Boot", String(pwBootId));
  pwHttp.sendHeader("Cache-Control", "no-store");
  pwHttp.send(200, "text/plain; charset=utf-8", buf);
}

static void pwHttpCmd() {
  if (!pwHttpAuth()) return;
  String c = pwHttp.arg("c");
  if (!c.length() || c.length() > 64) { pwHttpDeny(400, "empty or too long"); return; }
  pwCmdPush(c.c_str());
  pwHttp.send(200, "text/plain", pwAtRestNow() ? ("sent: " + c)
                                               : ("queued: " + c + " (runs once the wheel is at rest)"));
}

static void pwHttpWifi() {
  if (!pwHttpAuth()) return;
  if (!pwAtRestNow()) { pwHttpDeny(409, "only while the wheel is at rest"); return; }
  String ssid = pwHttp.arg("ssid"), pass = pwHttp.arg("pass");
  ssid.trim();
  if (!ssid.length() || ssid.length() > 32 || pass.length() > 63) { pwHttpDeny(400, "check the name/password"); return; }
  pwNetPrefs.putString("ssid", ssid);
  pwNetPrefs.putString("pass", pass);
  pwSetSta(ssid, pass);
  pwStaRestart = true;
  pwHttp.send(200, "text/plain", "Saved. Joining \"" + ssid + "\" - a phone on the hotspot may drop; "
                                 "then open http://prizewheel.local on the lab WiFi.");
  pwNetLog("# net: lab WiFi settings saved (\"%s\")\n", ssid.c_str());
}

static void pwHttpForget() {
  if (!pwHttpAuth()) return;
  if (!pwAtRestNow()) { pwHttpDeny(409, "only while the wheel is at rest"); return; }
  pwNetPrefs.remove("ssid"); pwNetPrefs.remove("pass");
  pwSetSta(String(), String());
  pwStaRestart = true;
  pwHttp.send(200, "text/plain", "Lab WiFi forgotten; hotspot only.");
  pwNetLog("# net: lab WiFi settings cleared\n");
}

static void pwHttpPassword() {
  if (!pwHttpAuth()) return;
  if (!pwAtRestNow()) { pwHttpDeny(409, "only while the wheel is at rest"); return; }
  String n = pwHttp.arg("new");
  if (n.length() < 8 || n.length() > 63) { pwHttpDeny(400, "8-63 characters"); return; }
  for (size_t i = 0; i < n.length(); ++i)
    if (n[i] < 0x20 || n[i] > 0x7e) { pwHttpDeny(400, "plain characters only"); return; }
  pwNetPrefs.putString("wpw", n);
  pwWheelPw = n;
  pwHttp.send(200, "text/plain", "Password changed.");
  pwNetLog("# net: wheel password changed\n");
  for (int i = 0; i < PW_WIFI_MAX_CLIENTS; ++i) pwTcAuth[i] = false;
  if (pwApUp) { vTaskDelay(pdMS_TO_TICKS(300)); pwStartAp(); }   /* new WPA2 key */
}

/* Firmware upload: multipart POST /update, field "fw". */
static void pwHttpUpdateChunk() {
  HTTPUpload& up = pwHttp.upload();
  if (up.status == UPLOAD_FILE_START) {
    pwOtaOk = false; pwOtaErr = ""; pwOtaBusy = false;
    if (!pwPwOk(pwHttp.header("X-Key"))) { pwOtaErr = "wrong or missing wheel password"; return; }
    if (!pwAtRestNow()) { pwOtaErr = "only while the wheel is at rest"; return; }
    pwMaintRefused = false; pwMaintReq = 1;
    for (int i = 0; i < 100 && !pwMaintActive && !pwMaintRefused; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (!pwMaintActive) { pwOtaErr = "wheel busy (not at rest); try again"; pwMaintReq = 2; return; }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      pwOtaErr = String("cannot start: ") + Update.errorString();
      pwMaintReq = 2; return;
    }
    pwOtaBusy = true;
    pwOtaMarkPos = 0; pwOtaMarkFound = false;
    pwNetLog("# net: firmware upload started (%s)\n", up.filename.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!pwOtaBusy) return;
    static const size_t mlen = sizeof(PW_FW_MARKER) - 1;
    for (size_t k = 0; k < up.currentSize && !pwOtaMarkFound; ++k) {   /* marker has no self-overlap */
      const char ch = (char)up.buf[k];
      if (ch == PW_FW_MARKER[pwOtaMarkPos]) { if (++pwOtaMarkPos == mlen) pwOtaMarkFound = true; }
      else pwOtaMarkPos = (ch == PW_FW_MARKER[0]) ? 1 : 0;
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      pwOtaErr = String("write failed: ") + Update.errorString();
      Update.abort(); pwOtaBusy = false; pwMaintReq = 2;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!pwOtaBusy) return;
    pwOtaBusy = false;
    if (!pwOtaMarkFound) {
      Update.abort();
      pwOtaErr = "not prize-wheel firmware with WiFi (marker missing)";
      pwMaintReq = 2;
    } else if (Update.end(true)) { pwOtaOk = true; pwNetLog("# net: firmware upload complete (%u bytes); restarting\n", (unsigned)up.totalSize); }
    else { pwOtaErr = String("image rejected: ") + Update.errorString(); pwMaintReq = 2; }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (pwOtaOk) {                     /* client left after a complete upload */
      pwNetLog("# net: upload finished, client gone; restarting into new firmware\n");
      vTaskDelay(pdMS_TO_TICKS(500));
      ESP.restart();
    }
    if (pwOtaBusy) { Update.abort(); pwOtaBusy = false; }
    pwOtaErr = "upload aborted"; pwMaintReq = 2;
  }
}

static void pwHttpUpdateDone() {
  if (pwOtaOk) {
    pwHttp.send(200, "text/plain", "Firmware accepted. The wheel restarts now; refresh this page in ~20 s.");
    vTaskDelay(pdMS_TO_TICKS(800));      /* let the response and log line out */
    ESP.restart();
  }
  if (!pwOtaErr.length()) pwOtaErr = "no firmware file received";
  if (pwMaintActive) pwMaintReq = 2;
  pwHttp.send(400, "text/plain", "Update failed: " + pwOtaErr + " (current firmware unchanged)");
  pwNetLog("# net: firmware upload failed: %s\n", pwOtaErr.c_str());
}

static void pwNetServersBegin() {
  if (pwNetServersUp) return;
  static const char* hdrs[] = {"X-Key"};
  pwHttp.collectHeaders(hdrs, 1);
  pwHttp.on("/", HTTP_GET, []() { pwHttp.send_P(200, "text/html", PW_PAGE); });
  pwHttp.on("/status", HTTP_GET, pwHttpStatus);
  pwHttp.on("/log", HTTP_GET, pwHttpLog);
  pwHttp.on("/cmd", HTTP_POST, pwHttpCmd);
  pwHttp.on("/wifi", HTTP_POST, pwHttpWifi);
  pwHttp.on("/wifi/forget", HTTP_POST, pwHttpForget);
  pwHttp.on("/password", HTTP_POST, pwHttpPassword);
  pwHttp.on("/update", HTTP_POST, pwHttpUpdateDone, pwHttpUpdateChunk);
  pwHttp.onNotFound([]() { pwHttp.send(404, "text/plain", "not found"); });
  pwHttp.begin();
  pwTel.begin();
  pwTel.setNoDelay(true);
  if (MDNS.begin(PW_NET_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("telnet", "tcp", PW_TELNET_PORT);
  }
  pwNetServersUp = true;
}

/* ------------------------------ rollback ---------------------------------- */
static void pwNetRollbackService(uint32_t now) {
  if (!pwOtaPending) return;
  uint32_t beat = pwLoopBeat;
  if (beat != pwOtaBeatSeen) { pwOtaBeatSeen = beat; pwOtaBeatMs = now; }
  if (now > PW_NET_OTA_CONFIRM_MS && now - pwOtaBeatMs < 1000) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
      pwNetLog("# net: new firmware confirmed after %lus healthy running\n", (unsigned long)(now / 1000));
    pwOtaPending = false;
  } else if (now - pwOtaBeatMs > PW_NET_OTA_STALL_MS) {
    pwNetLog("# net: new firmware stalled; rolling back\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_ota_mark_app_invalid_rollback_and_reboot();
    pwOtaPending = false;                         /* returned: nothing to roll back to */
    pwNetLog("# net: rollback impossible (no previous firmware); keeping this one\n");
  }
}

/* -------------------------------- task ------------------------------------ */
static void pwNetTask(void*) {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setHostname(PW_NET_HOSTNAME);
  if (pwStaSsid.length()) pwStartSta(); else pwStartAp();
  WiFi.setTxPower(PW_NET_TX_POWER);
  pwNetServersBegin();
  pwOtaBeatMs = millis();
  for (;;) {
    const uint32_t now = millis();
    pwNetFsm(now);
    pwHttp.handleClient();
    pwTelnetService();
    pwNetRollbackService(now);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/* ------------------- entry points used by pw_party_impl.h ------------------ */
void pwNetBegin() {            /* setup(), core 1, before the task exists */
  pwCmdQ = xQueueCreate(PW_NET_CMDQ_LEN, 1);
  pwLogQ = xQueueCreate(PW_NET_LOGQ_LEN, PW_NET_LOGLINE);
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(pwSsid, sizeof(pwSsid), "PW-%02X%02X", mac[4], mac[5]);
  if (pwNetPrefs.begin("pwnet", false)) {
    pwSetSta(pwNetPrefs.getString("ssid", ""), pwNetPrefs.getString("pass", ""));
    pwWheelPw = pwNetPrefs.getString("wpw", PW_WIFI_PASSWORD);
  } else {
    pwWheelPw = PW_WIFI_PASSWORD;
  }
  if (pwWheelPw.length() < 8) pwWheelPw = PW_WIFI_PASSWORD;
  esp_ota_img_states_t st;
  const esp_partition_t* run = esp_ota_get_running_partition();
  pwOtaPending = run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
                 st == ESP_OTA_IMG_PENDING_VERIFY;
  if (!pwCmdQ || !pwLogQ) {
    Serial.println(F("# net: queue alloc FAILED; WiFi off (wheel unaffected)"));
    if (pwOtaPending) esp_ota_mark_app_valid_cancel_rollback();   /* nothing else could confirm it */
    return;
  }
  pwBootId = esp_random();
  pwNetTaskRunning = xTaskCreatePinnedToCore(pwNetTask, "pwnet", PW_NET_TASK_STACK, nullptr, 1,
                                             &pwNetTaskHandle, 0) == pdPASS;
  Serial.printf("# net: %s; lab WiFi %s; hotspot %s; http://%s.local  telnet :%d%s\n",
                pwNetTaskRunning ? "task on core 0" : "task create FAILED (wheel unaffected)",
                pwStaSsid.length() ? "configured" : "not set", pwSsid, PW_NET_HOSTNAME,
                PW_TELNET_PORT, pwOtaPending ? "; NEW FIRMWARE pending 30 s health check" : "");
  if (!pwNetTaskRunning && pwOtaPending) esp_ota_mark_app_valid_cancel_rollback();
}

/* Loop side (core 1), called from pwPartyService every pass.  Bounded work. */
void pwNetLoopService(uint32_t nowMs) {
  pwLoopBeat = pwLoopBeat + 1;
  if (!pwCmdQ || !pwLogQ) return;
  char line[PW_NET_LOGLINE];
  for (int i = 0; i < 4 && xQueueReceive(pwLogQ, line, 0) == pdTRUE; ++i) Serial.print(line);

  const bool still = !(encoderVelocityValid && fabsf(omega) > STILL_REV_S);
  const bool atRest = still && (state == ST_IDLE_STOPPED || state == ST_SOFT_HOLD ||
                                state == ST_FAULT_LATCHED);
  /* Remote commands: one char per pass (like the USB console), only at rest,
   * never during an upload, and only with room in the UART buffer ('?' prints
   * ~5 KB).  Mid-spin they wait in the queue; USB stays the mid-spin path.   */
  char c;
  if (atRest && !pwMaintActive && Serial.availableForWrite() >= 6144 &&
      xQueueReceive(pwCmdQ, &c, 0) == pdTRUE)
    dispatchCommandChar(c);

  const uint8_t req = pwMaintReq;
  if (req == 1) {
    pwMaintReq = 0;
    if (!pwMaintActive) {
      if (atRest) {
        pwMaintSavedTakeover = takeoverEnabled;
        pwMaintSavedAudio = pwAudioEnabled;
        takeoverEnabled = false;                 /* RAM only; NVS untouched */
        pwAudioEnabled = false;                  /* flash erases stall the I2S feed */
        pwDfpFlush();
        pwDfpSendNow(PW_DFP_CMD_STOP, 0);
        pwMaintActive = true;
        Serial.println(F("# net: firmware upload - steering and sound paused"));
      } else {
        pwMaintRefused = true;
      }
    }
  } else if (req == 2) {
    pwMaintReq = 0;
    if (pwMaintActive) {
      takeoverEnabled = pwMaintSavedTakeover;
      pwAudioEnabled = pwMaintSavedAudio;
      pwMaintActive = false;
      Serial.printf("# net: firmware upload ended without update - steering %s\n",
                    takeoverEnabled ? "restored" : "stays off");
    }
  }

  if (nowMs - pwSnapLastMs < PW_NET_SNAP_MS) return;
  pwSnapLastMs = nowMs;
  pwSnapSeq = pwSnapSeq + 1;               /* odd: writing */
  __sync_synchronize();
  PwNetSnap& s = pwSnapBuf;
  s.uptimeS = nowMs / 1000;
  s.state = (uint8_t)state;
  s.fault = (uint8_t)faultCode;
  s.atRest = atRest;
  s.armed = steeringArmed();
  s.takeover = takeoverEnabled;
  s.maint = pwMaintActive;
  s.audio = pwAudioEnabled;
  s.leds = pwLedEnabled;
  s.tmc = tmcOk;
  s.encoder = encoderPrimed;
  s.angle = wheelAngleDeg();
  s.wedge = currentWedge();
  s.omega = encoderVelocityValid ? omega : 0.0f;
  s.brakeGain = brakeGain;
  s.volume = (uint8_t)lroundf(pwI2sGain * 30.0f / PW_I2S_GAIN_MAX);
  s.spinNo = spinCounter;
  s.lastFinal = spinOpen ? -1 : spin.finalWedge;
  s.lastTarget = spinOpen ? -1 : spin.targetWedge;
  s.lastResult = (uint8_t)spin.result;
  s.lastErr = spin.targetErrDeg;
  s.lastQuality = spin.targetQuality;
  for (int i = 0; i < NUM_WEDGES; ++i) s.counts[i] = wedgeChosenCount[i];
  __sync_synchronize();
  pwSnapSeq = pwSnapSeq + 1;               /* even: stable */
}

void pwNetPrintStatus() {        /* 'w' command, loop context: read-only peeks */
  Serial.printf("# net: mode=%s ssid(lab)=%s hotspot=%s(%s) task=%d stackFree=%u fw=%s heap=%u "
                "ota=%s marker=%s\n",
                pwNetModeName(), pwStaSsidShow[0] ? pwStaSsidShow : "-",
                pwSsid, pwApUp ? "up" : "off", pwNetTaskRunning ? 1 : 0,
                pwNetTaskHandle ? (unsigned)uxTaskGetStackHighWaterMark(pwNetTaskHandle) : 0u,
                PW_FW_TAG, (unsigned)ESP.getFreeHeap(), pwOtaPending ? "pending" : "confirmed",
                pwFwMarker);
}

#endif /* PW_WIFI_ENABLE */
#endif /* PW_NET_H */
