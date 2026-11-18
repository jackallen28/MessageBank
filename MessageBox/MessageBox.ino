/*
  MessageBox - test firmware
  ==========================
  A little box that shows messages from a friend or partner. They email a private Gmail
  inbox; the box reads it.

  How it behaves:
    Waiting       the screen is blank and the LED ring is off.
    New message   the ring spins (style and colours are settings), the motor goes
                  tick tick tick tick tick TICK (each longer and stronger), and the screen shows
                  a pixel envelope with a heart and the number of new messages.
    Press         opens the oldest new message. Turn clockwise to scroll down, anticlockwise up.
    Press         "Keep this message?": turn anticlockwise for DELETE, clockwise for SAVE,
                  then press again to confirm. The next new message opens.
    All read      a happy envelope, then the box powers down: the ring goes out LED by LED,
                  a fading brrr-rr-r, and the screen goes blank.
    Press when blank with nothing new: look through the saved messages (keep or delete each).
    Hold 5 s      opens the setup Wi-Fi (the ring fills up while you hold).
  Every click and press of the knob gives a small haptic tick (can be turned off).

  Board:  ESP32-C3 SuperMini (Arduino: "ESP32C3 Dev Module", USB CDC On Boot: Enabled,
          Partition Scheme: Huge APP)
  Parts:  WeAct 3.7" e-paper 240x416 (GDEY037T03), WS2812B 12-LED ring, EC11/KY-040 rotary
          encoder with push button, PWM coin vibration motor module.
  Libraries (Library Manager): GxEPD2, Adafruit GFX Library, Adafruit NeoPixel.

  Wiring (see ../wiring.svg and ../README.md):
    E-paper  VCC->3V3  GND->GND  SCL->GPIO4  SDA->GPIO6  CS->GPIO7  D/C->GPIO5  RES->GPIO10  BUSY->GPIO3
    Encoder  +->3V3 (not 5V!)  GND->GND  CLK(S1)->GPIO0  DT(S2)->GPIO1  SW(KEY)->GPIO9
    LED ring 5V->5V  GND->GND  DIN->330 ohm->GPIO2            (+ 470 uF across 5V/GND)
    Vibe     VCC->5V  GND->GND  IN->GPIO20

  First start: the box opens its own Wi-Fi "MessageBox-Setup" (password "messagebox").
  Join it and open http://192.168.4.1 to test every part and enter the Wi-Fi and Gmail details.
  Later the same page is at http://messagebox.local .

  Email subjects starting with "!" are instructions instead of messages:
    !led pink | red | green | blue | purple | orange | white | #rrggbb | off   (glow while waiting)
    !rainbow           !buzz 3          !ping          !clear (delete all messages)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include "config.h"
#include "imap.h"
#include "haptics.h"
#include "leds.h"
#include "store.h"
#include "screen.h"
#include "web_page.h"

String nowText() {
  struct tm t;
  if (!getLocalTime(&t, 10)) return "";
  char b[24];
  strftime(b, sizeof(b), "%a %d %b %H:%M", &t);
  return b;
}

// ---------------------------------------------------------------- rotary encoder and button

volatile int32_t encRaw = 0;
volatile uint8_t encState = 0;

void IRAM_ATTR encISR() {
  static const int8_t table[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
  encState = ((encState << 2) | (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B)) & 0x0F;
  encRaw = encRaw + table[encState];
}

// Clicks turned since boot; positive = clockwise.
int32_t encPosition() {
  noInterrupts();
  int32_t r = encRaw;
  interrupts();
  int32_t p = r / ENC_STEPS_PER_DETENT;
  return cfg.knobReverse ? -p : p;
}

bool btnDown = false;
uint32_t btnChange = 0, btnDownAt = 0, pressCount = 0;
bool longFired = false;

// ---------------------------------------------------------------- what the box is doing

enum Ui : uint8_t { UI_IDLE, UI_ENVELOPE, UI_READ, UI_DECIDE, UI_HAPPY, UI_SETUP };
const char *const UI_NAMES[] = { "waiting (blank)", "new message envelope", "reading", "keep or delete", "all read", "setup" };
Ui ui = UI_IDLE;

uint32_t curId = 0;        // the message being read
bool savedList = false;    // reading saved messages instead of new ones
int sessionDone = 0;       // new messages dealt with since the envelope was opened
std::vector<Line> curLines;
int scrollPos = 0;
int8_t choice = 0;
uint32_t lastInput = 0, happyAt = 0, notifyAt = 0, lastRemind = 0;
uint32_t ambientColor = 0; // "!led" colour shown while waiting (0 = off)
bool ambientRainbow = false;
uint32_t testScreenUntil = 0;

bool apOn = false;

int firstNew() {
  for (int i = 0; i < (int)store.size(); i++) if (store[i].state == MSG_NEW) return i;
  return -1;
}

int nextSavedAfter(uint32_t id) {
  for (int i = 0; i < (int)store.size(); i++) if (store[i].state == MSG_SAVED && store[i].id > id) return i;
  return -1;
}

void ringForState() {
  switch (ui) {
    case UI_IDLE:
      if (ambientRainbow) ringSet(RING_RAINBOW);
      else ringSet(ambientColor ? RING_SOLID : RING_OFF, ambientColor);
      break;
    case UI_ENVELOPE: ringSet(RING_NOTIFY); break;
    case UI_READ: ringSet(RING_DIM, cfg.color1); break;
    case UI_DECIDE: ringSet(RING_DIM, cfg.color1); break;
    case UI_HAPPY: ringSet(RING_RAINBOW); break;
    case UI_SETUP: ringSet(RING_SOLID, 0x0040FF); break;
  }
  choiceGlow = ui == UI_DECIDE ? choice : 0;
}

String setupText() {
  return "1. On your phone, join the Wi-Fi\n    " + String(AP_SSID) + "\n    password: " + cfg.apPass +
         "\n2. Open http://192.168.4.1\n3. Test the parts, then enter your Wi-Fi and Gmail details.\n\n" +
         (cfg.ssid.length() ? "Saved Wi-Fi: " + cfg.ssid + " (retrying)" : String("No Wi-Fi saved yet"));
}

// Sends the screen for the current state to the drawing task.
void showUi(bool clean = false, bool forceFull = false) {
  View v;
  v.cleanOK = clean;
  switch (ui) {
    case UI_IDLE: v.scr = SCR_BLANK; break;
    case UI_ENVELOPE: v.scr = SCR_ENVELOPE; v.count = countState(MSG_NEW); break;
    case UI_HAPPY: v.scr = SCR_HAPPY; break;
    case UI_SETUP: v.scr = SCR_SETUP; v.text = setupText(); break;
    case UI_READ:
    case UI_DECIDE: {
      int i = findMsg(curId);
      if (i < 0) { v.scr = SCR_BLANK; break; }
      const Message &m = store[i];
      v.scr = ui == UI_READ ? SCR_READ : SCR_DECIDE;
      v.from = m.from;
      v.when = m.when;
      v.title = m.subject.length() ? m.subject : (m.body.length() ? m.body : String("(empty)"));
      v.savedList = savedList;
      if (savedList) {
        int pos = 0;
        for (const Message &s : store) { if (s.state == MSG_SAVED) pos++; if (s.id == curId) break; }
        v.pos = pos;
        v.total = countState(MSG_SAVED);
      } else {
        v.pos = sessionDone + 1;
        v.total = sessionDone + countState(MSG_NEW);
      }
      if (ui == UI_READ) { v.lines = curLines; v.scroll = scrollPos; }
      v.choice = choice;
      break;
    }
  }
  screenShow(v, forceFull);
}

void setUi(Ui u, bool clean = false) {
  ui = u;
  lastInput = millis();
  ringForState();
  showUi(clean);
}

void enterEnvelope() {
  ringLookFromSettings();
  notifyAt = lastRemind = millis();
  setUi(UI_ENVELOPE, true);
}

void goIdle(bool powerDown) {
  if (powerDown) {
    hapticShutdown();
    ui = UI_IDLE;
    choiceGlow = 0;
    ringSet(RING_POWERDOWN, cfg.color1);  // the ring goes out LED by LED
    showUi(true);
    return;
  }
  setUi(UI_IDLE, true);
}

void openMessage(int idx, bool saved, bool clean) {
  const Message &m = store[idx];
  curId = m.id;
  savedList = saved;
  curLines = layoutMessage(m.subject, m.body);
  scrollPos = 0;
  choice = 0;
  setUi(UI_READ, clean);
  logf("Reading %s message %lu from %s", saved ? "saved" : "new", (unsigned long)curId, m.from.c_str());
}

void nextOrFinish() {
  int i = savedList ? nextSavedAfter(curId) : firstNew();
  if (i >= 0) { openMessage(i, savedList, true); return; }
  happyAt = millis();
  setUi(UI_HAPPY);
}

// A message arrived (from Gmail or the web page).
void addMessage(const String &from, const String &subject, const String &body) {
  storeAdd(from, subject, body, nowText());
  logf("New message from %s: %s", from.c_str(), subject.c_str());
  hapticNotifyFromSettings();
  switch (ui) {
    case UI_IDLE:
    case UI_HAPPY: enterEnvelope(); break;
    case UI_ENVELOPE: notifyAt = lastRemind = millis(); ringSet(RING_NOTIFY); showUi(); break;  // new number on the badge
    case UI_READ:
    case UI_DECIDE: if (!savedList) showUi(); break;  // the "2/5" in the header grows
    case UI_SETUP: break;  // the envelope shows once setup is left
  }
}

// Subject "!led red" etc. Returns true if it was an instruction.
bool runInstruction(const String &subjectIn) {
  String s = subjectIn;
  s.trim();
  if (!s.startsWith("!")) return false;
  s = s.substring(1);
  String cmd = s, arg;
  int sp = s.indexOf(' ');
  if (sp > 0) { cmd = s.substring(0, sp); arg = s.substring(sp + 1); arg.trim(); }
  cmd.toLowerCase();
  logf("Instruction: %s %s", cmd.c_str(), arg.c_str());
  if (cmd == "led" || cmd == "color" || cmd == "colour") {
    ambientColor = parseColor(arg);
    ambientRainbow = false;
  } else if (cmd == "rainbow") {
    ambientRainbow = true;
  } else if (cmd == "buzz" || cmd == "vibrate") {
    hapticNotify(cfg.ntfTicks, cfg.ntfMsFirst, cfg.ntfMsLast, cfg.ntfPowFirst, cfg.ntfPowLast, cfg.ntfGap,
                 constrain((int)arg.toInt(), 1, 4));
  } else if (cmd == "ping") {
    ringTest(RING_NOTIFY, 0, 4000);
    hapticTick(true);
  } else if (cmd == "clear") {
    storeClear();
    if (ui != UI_SETUP) goIdle(false);
  } else {
    logf("Unknown instruction '%s', shown as a message", cmd.c_str());
    return false;
  }
  if (ui == UI_IDLE) ringForState();
  return true;
}

// ---------------------------------------------------------------- knob

void onTurn(int d) {
  lastInput = millis();
  switch (ui) {
    case UI_READ: {
      int ms = maxScroll(curLines);
      int s = constrain(scrollPos + d * SCROLL_LINES_PER_CLICK, 0, ms);
      if (s == scrollPos) { hapticBump(); return; }
      hapticTick();
      scrollPos = s;
      showUi();
      break;
    }
    case UI_DECIDE: {
      int8_t c = d < 0 ? -1 : 1;  // anticlockwise = delete, clockwise = save
      if (c == choice) { hapticBump(); return; }
      hapticTick();
      choice = c;
      choiceGlow = c;
      showUi();
      break;
    }
    default:
      hapticTick();
  }
}

void onPress() {
  lastInput = millis();
  pressCount++;
  switch (ui) {
    case UI_IDLE: {
      int n = firstNew();
      if (n >= 0) { hapticTick(true); sessionDone = 0; openMessage(n, false, true); break; }
      int s = nextSavedAfter(0);
      if (s >= 0) { hapticTick(true); openMessage(s, true, true); }
      else hapticNope();  // nothing to show
      break;
    }
    case UI_ENVELOPE: {
      hapticsStop();
      hapticTick(true);
      int n = firstNew();
      sessionDone = 0;
      if (n >= 0) openMessage(n, false, true);
      else goIdle(false);
      break;
    }
    case UI_READ:
      hapticTick(true);
      choice = 0;
      setUi(UI_DECIDE);
      break;
    case UI_DECIDE:
      if (!choice) { hapticNope(); return; }
      hapticConfirm(choice > 0);
      if (choice > 0) storeSetState(curId, MSG_SAVED);
      else storeDelete(curId);
      logf("Message %lu %s", (unsigned long)curId, choice > 0 ? "saved" : "deleted");
      if (!savedList) sessionDone++;
      choice = 0;
      choiceGlow = 0;
      nextOrFinish();
      break;
    case UI_HAPPY:
    case UI_SETUP:
      hapticTick(true);
      break;
  }
}

void startAP(bool byUser);

void knobLoop() {
  bool raw = digitalRead(PIN_ENC_SW) == LOW;
  if (raw != btnDown && millis() - btnChange > 25) {
    btnDown = raw;
    btnChange = millis();
    if (btnDown) {
      btnDownAt = millis();
      longFired = false;
    } else {
      holdFill = -1;
      if (!longFired) onPress();
    }
  }
  if (btnDown && !longFired) {
    uint32_t held = millis() - btnDownAt;
    if (held > 800) holdFill = (float)held / LONG_PRESS_MS;
    if (held >= LONG_PRESS_MS) {
      longFired = true;
      holdFill = -1;
      logf("Knob held 5 s: setup Wi-Fi");
      Pulse p = { 255, 300, 0 };
      playPulses(&p, 1);
      startAP(true);
    }
  }
  static int32_t lastPos = 0;
  int32_t pos = encPosition();
  if (pos != lastPos) {
    int d = pos - lastPos;
    lastPos = pos;
    onTurn(d);
  }
}

// ---------------------------------------------------------------- Gmail (its own task)

GmailImap imap;
SemaphoreHandle_t mailLock;
struct Incoming { String from, subject, body; };
std::vector<Incoming> inbox;  // filled by the mail task, emptied by loop()
volatile uint32_t lastMailCheck = 0;
volatile bool mailCheckedOnce = false, mailOk = false, mailCheckNow = false, mailBusy = false;
String mailError;

void checkMail() {
  lastMailCheck = millis();
  mailBusy = true;
  String err;
  bool ok = false;
  if (!cfg.gmailUser.length() || !cfg.gmailPass.length()) {
    err = "Gmail address or app password not set";
  } else {
    digitalWrite(PIN_BOARD_LED, LOW);
    uint32_t t0 = millis();
    if (imap.connect(cfg.gmailUser, cfg.gmailPass)) {
      uint32_t uids[10];
      int n = 0;
      ok = imap.searchUnseen(cfg.allowFrom, uids, 10, n);
      if (ok && n) logf("Gmail: %d unread", n);
      for (int i = 0; ok && i < n && i < 5; i++) {  // at most 5 per check, oldest first
        MailItem m;
        if (!imap.fetch(uids[i], m)) { ok = false; break; }
        logf("Mail %lu from %s <%s>: '%s', %d chars", (unsigned long)m.uid, m.fromName.c_str(),
             m.fromAddr.c_str(), m.subject.c_str(), m.body.length());
        xSemaphoreTake(mailLock, portMAX_DELAY);
        inbox.push_back({ m.fromName, m.subject, m.body });
        xSemaphoreGive(mailLock);
        imap.markSeen(m.uid);
      }
      if (!ok) err = imap.error;
      imap.logout();
      logf("Gmail check: %s, %lu ms", ok ? "OK" : err.c_str(), millis() - t0);
    } else {
      err = imap.error;
      logf("Gmail: %s", err.c_str());
    }
    digitalWrite(PIN_BOARD_LED, HIGH);
  }
  xSemaphoreTake(mailLock, portMAX_DELAY);
  mailError = err;
  xSemaphoreGive(mailLock);
  mailOk = ok;
  mailCheckedOnce = true;
  mailBusy = false;
}

void mailTaskFn(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    if (!WiFi.isConnected()) continue;
    if (!mailCheckNow && mailCheckedOnce && millis() - lastMailCheck < cfg.pollSec * 1000UL) continue;
    mailCheckNow = false;
    checkMail();
  }
}

void drainInbox() {
  std::vector<Incoming> got;
  xSemaphoreTake(mailLock, portMAX_DELAY);
  got.swap(inbox);
  xSemaphoreGive(mailLock);
  for (const Incoming &m : got)
    if (!runInstruction(m.subject)) addMessage(m.from, m.subject, m.body);
}

// ---------------------------------------------------------------- Wi-Fi

WebServer server(80);
DNSServer dns;
bool apByUser = false;
uint32_t staRetryAt = 0, apOnSince = 0;
bool mdnsUp = false, timeSet = false;

void beginSta() {
  if (!cfg.ssid.length()) return;
  logf("Joining Wi-Fi '%s'", cfg.ssid.c_str());
  WiFi.setHostname(cfg.name.c_str());
  WiFi.begin(cfg.ssid.c_str(), cfg.wifiPass.c_str());
}

void startAP(bool byUser) {
  if (!apOn) {
    WiFi.mode(WIFI_AP_STA);
    String pass = cfg.apPass.length() >= 8 ? cfg.apPass : String("messagebox");
    WiFi.softAP(AP_SSID, pass.c_str());
    delay(100);
    dns.start(53, "*", WiFi.softAPIP());
    apOn = true;
    apOnSince = millis();
    logf("Setup Wi-Fi '%s' on, open http://%s", AP_SSID, WiFi.softAPIP().toString().c_str());
  }
  if (byUser) apByUser = true;
  setUi(UI_SETUP);
}

// Back to what the box was doing before setup.
void resumeUi() {
  if (firstNew() >= 0) enterEnvelope();
  else setUi(UI_IDLE, true);
}

void stopAP() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
  apByUser = false;
  logf("Setup Wi-Fi off");
  if (ui == UI_SETUP) resumeUi();
}

const char *staStateText() {
  switch (WiFi.status()) {
    case WL_CONNECTED: return "connected";
    case WL_NO_SSID_AVAIL: return "network not found";
    case WL_CONNECT_FAILED: return "wrong password?";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED: return "trying";
    case WL_IDLE_STATUS: return "idle";
    default: return "not connected";
  }
}

void onConnected() {
  logf("Wi-Fi connected, IP %s, %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (!mdnsUp && MDNS.begin(cfg.name.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
  }
  if (!timeSet) {
    configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com");
    timeSet = true;
  }
  if (apOn && !apByUser) stopAP();
  mailCheckNow = true;
}

void wifiLoop() {
  static bool wasConnected = false;
  bool c = WiFi.isConnected();
  if (c && !wasConnected) onConnected();
  if (!c && wasConnected) {
    logf("Wi-Fi lost (%s)", staStateText());
    staRetryAt = millis() + 30000;  // auto-reconnect gets 30 s, then the setup Wi-Fi opens
  }
  wasConnected = c;
  if (c || !cfg.ssid.length()) return;
  if (!apOn && staRetryAt && (int32_t)(millis() - staRetryAt) > 0) {
    logf("Can't join '%s' (%s)", cfg.ssid.c_str(), staStateText());
    startAP(false);
    staRetryAt = millis() + 60000;
  }
  // Joining the home Wi-Fi moves the radio's channel and would drop a phone on the setup
  // Wi-Fi, so only retry while nobody is connected to it.
  if (apOn && (int32_t)(millis() - staRetryAt) > 0) {
    staRetryAt = millis() + 60000;
    if (WiFi.softAPgetStationNum() == 0) beginSta();
  }
}

// ---------------------------------------------------------------- web server

String jsonEsc(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

void sendText(const String &t) { server.send(200, "text/plain", t); }
String jb(bool b) { return b ? "true" : "false"; }

void handleStatus() {
  String j;
  j.reserve(3200);
  j += "{\"setup\":" + jb(apOn) + ",\"apSsid\":\"" + String(AP_SSID) + "\"";
  j += ",\"staOk\":" + jb(WiFi.isConnected());
  j += ",\"staSsid\":\"" + jsonEsc(WiFi.SSID()) + "\",\"staState\":\"" + staStateText() + "\"";
  j += ",\"ip\":\"" + (WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\"";
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"ui\":\"" + String(UI_NAMES[ui]) + "\"";
  j += ",\"enc\":" + String(encPosition()) + ",\"btn\":" + jb(btnDown) + ",\"presses\":" + String(pressCount);
  xSemaphoreTake(mailLock, portMAX_DELAY);
  String err = mailError;
  xSemaphoreGive(mailLock);
  j += ",\"mail\":{\"ok\":" + String(!mailCheckedOnce ? "null" : jb(mailOk)) + ",\"busy\":" + jb(mailBusy);
  j += ",\"error\":\"" + jsonEsc(err) + "\",\"ago\":" + String(mailCheckedOnce ? (long)((millis() - lastMailCheck) / 1000) : -1L) + "}";
  j += ",\"newCount\":" + String(countState(MSG_NEW)) + ",\"savedCount\":" + String(countState(MSG_SAVED));
  j += ",\"refreshMs\":" + String(lastRefreshMs) + ",\"refreshes\":" + String(refreshCount);
  String now = nowText();
  j += ",\"time\":\"" + jsonEsc(now.length() ? now : String("not set (needs internet)")) + "\"";
  j += ",\"uptime\":" + String(millis() / 1000) + ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"settings\":{\"ssid\":\"" + jsonEsc(cfg.ssid) + "\",\"hasWifiPass\":" + jb(cfg.wifiPass.length());
  j += ",\"guser\":\"" + jsonEsc(cfg.gmailUser) + "\",\"hasGmailPass\":" + jb(cfg.gmailPass.length());
  j += ",\"allow\":\"" + jsonEsc(cfg.allowFrom) + "\",\"poll\":" + String(cfg.pollSec);
  j += ",\"tz\":\"" + jsonEsc(cfg.tz) + "\",\"name\":\"" + jsonEsc(cfg.name) + "\"";
  j += ",\"lstyle\":" + String(cfg.ledStyle) + ",\"col1\":\"" + colorHex(cfg.color1) + "\",\"col2\":\"" + colorHex(cfg.color2) + "\"";
  j += ",\"lspeed\":" + String(cfg.ledSpeed) + ",\"ltail\":" + String(cfg.ledTail) + ",\"lmin\":" + String(cfg.ledMinutes);
  j += ",\"bright\":" + String(cfg.brightness);
  j += ",\"hknob\":" + jb(cfg.knobHaptics) + ",\"hpow\":" + String(cfg.tickPower) + ",\"hms\":" + String(cfg.tickMs);
  j += ",\"nvibe\":" + jb(cfg.notifyVibe) + ",\"nticks\":" + String(cfg.ntfTicks);
  j += ",\"nms0\":" + String(cfg.ntfMsFirst) + ",\"nms1\":" + String(cfg.ntfMsLast);
  j += ",\"npow0\":" + String(cfg.ntfPowFirst) + ",\"npow1\":" + String(cfg.ntfPowLast);
  j += ",\"ngap\":" + String(cfg.ntfGap) + ",\"nrem\":" + String(cfg.ntfRemindMin);
  j += ",\"rot\":" + String(cfg.rotation) + ",\"fullev\":" + String(cfg.fullEvery) + ",\"krev\":" + jb(cfg.knobReverse) + "}";
  j += ",\"log\":\"" + jsonEsc(logText()) + "\"}";
  server.send(200, "application/json", j);
}

void handleMessages() {
  String j = "[";
  for (int i = (int)store.size() - 1; i >= 0; i--) {  // newest first
    const Message &m = store[i];
    if (j.length() > 1) j += ",";
    j += "{\"id\":" + String(m.id) + ",\"state\":\"" + (m.state == MSG_NEW ? "new" : "saved") + "\"";
    j += ",\"from\":\"" + jsonEsc(m.from) + "\",\"subject\":\"" + jsonEsc(m.subject) + "\",\"when\":\"" + jsonEsc(m.when) + "\"";
    j += ",\"body\":\"" + jsonEsc(m.body.substring(0, 160)) + "\"}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

void handleScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    server.send(200, "application/json", "{\"busy\":true}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "{\"busy\":true}");
    return;
  }
  String j = "{\"busy\":false,\"nets\":[";
  int shown = 0;
  for (int i = 0; i < n && shown < 25; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int k = 0; k < i; k++) if (WiFi.SSID(k) == s) dup = true;
    if (dup) continue;
    if (shown++) j += ",";
    j += "{\"ssid\":\"" + jsonEsc(s) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"open\":" + jb(WiFi.encryptionType(i) == WIFI_AUTH_OPEN) + "}";
  }
  j += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", j);
}

bool rebootPending = false;
uint32_t rebootAt = 0;

String arg(const char *k) {
  String v = server.arg(k);
  v.trim();
  return v;
}
long argNum(const char *k, long lo, long hi, long def) {
  if (!server.hasArg(k) || !server.arg(k).length()) return def;
  return constrain(server.arg(k).toInt(), lo, hi);
}
bool argBool(const char *k, bool def) { return server.hasArg(k) ? server.arg(k) == "1" : def; }

// Light and buzz settings, from the form (used by Save and by the "Try" buttons).
void readLookArgs(Settings &s) {
  s.ledStyle = argNum("lstyle", 0, STYLE_COUNT - 1, s.ledStyle);
  if (server.hasArg("col1")) s.color1 = parseColor(arg("col1"));
  if (server.hasArg("col2")) s.color2 = parseColor(arg("col2"));
  s.ledSpeed = argNum("lspeed", 200, 10000, s.ledSpeed);
  s.ledTail = argNum("ltail", 1, NUM_LEDS, s.ledTail);
  s.brightness = argNum("bright", 5, 255, s.brightness);
  s.knobHaptics = argBool("hknob", s.knobHaptics);
  s.tickPower = argNum("hpow", 0, 255, s.tickPower);
  s.tickMs = argNum("hms", 3, 100, s.tickMs);
  s.notifyVibe = argBool("nvibe", s.notifyVibe);
  s.ntfTicks = argNum("nticks", 1, 12, s.ntfTicks);
  s.ntfMsFirst = argNum("nms0", 5, 1000, s.ntfMsFirst);
  s.ntfMsLast = argNum("nms1", 5, 1000, s.ntfMsLast);
  s.ntfPowFirst = argNum("npow0", 0, 255, s.ntfPowFirst);
  s.ntfPowLast = argNum("npow1", 0, 255, s.ntfPowLast);
  s.ntfGap = argNum("ngap", 30, 3000, s.ntfGap);
}

void handleSettings() {
  Settings old = cfg;
  cfg.ssid = arg("ssid");
  if (server.arg("wpass").length()) cfg.wifiPass = server.arg("wpass");
  cfg.gmailUser = arg("guser");
  if (server.arg("gpass").length()) cfg.gmailPass = arg("gpass");
  cfg.allowFrom = arg("allow");
  cfg.pollSec = argNum("poll", 20, 3600, cfg.pollSec);
  if (arg("tz").length()) cfg.tz = arg("tz");
  if (arg("name").length()) cfg.name = arg("name");
  if (server.arg("appass").length()) {
    if (server.arg("appass").length() < 8) { cfg = old; sendText("Setup Wi-Fi password needs 8+ characters. Nothing saved."); return; }
    cfg.apPass = server.arg("appass");
  }
  readLookArgs(cfg);
  cfg.ledMinutes = argNum("lmin", 0, 1440, cfg.ledMinutes);
  cfg.ntfRemindMin = argNum("nrem", 0, 1440, cfg.ntfRemindMin);
  cfg.rotation = argNum("rot", 0, 3, cfg.rotation);
  cfg.fullEvery = argNum("fullev", 1, 50, cfg.fullEvery);
  cfg.knobReverse = argBool("krev", cfg.knobReverse);
  saveSettings();
  bool restart = cfg.ssid != old.ssid || cfg.wifiPass != old.wifiPass || cfg.name != old.name ||
                 cfg.apPass != old.apPass || cfg.tz != old.tz || cfg.rotation != old.rotation;
  if (restart) {
    logf("Settings saved, restarting");
    sendText("Saved. Restarting - if the home Wi-Fi works, the setup Wi-Fi closes.");
    rebootPending = true;
    rebootAt = millis() + 1500;
  } else {
    logf("Settings saved");
    ringLookFromSettings();
    if (cfg.gmailUser != old.gmailUser || cfg.gmailPass != old.gmailPass || cfg.allowFrom != old.allowFrom) mailCheckNow = true;
    sendText("Saved");
  }
}

void testScreen(View v) {
  screenShow(v);
  testScreenUntil = millis() + 12000;  // then back to the real screen
}

void handleTest() {
  String what = server.arg("what");
  Settings t = cfg;  // the form's values, not yet saved
  readLookArgs(t);
  if (what == "lights") {
    ringLook.style = t.ledStyle;
    ringLook.c1 = t.color1;
    ringLook.c2 = t.color2;
    ringLook.speed = t.ledSpeed;
    ringLook.tail = t.ledTail;
    ringTest(RING_NOTIFY, 0, 8000);
    sendText("New-message lights for 8 seconds");
  } else if (what == "chase") {
    ringTest(RING_CHASE_TEST, 0, NUM_LEDS * 3 * 250 + 200);
    sendText("Each LED goes red, then green, then blue, in order");
  } else if (what == "solid") {
    ringTest(RING_SOLID, t.color1, 5000);
    sendText("Colour 1 for 5 seconds");
  } else if (what == "ledsoff") {
    ringTest(RING_OFF, 0, 3000);
    sendText("LEDs off for 3 seconds");
  } else if (what == "notify") {
    hapticNotify(t.ntfTicks, t.ntfMsFirst, t.ntfMsLast, t.ntfPowFirst, t.ntfPowLast, t.ntfGap);
    sendText("tick tick tick ... TICK");
  } else if (what == "tick") {
    Pulse p[4] = { { t.tickPower, t.tickMs, 180 }, { t.tickPower, t.tickMs, 180 }, { t.tickPower, t.tickMs, 180 },
                   { (uint8_t)min(255, t.tickPower + 40), (uint16_t)(t.tickMs * 2 + 10), 0 } };
    playPulses(p, 4);
    sendText("Three clicks and a press");
  } else if (what == "shutdown") {
    bool k = cfg.knobHaptics;
    cfg.knobHaptics = true;
    hapticShutdown();
    cfg.knobHaptics = k;
    sendText("Power-down buzz");
  } else if (what == "envelope") {
    View v; v.scr = SCR_ENVELOPE; v.count = argNum("count", 1, 99, 3);
    testScreen(v);
    sendText("Envelope (back to normal in 12 s)");
  } else if (what == "happy") {
    View v; v.scr = SCR_HAPPY;
    testScreen(v);
    sendText("Happy envelope (back to normal in 12 s)");
  } else if (what == "decide") {
    View v; v.scr = SCR_DECIDE; v.from = "Test"; v.title = "Thinking of you"; v.pos = 1; v.total = 2;
    v.choice = argNum("choice", -1, 1, 0);
    testScreen(v);
    sendText("Keep or delete screen");
  } else if (what == "epd") {
    View v; v.scr = SCR_TEXT; v.text = foldToAscii(server.arg("text"));
    testScreen(v);
    sendText("Text on the e-paper");
  } else if (what == "pattern") {
    View v; v.scr = SCR_PATTERN;
    testScreen(v);
    sendText("Test pattern (full refresh)");
  } else sendText("Unknown test");
}

void setupWeb() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/messages", HTTP_GET, handleMessages);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/message", HTTP_POST, [] {
    String from = foldToAscii(server.arg("from")), sub = foldToAscii(server.arg("subject")), body = foldToAscii(server.arg("body"));
    if (runInstruction(sub)) { sendText("Ran it as an instruction"); return; }
    addMessage(from.length() ? from : String("Test"), sub, body);
    sendText(ui == UI_SETUP ? "Saved as new. The envelope shows when you leave setup mode." : "Sent: watch the box");
  });
  server.on("/api/msgdel", HTTP_POST, [] {
    if (server.arg("id") == "all") storeClear();
    else storeDelete(strtoul(server.arg("id").c_str(), nullptr, 10));
    if (ui == UI_READ || ui == UI_DECIDE) { if (findMsg(curId) < 0) resumeUi(); }
    else if (ui == UI_ENVELOPE) { if (firstNew() < 0) goIdle(false); else showUi(); }
    sendText("Deleted");
  });
  server.on("/api/checkmail", HTTP_POST, [] {
    if (!WiFi.isConnected()) { sendText("Not on the home Wi-Fi yet, so Gmail can't be reached"); return; }
    mailCheckNow = true;
    sendText("Checking Gmail - watch the status and log");
  });
  server.on("/api/leavesetup", HTTP_POST, [] {
    if (!WiFi.isConnected()) { sendText("Stays on: the home Wi-Fi isn't connected"); return; }
    sendText("Setup Wi-Fi turning off. The page stays at http://" + WiFi.localIP().toString());
    stopAP();
  });
  server.on("/api/reboot", HTTP_POST, [] {
    sendText("Restarting...");
    rebootPending = true;
    rebootAt = millis() + 800;
  });
  server.on("/api/factory", HTTP_POST, [] {
    prefs.begin("mbox", false);
    prefs.clear();
    prefs.end();
    storeClear();
    sendText("Everything erased. Restarting into setup mode...");
    rebootPending = true;
    rebootAt = millis() + 800;
  });
  // Captive portal: phones probe these addresses; sending them to the page opens it by itself.
  server.onNotFound([] {
    if (apOn) {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/plain", "");
    } else server.send(404, "text/plain", "Not found");
  });
  server.begin();
}

// ---------------------------------------------------------------- setup and loop

void setup() {
  Serial.begin(115200);
  delay(300);
  logLock = xSemaphoreCreateMutex();
  mailLock = xSemaphoreCreateMutex();
  pinMode(PIN_BOARD_LED, OUTPUT);
  digitalWrite(PIN_BOARD_LED, HIGH);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  logf("MessageBox starting");
  loadSettings();
  hapticsBegin();
  ledsBegin();
  storeLoad();
  screenBegin();

  setupWeb();
  WiFi.persistent(false);
  xTaskCreate(mailTaskFn, "mail", 12288, nullptr, 1, nullptr);
  if (cfg.ssid.length()) {
    WiFi.mode(WIFI_STA);
    beginSta();
    staRetryAt = millis() + 20000;  // 20 s to join, else the setup Wi-Fi opens
    resumeUi();                     // envelope if there are new messages, else blank
  } else {
    logf("No Wi-Fi saved");
    startAP(false);
  }
}

void loop() {
  if (apOn) dns.processNextRequest();
  server.handleClient();
  knobLoop();
  hapticsLoop();
  ledsLoop();
  wifiLoop();
  drainInbox();

  uint32_t now = millis();
  switch (ui) {
    case UI_ENVELOPE:
      if (cfg.ledMinutes && ringMode == RING_NOTIFY && now - notifyAt > cfg.ledMinutes * 60000UL) ringSet(RING_OFF);
      if (cfg.ntfRemindMin && now - lastRemind > cfg.ntfRemindMin * 60000UL) {
        lastRemind = notifyAt = now;
        hapticNotifyFromSettings();
        ringSet(RING_NOTIFY);
      }
      break;
    case UI_READ:
    case UI_DECIDE:
      if (now - lastInput > READ_TIMEOUT_MS) {  // walked away: back to the envelope, or off
        logf("No knob use for a while");
        choice = 0;
        if (firstNew() >= 0) enterEnvelope();
        else goIdle(true);
      }
      break;
    case UI_HAPPY:
      if (now - happyAt > 3000) goIdle(true);  // shut down: brrr, ring out, screen blank
      break;
    default: break;
  }
  // the power-down animation has finished: bring back a "!led" glow, if one was set
  if (ui == UI_IDLE && ringMode == RING_OFF && !overrideUntil && (ambientColor || ambientRainbow)) ringForState();
  if (testScreenUntil && (int32_t)(now - testScreenUntil) > 0) {
    testScreenUntil = 0;
    showUi(true);
  }
  if (rebootPending && (int32_t)(now - rebootAt) > 0) ESP.restart();
  // Setup Wi-Fi opened by hand closes after 15 minutes if the home Wi-Fi works and nobody is on it.
  if (apOn && apByUser && WiFi.isConnected() && now - apOnSince > 15 * 60000UL && WiFi.softAPgetStationNum() == 0) stopAP();
  delay(2);
}
