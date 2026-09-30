"""Flash 13 (2026-09-30): WiFi - lab network with hotspot fallback, phone web page,
telnet, firmware over WiFi with rollback.  Applies to the flash-12 source
(party12_20260930) plus the new prize_wheel_gpt/pw_net.h.  Exact-count anchors.

Owner 9/30 10:00: "Enable wifi part of the project. Connectivity, log reading,
changing settings."  Chosen: lab WiFi with hotspot fallback; web page + telnet;
firmware upload over WiFi included.
- pw_party.h: PW_WIFI_ENABLE 1, PW_FW_TAG.
- pw_party_impl.h: the flash-2 in-loop SoftAP/telnet block is replaced by
  pw_net.h (all network work in a core-0 task; loop side = two queue drains + a
  5 Hz status snapshot); pwPartyBegin/pwPartyService/'w' call into it.
- prize_wheel_gpt.ino: handleSerial() split so serial, telnet and the web page
  share dispatchCommandChar() (incl. the G imbalance line); build banner.
  DIAG_CAPACITY drops 2304 -> 1664 with WiFi compiled in (existing switch, flash 2).
Control logic untouched."""
from pathlib import Path
D = Path(__file__).resolve().parent / 'prize_wheel_gpt'


def rep(name, old, new, count=1):
    p = D / name
    s = p.read_text(encoding='utf-8')
    n = s.count(old)
    assert n == count, f'{name}: anchor count {n} != {count}: {old[:80]!r}'
    p.write_text(s.replace(old, new), encoding='utf-8', newline='\n')


assert (D / 'pw_net.h').is_file(), 'pw_net.h missing'
H = 'pw_party.h'; I = 'pw_party_impl.h'; INO = 'prize_wheel_gpt.ino'

rep(H, '#define PW_WIFI_ENABLE    0 /* SoftAP + telnet mirror/commands (WIFI_TASK.md)     */',
       '#define PW_WIFI_ENABLE    1 /* flash 13: lab WiFi + hotspot, web page, telnet, OTA (pw_net.h) */')
rep(H, '#define PW_WIFI_PASSWORD  "spinthewheel"  /* CHANGE BEFORE THE PARTY           */',
       '#define PW_WIFI_PASSWORD  "spinthewheel"  /* DEFAULT wheel password only: change it on the web page (NVS) */\n'
       '#define PW_FW_TAG "party13-20260930"      /* shown on the web page             */')

# replace the flash-2 in-loop WiFi block with the task-based module
p = D / I
s = p.read_text(encoding='utf-8')
a = s.index('/* -------------------------------- WiFi ------------------------------------ */\n#if PW_WIFI_ENABLE\nstatic WiFiServer pwServer(PW_TELNET_PORT);')
b = s.index('#endif /* PW_WIFI_ENABLE */\n', a) + len('#endif /* PW_WIFI_ENABLE */\n')
s = s[:a] + ('/* -------------------------------- WiFi ------------------------------------ */\n'
             '/* flash 13: task-based network module (lab WiFi + hotspot, web, telnet, OTA) */\n'
             '#if PW_WIFI_ENABLE\n#include "pw_net.h"\n#endif\n') + s[b:]
p.write_text(s, encoding='utf-8', newline='\n')

rep(I, '''  if (pwWifiUp) {
    int live = 0;
    for (int i = 0; i < PW_WIFI_MAX_CLIENTS; ++i)
      if (pwClientLive[i] && pwClients[i].connected()) ++live;
    Serial.printf("# net: ap=%s ip=%s ch=%d clients=%d assoc=%d heap=%u\\n",
                  pwSsid, WiFi.softAPIP().toString().c_str(), PW_WIFI_CHANNEL,
                  live, (int)WiFi.softAPgetStationNum(),
                  (unsigned)ESP.getFreeHeap());
  } else {
    Serial.printf("# net: SoftAP DOWN (retries %u/%u); wheel unaffected\\n",
                  pwWifiRetries, (unsigned)PW_WIFI_MAX_RETRIES);
  }''', '''  pwNetPrintStatus();''')
rep(I, '''#if PW_WIFI_ENABLE
  pwWifiTryStart();
#endif''', '''#if PW_WIFI_ENABLE
  pwNetBegin();
#endif''')
rep(I, '''#if PW_WIFI_ENABLE
  pwWifiService(nowMs);
#endif''', '''#if PW_WIFI_ENABLE
  pwNetLoopService(nowMs);
#endif''')
rep(I, '''    " a  audio on/off   P  play fanfare (speaker test)   l  LEDs on/off   w  network status\\n"''',
       '''    " a  audio on/off   P  play fanfare (speaker test)   l  LEDs on/off   w  network status\\n"
    " WiFi: web page http://prizewheel.local (or the IP in 'w'), telnet port 23\\n"''')

# shared command path: serial, telnet and web
rep(INO, '''  char command = (char)Serial.read();
  if (imbalanceLine.open()) {''', '''  dispatchCommandChar((char)Serial.read());
}

// flash 13: the USB console, telnet and the web page all feed this one parser.
void dispatchCommandChar(char command) {
  if (imbalanceLine.open()) {''')
rep(INO, 'Serial.println(F("# build: party12-20260930 (flash 12:',
    'Serial.println(F("# build: party13-20260930 (flash 13: WiFi - lab network + hotspot, web page, telnet, OTA) on party12-20260930 (flash 12:')
print('flash13 applied')
