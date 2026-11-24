/*
  MessageBox - main firmware v14
  ==========================
  A little box that shows messages from a friend or partner. They email a private Gmail
  inbox; the box reads it and shows an attached picture when available.

  How it behaves:
    Waiting       the screen is blank and the LED ring is off.
    New message   a hummingbird flies in and pecks the envelope open; the motor goes
                  tick tick tick tick tick TICK. The screen then stays on the open envelope with the
                  number of new messages, and the ring spins for a minute every ten minutes.
    Press         opens the oldest new message. Turn clockwise to scroll down, anticlockwise up.
                  Turn on past the end for the quick replies (reactions): pick one and press to
                  send it as a reply in the same email thread.
    Press         done with it (it's kept); the next new message opens. Hold 2 s: delete it.
    All read      a happy envelope, then the box powers down: the ring goes out LED by LED,
                  a fading brrr-rr-r, and the screen goes blank.
    Press when blank with nothing new: "No new messages" with a happy envelope and a light show.
                  Turn the knob then to flick through the saved messages (newest first); press to
                  open one.
    Hold 20 s     the settings screen: lights, buzz, bedtime (quiet hours), change Wi-Fi, shut down.
  Every click and press of the knob gives a small haptic tick (can be turned off).

  Board:  ESP32-C3 SuperMini (Arduino: "ESP32C3 Dev Module", USB CDC On Boot: Enabled,
          Partition Scheme: custom OTA (partitions.csv))
  Parts:  WeAct 3.7" e-paper 240x416 (GDEY037T03), WS2812B 12-LED ring, EC11/KY-040 rotary
          encoder with push button, PWM coin vibration motor module.
  Libraries (Library Manager): GxEPD2, Adafruit GFX Library, Adafruit NeoPixel.

  Wiring (see ../wiring.svg and ../README.md):
    E-paper  VCC->3V3  GND->GND  SCL->GPIO10  SDA->GPIO6  CS->GPIO7  D/C->GPIO5  RES->GPIO8  BUSY->GPIO4
             (or, with Screen wiring set to "not wired": RES->3V3, BUSY unconnected)
    Encoder  +->3V3 (not 5V!)  GND->GND  CLK(S1)->GPIO0  DT(S2)->GPIO1  SW(KEY)->GPIO21
    LED ring 5V->5V  GND->GND  DIN->330 ohm->GPIO2            (+ 470 uF across 5V/GND)
    Vibe     VCC->5V  GND->GND  IN->GPIO20
    Leave BOOT/GPIO9 and GPIO3 unconnected: the zip tie in the case goes over those two pads.

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
#include <esp_system.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <time.h>
#include <utility>
#include "config.h"
#include "EncoderCounter.h"
#include "imap.h"
#include "smtp.h"
#include "haptics.h"
#include "leds.h"
#include "store.h"
#include "screen.h"
#include "web_page.h"
#include "firmware_update.h"

FirmwareUpdater firmwareUpdater;
SemaphoreHandle_t networkOperationLock;

String nowText() {
  struct tm t;
  if (!getLocalTime(&t, 10)) return "";
  char b[24];
  strftime(b, sizeof(b), "%a %d %b %H:%M", &t);
  return b;
}

// ---------------------------------------------------------------- rotary encoder and button

EncoderCounter encoder;
volatile int32_t encoderClicks = 0;

// The polling decoder validated in the hardware tests (no interrupt, so flash writes can't
// crash it), run every 1 ms in its own task so clicks aren't missed while loop() is busy
// saving to flash, serving the web page or handing the screen a new picture.
void encoderTaskFn(void *) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    int8_t d = encoder.update((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
    if (d) encoderClicks = encoderClicks + d;
    vTaskDelayUntil(&last, 1);
  }
}

int32_t encPosition() {
  int32_t c = encoderClicks;
  return cfg.knobReverse ? -c : c;
}

// After a web update, the new firmware is only kept once it has run for 30 s with its web page
// up. If it crashes or is restarted before that, the bootloader goes back to the old firmware.
extern "C" bool verifyRollbackLater() { return true; }
bool firmwareConfirmed = false;
extern bool webStarted;

void confirmFirmware() {
  if (firmwareConfirmed || millis() < 30000 || !webStarted) return;
  firmwareConfirmed = true;
  esp_ota_img_states_t state;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    logf("Firmware update confirmed: it ran 30 s without problems");
  }
}

bool btnDown = false;
uint32_t btnChange = 0, btnDownAt = 0, pressCount = 0;
bool longFired = false;

// ---------------------------------------------------------------- what the box is doing

const char *const UI_NAMES[] = { "waiting (blank)", "new message envelope", "reading", "choosing a quick reply",
                                 "all read", "setup", "a message arriving", "no new messages",
                                 "looking through saved messages", "reply sent", "settings", "going to sleep", "updating" };
Ui ui = UI_IDLE;

uint32_t curId = 0;        // the message being read
bool savedList = false;    // reading a saved message (from the carousel) instead of new ones
int sessionDone = 0;       // new messages dealt with since the envelope was opened
std::vector<Line> curLines;
int scrollPos = 0;
std::shared_ptr<const MailImage> curPicture;
bool showingPicture = false, pictureOnly = false;
int8_t reactSel = -1;      // the reaction picked on the reply screen (-1: none yet)
uint32_t lastInput = 0, happyAt = 0, notifyAt = 0, lastRemind = 0, sentAt = 0, nomsgAt = 0;
int carouselPos = 0;       // saved messages, newest first
int arriveFrame = 0;
uint32_t arriveShownAt = 0, arriveRefreshes = 0;
int menuSel = 0;
bool menuEditing = false;
uint32_t sleepAt = 0, sleepRefreshes = 0;
uint32_t ambientColor = 0; // "!led" colour shown while waiting (0 = off)
bool ambientRainbow = false;
uint32_t testScreenUntil = 0;
bool screenTestActive = false;

bool apOn = false;

// Quick replies: loop() queues them, the mail task sends them and hands back the result.
SemaphoreHandle_t mailLock;
std::vector<ReplyJob> outbox;
struct ReplyResult { uint32_t msgId; bool ok; bool giveUp; String error; };
std::vector<ReplyResult> replyResults;
volatile bool replyNow = false;

// Updates: the mail task reads the update file (firmware/latest.txt in the repo); loop() decides.
struct UpdateCheck { bool ok = false; int version = 0; String url, error; };
UpdateCheck updResult;                   // guarded by mailLock
volatile bool updCheckRequest = false, updResultReady = false;
bool updManual = false;                  // asked for on the box or the web page: show the result
bool updInstalling = false;
String updPendingUrl;                    // a newer version found by the daily check
int updPendingVersion = 0, updLastPercent = -1;
uint32_t updNextAuto = 0, updShownAt = 0;
String updTitle, updDetail;
int updPercent = -1;

// ---------------------------------------------------------------- helpers

// Bedtime: no lights or buzz for new messages (only once the clock is set from the internet).
bool quietNow() {
  if (!cfg.bedtime || cfg.bedFrom == cfg.bedTo) return false;
  struct tm t;
  if (!getLocalTime(&t, 5)) return false;
  int m = t.tm_hour * 60 + t.tm_min;
  return cfg.bedFrom < cfg.bedTo ? (m >= cfg.bedFrom && m < cfg.bedTo) : (m >= cfg.bedFrom || m < cfg.bedTo);
}
bool notifyLightsAllowed() { return cfg.ledsOn && !quietNow(); }
void notifyBuzz() { if (!quietNow()) hapticNotifyFromSettings(); }

String shortDate(const Message &m) {
  if (!m.ts) return m.when;
  time_t t = m.ts;
  struct tm tmv;
  localtime_r(&t, &tmv);
  char b[12];
  strftime(b, sizeof(b), "%d/%m/%y", &tmv);
  return b;
}

String senderName(const Message &m) { return displayName(m.meta.fromAddr, m.from); }

int firstNew() {
  for (int i = 0; i < (int)store.size(); i++) if (store[i].state == MSG_NEW) return i;
  return -1;
}

// Store indexes of the saved messages, newest first (the carousel's order).
std::vector<int> savedIdx() {
  std::vector<int> v;
  for (int i = (int)store.size() - 1; i >= 0; i--) if (store[i].state == MSG_SAVED) v.push_back(i);
  return v;
}

// While a message waits: spin for spinSec every spinEveryMin minutes (from when it arrived).
bool envelopeSpinning(uint32_t now) {
  if (!notifyLightsAllowed() || !cfg.spinSec) return false;
  if (!cfg.spinEveryMin) return true;
  return (now - notifyAt) % (cfg.spinEveryMin * 60000UL) < cfg.spinSec * 1000UL;
}

void ringForState() {
  switch (ui) {
    case UI_IDLE:
      if (ambientRainbow) ringSet(RING_RAINBOW);
      else ringSet(ambientColor ? RING_SOLID : RING_OFF, ambientColor);
      break;
    case UI_ENVELOPE: ringSet(envelopeSpinning(millis()) ? RING_NOTIFY : RING_OFF); break;
    case UI_ARRIVE: ringSet(notifyLightsAllowed() ? RING_NOTIFY : RING_OFF); break;
    case UI_READ:
    case UI_REACT: ringSet(RING_DIM, cfg.color1); break;
    case UI_SENT:
    case UI_HAPPY: ringSet(RING_RAINBOW); break;
    case UI_CAROUSEL: ringSet(RING_DIM, cfg.color2); break;
    case UI_SETUP: ringSet(RING_SOLID, 0x0040FF); break;
    case UI_MENU: ringSet(RING_DIM, 0xFFFFFF); break;
    case UI_UPDATE: ringSet(RING_DIM, 0x0040FF); break;
    case UI_NOMSG:   // the party animation was started on the way in
    case UI_SLEEP: break;
  }
  choiceGlow = 0;
}

String setupText() {
  return "1. On your phone, join the Wi-Fi\n    " + String(AP_SSID) + "\n    password: " + cfg.apPass +
         "\n2. Open http://192.168.4.1\n3. Test the parts, then enter your Wi-Fi and Gmail details.\n\n" +
         (cfg.ssid.length() ? "Saved Wi-Fi: " + cfg.ssid + " (retrying)" : String("No Wi-Fi saved yet"));
}

enum MenuId : uint8_t { MI_LIGHTS, MI_BUZZ, MI_BED, MI_FROM, MI_TO, MI_AUTOUPD, MI_CHECKUPD, MI_WIFI, MI_SLEEP, MI_EXIT,
                        MI_COUNT };

std::vector<MenuItem> menuItems() {
  auto onOff = [](bool b) { return String(b ? "On" : "Off"); };
  return { { "Lights", onOff(cfg.ledsOn) }, { "Buzz for new messages", onOff(cfg.notifyVibe) },
           { "Bedtime (quiet)", onOff(cfg.bedtime) }, { "Quiet from", hhmm(cfg.bedFrom) },
           { "Quiet until", hhmm(cfg.bedTo) }, { "Automatic updates", onOff(cfg.autoUpdate) },
           { "Check for updates", "v" + String(firmwareNumber(FIRMWARE_VERSION)) },
           { "Change Wi-Fi", "" }, { "Shut down", "" }, { "Exit", "" } };
}

// Sends the screen for the current state to the drawing task.
void showUi(bool forceFull = false) {
  if (screenTestActive) return;
  View v;
  switch (ui) {
    case UI_IDLE: v.scr = SCR_BLANK; break;
    case UI_ENVELOPE: v.scr = SCR_ENVELOPE; v.count = countState(MSG_NEW); break;
    case UI_ARRIVE: v.scr = SCR_ARRIVE; v.frame = arriveFrame; break;
    case UI_HAPPY: v.scr = SCR_HAPPY; break;
    case UI_SETUP: v.scr = SCR_SETUP; v.text = setupText(); break;
    case UI_NOMSG: v.scr = SCR_NOMSG; v.savedCount = countState(MSG_SAVED); break;
    case UI_MENU: v.scr = SCR_MENU; v.menu = menuItems(); v.menuSel = menuSel; v.menuEditing = menuEditing; break;
    case UI_SLEEP: v.scr = SCR_SLEEP; break;
    case UI_UPDATE: v.scr = SCR_UPDATE; v.title = updTitle; v.text = updDetail; v.pos = updPercent; break;
    case UI_CAROUSEL: {
      std::vector<int> s = savedIdx();
      if (s.empty()) { v.scr = SCR_BLANK; break; }
      carouselPos = constrain(carouselPos, 0, (int)s.size() - 1);
      const Message &m = store[s[carouselPos]];
      v.scr = SCR_CAROUSEL;
      v.from = senderName(m);
      v.when = shortDate(m);
      v.title = m.subject.length() && m.subject != "(no subject)" ? m.subject : m.body.substring(0, 60);
      v.pos = carouselPos + 1;
      v.total = s.size();
      v.sentReaction = m.replyState == REPLY_SENT || m.replyState == REPLY_PENDING ? m.reaction : -1;
      break;
    }
    case UI_READ:
    case UI_REACT:
    case UI_SENT: {
      int i = findMsg(curId);
      if (i < 0) { v.scr = SCR_BLANK; break; }
      const Message &m = store[i];
      v.scr = ui == UI_SENT ? SCR_SENT : ui == UI_REACT ? SCR_REACT : (showingPicture ? SCR_PICTURE : SCR_READ);
      v.from = senderName(m);
      v.when = m.when;
      v.title = m.subject.length() ? m.subject : (m.body.length() ? m.body : String("(empty)"));
      v.savedList = savedList;
      if (savedList) {
        std::vector<int> s = savedIdx();
        v.total = s.size();
        for (int k = 0; k < (int)s.size(); k++) if (s[k] == i) v.pos = k + 1;
      } else {
        v.pos = sessionDone + 1;
        v.total = sessionDone + countState(MSG_NEW);
      }
      v.sentReaction = m.replyState == REPLY_SENT || m.replyState == REPLY_PENDING ? m.reaction : -1;
      if (ui == UI_READ) {
        if (!showingPicture) v.lines = curLines;
        v.scroll = scrollPos;
        v.picture = curPicture;
        v.pictureOnly = pictureOnly;
      } else if (ui == UI_REACT) {
        v.reaction = reactSel;
      } else {
        v.reaction = m.reaction;
        v.replyQueued = m.replyState == REPLY_PENDING && !WiFi.isConnected();
      }
      break;
    }
  }
  screenShow(v, forceFull);
}

void setUi(Ui u) {
  if (u != UI_READ && u != UI_REACT && u != UI_SENT) curPicture.reset();
  ui = u;
  lastInput = millis();
  ringForState();
  showUi();
}

void enterEnvelope() {
  ringLookFromSettings();
  setUi(UI_ENVELOPE);
}

void goIdle(bool powerDown) {
  if (powerDown) {
    curPicture.reset();
    hapticShutdown();
    ui = UI_IDLE;
    choiceGlow = 0;
    if (cfg.ledsOn) ringSet(RING_POWERDOWN, cfg.color1);  // the ring goes out LED by LED
    else ringSet(RING_OFF);
    showUi();
    return;
  }
  setUi(UI_IDLE);
}

// Back to what the box should show when nobody is using it.
void resumeUi() {
  if (firstNew() >= 0) enterEnvelope();
  else setUi(UI_IDLE);
}

void openMessage(int idx, bool saved) {
  const Message &m = store[idx];
  curId = m.id;
  savedList = saved;
  curPicture = m.hasPicture ? storeLoadPicture(m.id) : nullptr;
  if (m.hasPicture && !curPicture) logf("Store: could not read the picture for message %lu", (unsigned long)m.id);
  pictureOnly = curPicture && !m.body.length() && (!m.subject.length() || m.subject == "(no subject)");
  showingPicture = pictureOnly;
  curLines = pictureOnly ? std::vector<Line>() : layoutMessage(m.subject, m.body);
  scrollPos = 0;
  reactSel = -1;
  setUi(UI_READ);
  logf("Reading %s message %lu from %s", saved ? "saved" : "new", (unsigned long)curId, m.from.c_str());
}

// After a message: the next new one, or "All read!"; from the carousel, back to the carousel.
void afterMessage(int carouselKeep) {
  if (savedList) {
    if (firstNew() >= 0) { enterEnvelope(); return; }  // something new came in meanwhile
    std::vector<int> s = savedIdx();
    if (s.empty()) { goIdle(false); return; }
    carouselPos = constrain(carouselKeep, 0, (int)s.size() - 1);
    setUi(UI_CAROUSEL);
    return;
  }
  int n = firstNew();
  if (n >= 0) { openMessage(n, false); return; }
  happyAt = millis();
  setUi(UI_HAPPY);
}

int carouselIndexOf(uint32_t id) {
  std::vector<int> s = savedIdx();
  for (int k = 0; k < (int)s.size(); k++) if (store[s[k]].id == id) return k;
  return carouselPos;
}

// Press while reading: done with it. A new message is kept (it joins the saved ones).
void exitMessage() {
  int i = findMsg(curId);
  if (i >= 0 && store[i].state == MSG_NEW) {
    storeSetState(curId, MSG_SAVED);
    sessionDone++;
  }
  afterMessage(carouselIndexOf(curId));
}

// Hold while reading: delete it.
void deleteCurrent() {
  int i = findMsg(curId);
  if (i < 0) return;
  const bool wasNew = store[i].state == MSG_NEW;
  const int keep = savedList ? carouselIndexOf(curId) : 0;
  storeDelete(curId);
  logf("Message %lu deleted", (unsigned long)curId);
  Pulse p[1] = { { 255, 160, 0 } };  // one firm thud
  playPulses(p, 1);
  if (wasNew) sessionDone++;
  afterMessage(keep);
}

void queueReply(const Message &m) {
  ReplyJob job;
  job.msgId = m.id;
  job.reaction = m.reaction;
  job.to = m.meta.replyTo.length() ? m.meta.replyTo : m.meta.fromAddr;
  job.subject = m.meta.rawSubject.length() ? m.meta.rawSubject : (m.subject == "(no subject)" ? String("") : m.subject);
  job.inReplyTo = m.meta.msgId;
  job.refs = m.meta.refs;
  job.toName = m.from;
  xSemaphoreTake(mailLock, portMAX_DELAY);
  for (size_t k = 0; k < outbox.size(); k++)
    if (outbox[k].msgId == job.msgId) { outbox.erase(outbox.begin() + k); break; }  // a newer reaction replaces it
  outbox.push_back(job);
  xSemaphoreGive(mailLock);
  replyNow = true;
}

// Press on the reply screen: send the reaction as a threaded email reply.
void sendReaction() {
  int i = findMsg(curId);
  if (i < 0) return;
  const Message &m = store[i];
  if (!(m.meta.replyTo.length() || m.meta.fromAddr.length())) {
    logf("No email address to reply to (a test message?)");
    hapticNope();
    return;
  }
  storeSetReply(curId, reactSel, REPLY_PENDING);
  queueReply(store[findMsg(curId)]);
  logf("Quick reply '%s' to %s queued", REACTIONS[reactSel].label, senderName(m).c_str());
  hapticConfirm(true);
  sentAt = millis();
  setUi(UI_SENT);
}

void showArriveFrame() {
  arriveRefreshes = refreshCount;
  arriveShownAt = millis();
  showUi();
}

// A message came in: the hummingbird brings it.
void startArrival() {
  arriveFrame = 0;
  ringLookFromSettings();
  ui = UI_ARRIVE;
  lastInput = millis();
  ringForState();
  showArriveFrame();
}

void enterNoMsg() {
  nomsgAt = millis();
  setUi(UI_NOMSG);
  if (cfg.ledsOn) ringSet(RING_PARTY);
  Pulse p[3] = { { 140, 25, 90 }, { 170, 25, 90 }, { 220, 45, 0 } };  // a happy little da-da-DA
  if (cfg.knobHaptics) playPulses(p, 3);
}

void openMenu() {
  menuSel = 0;
  menuEditing = false;
  logf("Knob held 20 s: settings");
  Pulse p = { 255, 300, 0 };
  playPulses(&p, 1);
  setUi(UI_MENU);
}

void enterSleep() {
  logf("Shutting down (turn the knob to wake)");
  ui = UI_SLEEP;
  lastInput = sleepAt = millis();
  sleepRefreshes = refreshCount;
  hapticShutdown();
  ringSet(cfg.ledsOn ? RING_POWERDOWN : RING_OFF, cfg.color1);
  showUi();
}

// Deep sleep until the knob is turned (GPIO0, the encoder's CLK, changes level). The screen keeps
// showing "Sleeping" without power. The button can't wake it: GPIO21 can't wake an ESP32-C3.
void goToSleep() {
  ring.clear();
  ring.show();
  motorWrite(0);
  pinMode(PIN_VIBE, OUTPUT);
  digitalWrite(PIN_VIBE, LOW);
  gpio_hold_en((gpio_num_t)PIN_VIBE);  // keep the motor off while asleep
  gpio_deep_sleep_hold_en();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  gpio_pullup_en((gpio_num_t)PIN_ENC_A);
  gpio_pulldown_dis((gpio_num_t)PIN_ENC_A);
  const int level = digitalRead(PIN_ENC_A);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_ENC_A, level ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH);
  Serial.flush();
  esp_deep_sleep_start();
}

void startAP(bool byUser);

void showUpdate(const String &title, const String &detail, int percent = -1) {
  updTitle = title;
  updDetail = detail;
  updPercent = percent;
  updShownAt = millis();
  if (ui != UI_UPDATE) setUi(UI_UPDATE);
  else showUi();
}

// Check the repo for a newer version now (from the box's settings or the web page).
void requestUpdateCheck(bool showOnBox) {
  if (firmwareUpdater.active()) return;
  if (!WiFi.isConnected()) {
    if (showOnBox) showUpdate("Couldn't check", "The box isn't on the home Wi-Fi.");
    return;
  }
  updManual = true;
  updCheckRequest = true;
  if (showOnBox || ui == UI_IDLE) showUpdate("Checking for updates", "Looking at the MessageBox repo on GitHub");
}

void startInstall(const String &url, int version) {
  String err;
  if (!firmwareUpdater.start(url, err)) {
    logf("Update to v%d not started: %s", version, err.c_str());
    if (ui == UI_UPDATE) showUpdate("Update didn't start", err);
    return;
  }
  logf("Installing update v%d from %s", version, url.c_str());
  updInstalling = true;
  updPendingUrl = "";
  updLastPercent = -1;
  showUpdate("Updating to v" + String(version), "Downloading. Don't unplug the box.", 0);
}

// What the daily or manual check found.
void handleUpdateResult() {
  if (!updResultReady) return;
  updResultReady = false;
  xSemaphoreTake(mailLock, portMAX_DELAY);
  UpdateCheck c = updResult;
  xSemaphoreGive(mailLock);
  const int current = firmwareNumber(FIRMWARE_VERSION);
  const bool newer = c.ok && c.version > current;
  if (c.ok) logf("Update check: latest v%d, this is v%d", c.version, current);
  else logf("Update check failed: %s", c.error.c_str());
  if (updManual) {
    updManual = false;
    if (newer) startInstall(c.url, c.version);
    else if (ui == UI_UPDATE) showUpdate(c.ok ? "Up to date" : "Couldn't check",
                                         c.ok ? "This box has v" + String(current) + ", the latest." : c.error);
  } else if (newer) {
    updPendingUrl = c.url;
    updPendingVersion = c.version;
  }
}

// Keeps the update screen in step with the download; installs a found update while idle.
void updateLoop(uint32_t now) {
  handleUpdateResult();
  // an update started from the web page shows on the box too
  if (firmwareUpdater.active() && !updInstalling && ui != UI_SETUP) {
    updInstalling = true;
    updLastPercent = -1;
    showUpdate("Updating", "Downloading. Don't unplug the box.", 0);
  }
  // the daily check found a newer version: install it once nobody has used the box for a minute
  if (updPendingUrl.length() && cfg.autoUpdate && !updInstalling && (ui == UI_IDLE || ui == UI_ENVELOPE) &&
      now - lastInput > 60000)
    startInstall(updPendingUrl, updPendingVersion);
  if (updManual && !updInstalling && now - updShownAt > 60000) {  // the check never answered
    updManual = updCheckRequest = false;
    if (ui == UI_UPDATE) showUpdate("Couldn't check", "No answer from GitHub. Try again later.");
  }
  if (ui != UI_UPDATE) return;
  if (updInstalling) {
    FirmwareUpdateStatus st = firmwareUpdater.status();
    if (st.restarting) {
      if (updPercent != 100) showUpdate(updTitle, "Installed. Restarting...", 100);
    } else if (st.busy) {
      int pct = st.total ? (int)((uint64_t)st.written * 100 / st.total) : 0;
      if (pct / 20 != updLastPercent / 20) {  // a screen refresh every 20 %
        updLastPercent = pct;
        showUpdate(updTitle, "Downloading. Don't unplug the box.", pct);
      }
    } else {
      updInstalling = false;
      showUpdate("Update failed", st.message + " The box keeps its current version.");
    }
  } else if (!updCheckRequest && !updManual && now - updShownAt > 5000) {
    resumeUi();  // "up to date" or an error has been shown long enough
  }
}

void menuPress() {
  switch (menuSel) {
    case MI_LIGHTS: cfg.ledsOn = !cfg.ledsOn; saveSettings(); break;
    case MI_BUZZ:
      cfg.notifyVibe = !cfg.notifyVibe;
      saveSettings();
      if (cfg.notifyVibe) hapticNotifyFromSettings();  // a taste of it
      break;
    case MI_BED: cfg.bedtime = !cfg.bedtime; saveSettings(); break;
    case MI_FROM:
    case MI_TO:
      menuEditing = !menuEditing;
      if (!menuEditing) saveSettings();
      break;
    case MI_AUTOUPD: cfg.autoUpdate = !cfg.autoUpdate; saveSettings(); break;
    case MI_CHECKUPD: hapticTick(true); requestUpdateCheck(true); return;
    case MI_WIFI: startAP(true); return;
    case MI_SLEEP: enterSleep(); return;
    case MI_EXIT: resumeUi(); return;
  }
  hapticTick(true);
  ringForState();
  showUi();
}

// A message arrived (from Gmail or the web page).
void addMessage(const String &from, const String &subject, const String &body, const MailImage *picture = nullptr,
                const MsgMeta *meta = nullptr) {
  storeAdd(from, subject, body, nowText(), picture, meta, time(nullptr) > 1700000000 ? (uint32_t)time(nullptr) : 0);
  logf("New message from %s: %s%s", from.c_str(), subject.c_str(), picture ? " (with picture)" : "");
  notifyAt = lastRemind = millis();
  notifyBuzz();
  switch (ui) {
    case UI_IDLE:
    case UI_HAPPY:
    case UI_NOMSG:
    case UI_CAROUSEL:
    case UI_ENVELOPE: startArrival(); break;
    case UI_ARRIVE: break;  // the count is right when it lands
    case UI_READ:
    case UI_REACT:
    case UI_SENT: if (!savedList) showUi(); break;  // the "2/5" in the header grows
    case UI_SETUP:
    case UI_MENU:
    case UI_UPDATE:
    case UI_SLEEP: break;  // the envelope shows afterwards
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
    if (ui != UI_SETUP && ui != UI_MENU) goIdle(false);
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
    case UI_NOMSG:
      if (countState(MSG_SAVED) > 0) { hapticTick(); carouselPos = 0; setUi(UI_CAROUSEL); }
      else hapticBump();
      break;
    case UI_CAROUSEL: {
      int n = countState(MSG_SAVED);
      int p = constrain(carouselPos + d, 0, max(0, n - 1));
      if (p == carouselPos) { hapticBump(); break; }
      hapticTick();
      carouselPos = p;
      showUi();
      break;
    }
    case UI_READ: {
      if (showingPicture) {
        if (d > 0) { hapticTick(); reactSel = -1; setUi(UI_REACT); break; }  // past the picture: reply
        if (pictureOnly) { hapticBump(); break; }
        showingPicture = false;
        hapticTick();
        showUi();
        break;
      }
      int ms = maxScroll(curLines);
      if (d > 0 && scrollPos == ms) {
        hapticTick();
        if (curPicture) { showingPicture = true; showUi(); }
        else { reactSel = -1; setUi(UI_REACT); }
        break;
      }
      int s = constrain(scrollPos + d * SCROLL_LINES_PER_CLICK, 0, ms);
      if (s == scrollPos) { hapticBump(); return; }
      hapticTick();
      scrollPos = s;
      showUi();
      break;
    }
    case UI_REACT: {
      int s = reactSel + d;
      if (s < -1) {  // back to the message, where it was left
        hapticTick();
        showingPicture = curPicture != nullptr;
        scrollPos = maxScroll(curLines);
        setUi(UI_READ);
        break;
      }
      s = min(s, REACTION_COUNT - 1);
      if (s == reactSel) { hapticBump(); break; }
      hapticTick();
      reactSel = s;
      showUi();
      break;
    }
    case UI_MENU:
      if (menuEditing) {
        uint16_t &t = menuSel == MI_FROM ? cfg.bedFrom : cfg.bedTo;
        t = (uint16_t)(((int)t + d * 30 + 1440 * 4) % 1440);
        hapticTick();
        showUi();
      } else {
        int s = constrain(menuSel + d, 0, MI_COUNT - 1);
        if (s == menuSel) { hapticBump(); break; }
        hapticTick();
        menuSel = s;
        showUi();
      }
      break;
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
      if (n >= 0) { hapticTick(true); sessionDone = 0; openMessage(n, false); }
      else enterNoMsg();
      break;
    }
    case UI_ARRIVE:
    case UI_ENVELOPE: {
      hapticsStop();
      hapticTick(true);
      int n = firstNew();
      sessionDone = 0;
      if (n >= 0) openMessage(n, false);
      else goIdle(false);
      break;
    }
    case UI_NOMSG:
      hapticTick(true);
      goIdle(false);
      break;
    case UI_CAROUSEL: {
      std::vector<int> s = savedIdx();
      if (carouselPos < 0 || carouselPos >= (int)s.size()) { hapticNope(); break; }
      hapticTick(true);
      openMessage(s[carouselPos], true);
      break;
    }
    case UI_READ:
      hapticTick(true);
      exitMessage();
      break;
    case UI_REACT:
      if (reactSel < 0) { hapticTick(true); exitMessage(); }
      else sendReaction();
      break;
    case UI_SENT:
      hapticTick(true);
      exitMessage();
      break;
    case UI_MENU:
      menuPress();
      break;
    case UI_HAPPY:
    case UI_SETUP:
      hapticTick(true);
      break;
    case UI_SLEEP:
      break;
    case UI_UPDATE:
      if (!updInstalling && !updManual) { hapticTick(true); resumeUi(); }
      break;
  }
}

// How long a hold does something here: 2 s deletes the message being read; 20 s opens settings.
uint32_t holdNeeded() {
  if (ui == UI_READ || ui == UI_REACT) return DELETE_HOLD_MS;
  if (ui == UI_MENU || ui == UI_SLEEP || ui == UI_SENT || ui == UI_SETUP || ui == UI_UPDATE) return 0;
  return MENU_HOLD_MS;
}

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
      // a press is a short press: letting go of a hold that didn't finish just cancels it
      if (!longFired && millis() - btnDownAt < 800) onPress();
    }
  }
  if (btnDown && !longFired) {
    const uint32_t held = millis() - btnDownAt, need = holdNeeded();
    if (need) {
      const bool del = need == DELETE_HOLD_MS;
      const uint32_t showFrom = del ? 400 : 1500;  // a normal press never lights the ring
      if (held > showFrom) {
        holdColor = del ? 0xFF1010 : 0xFFFFFF;
        holdFill = (float)(held - showFrom) / (need - showFrom);
      }
      if (held >= need) {
        longFired = true;
        holdFill = -1;
        lastInput = millis();
        if (del) deleteCurrent();
        else openMenu();
      }
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
struct Incoming { String from, subject, body; MailImage picture; MsgMeta meta; };
std::vector<Incoming> inbox;  // filled by the mail task, emptied by loop()
volatile uint32_t lastMailCheck = 0;
volatile bool mailCheckedOnce = false, mailOk = false, mailCheckNow = false, mailBusy = false;
String mailError;

void checkMail() {
  xSemaphoreTake(networkOperationLock, portMAX_DELAY);
  if (firmwareUpdater.active()) { xSemaphoreGive(networkOperationLock); return; }
  xSemaphoreTake(mailLock, portMAX_DELAY);
  const String user = cfg.gmailUser, password = cfg.gmailPass, allowFrom = cfg.allowFrom;
  xSemaphoreGive(mailLock);
  lastMailCheck = millis();
  mailBusy = true;
  String err;
  bool ok = false;
  if (!user.length() || !password.length()) {
    err = "Gmail address or app password not set";
  } else {
    uint32_t t0 = millis();
    if (imap.connect(user, password)) {
      uint32_t uids[10];
      int n = 0;
      ok = imap.searchUnseen(allowFrom, uids, 10, n);
      if (ok && n) logf("Gmail: %d unread", n);
      for (int i = 0; ok && i < n && i < 5; i++) {  // at most 5 per check, oldest first
        MailItem m;
        if (!imap.fetch(uids[i], m)) { ok = false; break; }
        logf("Mail %lu from %s <%s>: '%s', %d chars", (unsigned long)m.uid, m.fromName.c_str(),
             m.fromAddr.c_str(), m.subject.c_str(), m.body.length());
        xSemaphoreTake(mailLock, portMAX_DELAY);
        MsgMeta meta{ m.fromAddr, m.replyTo, m.msgId, m.refs, m.rawSubject };
        inbox.push_back({ std::move(m.fromName), std::move(m.subject), std::move(m.body), std::move(m.picture), meta });
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
  }
  xSemaphoreTake(mailLock, portMAX_DELAY);
  mailError = err;
  xSemaphoreGive(mailLock);
  mailOk = ok;
  mailCheckedOnce = true;
  mailBusy = false;
  xSemaphoreGive(networkOperationLock);
}

GmailSmtp smtp;

// Sends the oldest queued quick reply. Transient failures are retried (3 tries, a minute apart).
void sendOneReply() {
  xSemaphoreTake(networkOperationLock, portMAX_DELAY);
  if (firmwareUpdater.active()) { xSemaphoreGive(networkOperationLock); return; }
  xSemaphoreTake(mailLock, portMAX_DELAY);
  int pick = -1;
  for (size_t k = 0; k < outbox.size(); k++)
    if ((int32_t)(millis() - outbox[k].notBefore) >= 0) { pick = k; break; }
  if (pick < 0) { xSemaphoreGive(mailLock); xSemaphoreGive(networkOperationLock); return; }
  ReplyJob job = outbox[pick];
  const String user = cfg.gmailUser, password = cfg.gmailPass;
  xSemaphoreGive(mailLock);
  bool ok = false;
  String err;
  if (!user.length() || !password.length()) err = "Gmail address or app password not set";
  else {
    uint32_t t0 = millis();
    ok = smtp.send(user, password, job);
    err = smtp.error;
    logf("Quick reply to %s: %s, %lu ms", job.to.c_str(), ok ? "sent" : err.c_str(), millis() - t0);
  }
  const bool giveUp = !ok && (++job.tries >= 3 || err.startsWith("Login refused") || err.startsWith("no address"));
  xSemaphoreTake(mailLock, portMAX_DELAY);
  for (size_t k = 0; k < outbox.size(); k++) {
    if (outbox[k].msgId != job.msgId) continue;
    if (outbox[k].reaction == job.reaction) {  // not replaced by a newer reaction meanwhile
      if (ok || giveUp) outbox.erase(outbox.begin() + k);
      else { outbox[k].tries = job.tries; outbox[k].notBefore = millis() + 60000; }
    }
    break;
  }
  if (ok || giveUp) replyResults.push_back({ job.msgId, ok, giveUp, err });
  xSemaphoreGive(mailLock);
  xSemaphoreGive(networkOperationLock);
}

bool replyWaiting() {
  xSemaphoreTake(mailLock, portMAX_DELAY);
  bool any = false;
  for (const ReplyJob &j : outbox) if ((int32_t)(millis() - j.notBefore) >= 0) any = true;
  xSemaphoreGive(mailLock);
  return any;
}

// Reads the update file: "version=15" and "file=MessageBox-main-v15-OTA.bin" (or "url=...").
void checkForUpdate() {
  xSemaphoreTake(mailLock, portMAX_DELAY);
  const String manifest = cfg.updateUrl;
  xSemaphoreGive(mailLock);
  UpdateCheck c;
  String text;
  if (firmwareUpdater.fetchText(manifest, text, c.error)) {
    String file;
    int p = 0;
    while (p < (int)text.length()) {
      int e = text.indexOf('\n', p);
      if (e < 0) e = text.length();
      String line = text.substring(p, e);
      p = e + 1;
      line.trim();
      int eq = line.indexOf('=');
      if (eq <= 0 || line.startsWith("#")) continue;
      String k = line.substring(0, eq), v = line.substring(eq + 1);
      k.trim();
      v.trim();
      if (k == "version") c.version = v.toInt();
      else if (k == "file") file = v;
      else if (k == "url") c.url = v;
    }
    if (!c.url.length() && file.length()) c.url = manifest.substring(0, manifest.lastIndexOf('/') + 1) + file;
    c.ok = c.version > 0 && c.url.length();
    if (!c.ok) c.error = "The update file has no version or file name";
  }
  xSemaphoreTake(mailLock, portMAX_DELAY);
  updResult = c;
  xSemaphoreGive(mailLock);
  updResultReady = true;
}

void mailTaskFn(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    if (!WiFi.isConnected() || firmwareUpdater.active()) continue;
    const bool timeOk = time(nullptr) > 1700000000;
    if (updCheckRequest || (cfg.autoUpdate && timeOk && (int32_t)(millis() - updNextAuto) >= 0)) {
      updCheckRequest = false;
      updNextAuto = millis() + 24UL * 3600 * 1000;  // once a day
      checkForUpdate();
      continue;
    }
    if (replyWaiting()) { replyNow = false; sendOneReply(); continue; }
    xSemaphoreTake(mailLock, portMAX_DELAY);
    const uint32_t pollSec = cfg.pollSec;
    xSemaphoreGive(mailLock);
    if (!mailCheckNow && mailCheckedOnce && millis() - lastMailCheck < pollSec * 1000UL) continue;
    mailCheckNow = false;
    checkMail();
  }
}

void drainInbox() {
  if (!mailLock) return;
  std::vector<Incoming> got;
  xSemaphoreTake(mailLock, portMAX_DELAY);
  got.swap(inbox);
  xSemaphoreGive(mailLock);
  for (const Incoming &m : got)
    if (!runInstruction(m.subject)) addMessage(m.from, m.subject, m.body, m.picture.valid() ? &m.picture : nullptr, &m.meta);
}

// Results of sent quick replies, from the mail task.
void drainReplies() {
  if (!mailLock) return;
  std::vector<ReplyResult> got;
  xSemaphoreTake(mailLock, portMAX_DELAY);
  got.swap(replyResults);
  xSemaphoreGive(mailLock);
  for (const ReplyResult &r : got) {
    int i = findMsg(r.msgId);
    if (i < 0) continue;
    storeSetReply(r.msgId, store[i].reaction, r.ok ? REPLY_SENT : REPLY_FAILED);
    if (!r.ok) logf("Quick reply failed: %s", r.error.c_str());
    if (ui == UI_CAROUSEL || (ui == UI_SENT && curId == r.msgId)) showUi();
  }
}

// ---------------------------------------------------------------- Wi-Fi

WebServer server(80);
DNSServer dns;
bool webStarted = false;
bool apByUser = false;
const uint32_t STA_JOIN_MS = 20000, STA_RETRY_MS = 60000, AP_RETRY_MS = 5000;
uint32_t staRetryAt = 0, staJoinUntil = 0, apFallbackAt = 0, apOnSince = 0, apRetryAt = 0;
bool staJoining = false, scanPending = false;
bool mdnsUp = false, timeSet = false;

void setupWeb();

void startWeb() {
  if (webStarted) return;
  // WiFi.mode() initializes Arduino's network stack. Bind sockets only
  // after that has succeeded, or lwIP can assert during a cold boot.
  setupWeb();
  webStarted = true;
}

void beginSta() {
  const uint32_t now = millis();
  staRetryAt = now + (apOn ? STA_RETRY_MS : STA_JOIN_MS + 1000);
  logf("Joining Wi-Fi '%s'", cfg.ssid.c_str());
  staJoining = WiFi.STA.begin() &&
      WiFi.STA.connect(cfg.ssid.c_str(), cfg.wifiPass.c_str());
  staJoinUntil = now + STA_JOIN_MS;
  if (!staJoining) logf("Could not start the Wi-Fi connection attempt");
}

void cancelStaJoin() {
  if (!staJoining) return;
  staJoining = false;
  WiFi.disconnectAsync();
}

void startAP(bool byUser) {
  if (!apOn) {
    apRetryAt = millis() + AP_RETRY_MS;
    logf("Starting setup Wi-Fi");
    // Enable AP+STA only while the portal needs both interfaces. This also keeps
    // the existing station connection when the portal is opened by hand.
    if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAP(AP_SSID, cfg.apPass.c_str())) {
      logf("Could not start setup Wi-Fi. Retrying in 5 seconds.");
      return;
    }
    apOn = true;
    apRetryAt = apOnSince = millis();
    if (!dns.start(53, "*", WiFi.softAPIP())) {
      logf("Captive DNS unavailable. Open http://%s directly.", WiFi.softAPIP().toString().c_str());
    }
    startWeb();
    logf("Setup Wi-Fi '%s' on, open http://%s", AP_SSID, WiFi.softAPIP().toString().c_str());
  }
  if (byUser) apByUser = true;
  setUi(UI_SETUP);
}

void stopAP() {
  if (!apOn) return;
  if (!WiFi.mode(WIFI_STA)) {
    apRetryAt = millis() + AP_RETRY_MS;
    logf("Could not stop setup Wi-Fi");
    return;
  }
  dns.stop();
  apOn = false;
  apByUser = false;
  logf("Setup Wi-Fi off");
  if (ui == UI_SETUP) resumeUi();
}

const char *staStateText() {
  switch (WiFi.status()) {
    case WL_CONNECTED: return "connected";
    case WL_NO_SSID_AVAIL: return "network not found";
    case WL_CONNECT_FAILED: return "connection failed";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED: return staJoining ? "trying" : "disconnected";
    case WL_IDLE_STATUS: return staJoining ? "waiting for IP address" : "idle";
    default: return "not connected";
  }
}

void onConnected() {
  startWeb();
  logf("Wi-Fi connected, IP %s, %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (!mdnsUp && MDNS.begin(cfg.name.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
  }
  if (!timeSet) {
    configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com");
    timeSet = true;
  }
  mailCheckNow = true;
}

void wifiLoop() {
  static bool wasConnected = false;
  const uint32_t now = millis();
  const bool connected = WiFi.isConnected();
  const bool portalInUse = apOn && WiFi.softAPgetStationNum() > 0;
  if (connected) {
    staJoining = false;
    if (!wasConnected) onConnected();
    wasConnected = true;
    // Leave the setup page reachable until its clients disconnect.
    if (apOn && !portalInUse && (!apByUser || now - apOnSince >= 15 * 60000UL) &&
        (int32_t)(now - apRetryAt) >= 0) stopAP();
    return;
  }
  if (wasConnected) {
    wasConnected = false;
    logf("Wi-Fi lost (%s)", staStateText());
    staRetryAt = now + 1000;
    apFallbackAt = now + 30000;
    apRetryAt = now;
  }
  const bool configured = cfg.ssid.length() != 0;
  if (!apOn && (!configured || (int32_t)(now - apFallbackAt) >= 0) &&
      (int32_t)(now - apRetryAt) >= 0) {
    if (configured) logf("Can't join '%s' (%s)", cfg.ssid.c_str(), staStateText());
    cancelStaJoin();
    startAP(false);
    if (apOn) staRetryAt = now + STA_RETRY_MS;
  }
  // AP and STA share a channel. Defer a home Wi-Fi join while anyone is using the portal.
  if (staJoining && (portalInUse || (int32_t)(now - staJoinUntil) >= 0)) cancelStaJoin();
  if (configured && !staJoining && !portalInUse && (int32_t)(now - staRetryAt) >= 0 &&
      WiFi.scanComplete() != WIFI_SCAN_RUNNING) beginSta();
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
  String err;
  if (mailLock) {
    xSemaphoreTake(mailLock, portMAX_DELAY);
    err = mailError;
    xSemaphoreGive(mailLock);
  }
  j += ",\"mail\":{\"ok\":" + String(!mailCheckedOnce ? "null" : jb(mailOk)) + ",\"busy\":" + jb(mailBusy);
  j += ",\"error\":\"" + jsonEsc(err) + "\",\"ago\":" + String(mailCheckedOnce ? (long)((millis() - lastMailCheck) / 1000) : -1L) + "}";
  j += ",\"newCount\":" + String(countState(MSG_NEW)) + ",\"savedCount\":" + String(countState(MSG_SAVED));
  j += ",\"refreshMs\":" + String(lastRefreshMs) + ",\"refreshes\":" + String(refreshCount);
  j += ",\"fullRefreshes\":" + String(fullRefreshCount) + ",\"partialRefreshes\":" + String(partialRefreshCount);
  j += ",\"lastRefreshFull\":" + jb(lastRefreshFull) + ",\"firmware\":\"" + FIRMWARE_VERSION + "\"";
  String now = nowText();
  j += ",\"time\":\"" + jsonEsc(now.length() ? now : String("not set (needs internet)")) + "\"";
  j += ",\"uptime\":" + String(millis() / 1000) + ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"settings\":{\"ssid\":\"" + jsonEsc(cfg.ssid) + "\",\"hasWifiPass\":" + jb(cfg.wifiPass.length());
  j += ",\"guser\":\"" + jsonEsc(cfg.gmailUser) + "\",\"hasGmailPass\":" + jb(cfg.gmailPass.length());
  j += ",\"allow\":\"" + jsonEsc(cfg.allowFrom) + "\",\"poll\":" + String(cfg.pollSec);
  j += ",\"tz\":\"" + jsonEsc(cfg.tz) + "\",\"name\":\"" + jsonEsc(cfg.name) + "\"";
  j += ",\"lstyle\":" + String(cfg.ledStyle) + ",\"col1\":\"" + colorHex(cfg.color1) + "\",\"col2\":\"" + colorHex(cfg.color2) + "\"";
  j += ",\"lspeed\":" + String(cfg.ledSpeed) + ",\"ltail\":" + String(cfg.ledTail);
  j += ",\"ledson\":" + jb(cfg.ledsOn) + ",\"spinsec\":" + String(cfg.spinSec) + ",\"spinevery\":" + String(cfg.spinEveryMin);
  j += ",\"bed\":" + jb(cfg.bedtime) + ",\"bedfrom\":\"" + hhmm(cfg.bedFrom) + "\",\"bedto\":\"" + hhmm(cfg.bedTo) + "\"";
  j += ",\"nicks\":\"" + jsonEsc(cfg.nicknames) + "\"";
  j += ",\"autoupd\":" + jb(cfg.autoUpdate) + ",\"updurl\":\"" + jsonEsc(cfg.updateUrl) + "\"";
  j += ",\"bright\":" + String(cfg.brightness);
  j += ",\"hknob\":" + jb(cfg.knobHaptics) + ",\"hpow\":" + String(cfg.tickPower) + ",\"hms\":" + String(cfg.tickMs);
  j += ",\"nvibe\":" + jb(cfg.notifyVibe) + ",\"nticks\":" + String(cfg.ntfTicks);
  j += ",\"nms0\":" + String(cfg.ntfMsFirst) + ",\"nms1\":" + String(cfg.ntfMsLast);
  j += ",\"npow0\":" + String(cfg.ntfPowFirst) + ",\"npow1\":" + String(cfg.ntfPowLast);
  j += ",\"ngap\":" + String(cfg.ntfGap) + ",\"nrem\":" + String(cfg.ntfRemindMin);
  j += ",\"rot\":" + String(cfg.rotation) + ",\"krev\":" + jb(cfg.knobReverse);
  j += ",\"epdwire\":" + jb(cfg.epdWired) + ",\"epdspi\":" + String(cfg.epdSpiMHz) + "}";
  FirmwareUpdateStatus update = firmwareUpdater.status();
  j += ",\"update\":{\"busy\":" + jb(update.busy) + ",\"restarting\":" + jb(update.restarting);
  j += ",\"written\":" + String(update.written) + ",\"total\":" + String(update.total);
  j += ",\"message\":\"" + jsonEsc(update.message) + "\",\"token\":\"" + firmwareUpdater.token() + "\"}";
  j += ",\"log\":\"" + jsonEsc(logText()) + "\"}";
  server.send(200, "application/json", j);
}

void handleMessages() {
  String j = "[";
  for (int i = (int)store.size() - 1; i >= 0; i--) {  // newest first
    const Message &m = store[i];
    if (j.length() > 1) j += ",";
    j += "{\"id\":" + String(m.id) + ",\"state\":\"" + (m.state == MSG_NEW ? "new" : "saved") + "\"";
    j += ",\"from\":\"" + jsonEsc(senderName(m)) + "\",\"addr\":\"" + jsonEsc(m.meta.fromAddr) + "\"";
    j += ",\"subject\":\"" + jsonEsc(m.subject) + "\",\"when\":\"" + jsonEsc(m.when) + "\"";
    j += ",\"reaction\":\"" + String(m.reaction >= 0 && m.reaction < REACTION_COUNT ? REACTIONS[m.reaction].label : "") + "\"";
    static const char *const RS[] = { "", "sending", "sent", "failed" };
    j += ",\"reply\":\"" + String(RS[m.replyState & 3]) + "\"";
    j += ",\"body\":\"" + jsonEsc(m.body.substring(0, 160)) + "\"}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

void handleScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED && !scanPending && !staJoining) n = WiFi.scanNetworks(true);
  if (n == WIFI_SCAN_RUNNING) {
    scanPending = true;
    server.send(200, "application/json", "{\"busy\":true}");
    return;
  }
  scanPending = false;
  if (n < 0) {
    server.send(503, "application/json", "{\"error\":\"Wi-Fi scan unavailable. Try again shortly.\"}");
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

bool blockDuringUpdate() {
  if (!firmwareUpdater.active()) return false;
  server.send(409, "text/plain", "Firmware update in progress. Wait for it to finish.");
  return true;
}

void handleSettings() {
  if (blockDuringUpdate()) return;
  Settings next = cfg;
  if (server.hasArg("ssid")) next.ssid = server.arg("ssid");
  if (next.ssid.length() > 32) {
    server.send(400, "text/plain", "The Wi-Fi network name can be up to 32 bytes.");
    return;
  }
  const String wifiPass = server.arg("wpass");
  if (!next.ssid.length() || server.arg("wopen") == "1") {
    next.wifiPass = "";
  } else if (wifiPass.length()) {
    if (wifiPass.length() > 64) {
      server.send(400, "text/plain", "The Wi-Fi password can be up to 64 bytes.");
      return;
    }
    next.wifiPass = wifiPass;
  } else if (next.ssid != cfg.ssid) {
    next.wifiPass = "";
  }
  next.gmailUser = arg("guser");
  if (server.arg("gpass").length()) next.gmailPass = arg("gpass");
  next.allowFrom = arg("allow");
  next.pollSec = argNum("poll", 20, 3600, next.pollSec);
  if (arg("tz").length()) next.tz = arg("tz");
  if (arg("name").length()) next.name = arg("name");
  const String apPass = server.arg("appass");
  if (apPass.length()) {
    if (apPass.length() < 8 || apPass.length() > 63) {
      server.send(400, "text/plain", "The setup Wi-Fi password needs 8 to 63 characters.");
      return;
    }
    next.apPass = apPass;
  }
  readLookArgs(next);
  next.ledsOn = argBool("ledson", next.ledsOn);
  next.spinSec = argNum("spinsec", 0, 3600, next.spinSec);
  next.spinEveryMin = argNum("spinevery", 0, 1440, next.spinEveryMin);
  next.bedtime = argBool("bed", next.bedtime);
  auto parseHhmm = [](const String &t, uint16_t def) -> uint16_t {
    int c = t.indexOf(':');
    if (c < 1) return def;
    int h = t.substring(0, c).toInt(), m = t.substring(c + 1).toInt();
    if (h < 0 || h > 23 || m < 0 || m > 59) return def;
    return h * 60 + m;
  };
  if (server.hasArg("bedfrom")) next.bedFrom = parseHhmm(arg("bedfrom"), next.bedFrom);
  if (server.hasArg("bedto")) next.bedTo = parseHhmm(arg("bedto"), next.bedTo);
  next.autoUpdate = argBool("autoupd", next.autoUpdate);
  if (arg("updurl").length()) next.updateUrl = arg("updurl");
  if (server.hasArg("nicks")) {
    String n = server.arg("nicks");
    n.replace("\r", "");
    if (n.length() > 1500) {
      server.send(400, "text/plain", "Too many nicknames (keep it under 1500 characters).");
      return;
    }
    next.nicknames = n;
  }
  next.ntfRemindMin = argNum("nrem", 0, 1440, next.ntfRemindMin);
  next.rotation = argNum("rot", 0, 3, next.rotation);
  next.knobReverse = argBool("krev", next.knobReverse);
  next.epdWired = argBool("epdwire", next.epdWired);
  {
    long mhz = argNum("epdspi", 1, 10, next.epdSpiMHz);
    next.epdSpiMHz = mhz >= 10 ? 10 : (mhz >= 4 ? 4 : 1);
  }
  const bool restart = next.ssid != cfg.ssid || next.wifiPass != cfg.wifiPass || next.name != cfg.name ||
                       next.apPass != cfg.apPass || next.tz != cfg.tz || next.rotation != cfg.rotation ||
                       next.epdWired != cfg.epdWired || next.epdSpiMHz != cfg.epdSpiMHz;
  const bool mailChanged = next.gmailUser != cfg.gmailUser || next.gmailPass != cfg.gmailPass ||
                           next.allowFrom != cfg.allowFrom;
  if (mailLock) xSemaphoreTake(mailLock, portMAX_DELAY);
  cfg = std::move(next);
  if (mailLock) xSemaphoreGive(mailLock);
  saveSettings();
  if (restart) {
    logf("Settings saved, restarting");
    sendText("Saved. Restarting - if the home Wi-Fi works, the setup Wi-Fi closes.");
    rebootPending = true;
    rebootAt = millis() + 1500;
  } else {
    logf("Settings saved");
    ringLookFromSettings();
    if (mailChanged) mailCheckNow = true;
    sendText("Saved");
  }
}

void testScreen(const View &v) {
  screenTestActive = true;
  testScreenUntil = millis() + 12000;  // then back to the real screen
  screenShow(v);
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
  } else if (what == "react") {
    View v; v.scr = SCR_REACT; v.from = "Test"; v.pos = 1; v.total = 2; v.reaction = 0;
    testScreen(v);
    sendText("Quick reply screen");
  } else if (what == "arrive") {
    if (ui == UI_IDLE || ui == UI_ENVELOPE || ui == UI_NOMSG || ui == UI_CAROUSEL || ui == UI_HAPPY) startArrival();
    sendText("The hummingbird (then back to normal)");
  } else if (what == "nomsg") {
    if (firstNew() < 0 && (ui == UI_IDLE || ui == UI_NOMSG || ui == UI_CAROUSEL)) enterNoMsg();
    sendText("No new messages screen");
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
  server.on("/api/update", HTTP_POST, [] {
    if (!server.arg("token").length() || server.arg("token") != firmwareUpdater.token()) {
      server.send(403, "text/plain", "Reload the device page before requesting an update");
      return;
    }
    String error;
    if (!firmwareUpdater.start(server.arg("url"), error)) {
      server.send(400, "text/plain", error);
      return;
    }
    sendText("Update queued. Watch its progress below; the device restarts when installed.");
  });
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/messages", HTTP_GET, handleMessages);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/message", HTTP_POST, [] {
    String from = foldToAscii(server.arg("from")), sub = foldToAscii(server.arg("subject")), body = foldToAscii(server.arg("body"));
    if (runInstruction(sub)) { sendText("Ran it as an instruction"); return; }
    MsgMeta meta;
    meta.fromAddr = arg("addr");  // optional: lets you try a quick reply to yourself
    meta.rawSubject = server.arg("subject");
    addMessage(from.length() ? from : String("Test"), sub, body, nullptr, &meta);
    sendText(ui == UI_SETUP ? "Saved as new. The envelope shows when you leave setup mode." : "Sent: watch the box");
  });
  server.on("/api/msgdel", HTTP_POST, [] {
    if (server.arg("id") == "all") storeClear();
    else storeDelete(strtoul(server.arg("id").c_str(), nullptr, 10));
    if (ui == UI_READ || ui == UI_REACT || ui == UI_SENT) { if (findMsg(curId) < 0) resumeUi(); }
    else if (ui == UI_ENVELOPE) { if (firstNew() < 0) goIdle(false); else showUi(); }
    else if (ui == UI_CAROUSEL) { if (countState(MSG_SAVED) == 0) resumeUi(); else showUi(); }
    sendText("Deleted");
  });
  server.on("/api/checkupdate", HTTP_POST, [] {
    if (!WiFi.isConnected()) { sendText("Not on the home Wi-Fi yet, so GitHub can't be reached"); return; }
    if (firmwareUpdater.active()) { sendText("An update is already running"); return; }
    requestUpdateCheck(false);
    sendText("Checking for a newer version. If there is one, it installs and the box restarts. Watch the log.");
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
    if (blockDuringUpdate()) return;
    sendText("Restarting...");
    rebootPending = true;
    rebootAt = millis() + 800;
  });
  server.on("/api/factory", HTTP_POST, [] {
    if (blockDuringUpdate()) return;
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
  Serial.printf("\n%s starting, reset reason %d\n", FIRMWARE_VERSION, (int)esp_reset_reason());
  // waking from "Shut down": let go of the pin that kept the motor off
  gpio_hold_dis((gpio_num_t)PIN_VIBE);
  gpio_deep_sleep_hold_dis();
  const bool wokeByKnob = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
  logLock = xSemaphoreCreateMutex();
  mailLock = xSemaphoreCreateMutex();
  networkOperationLock = xSemaphoreCreateMutex();
  firmwareUpdater.begin(networkOperationLock);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  encoder.begin((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
  if (xTaskCreate(encoderTaskFn, "knob", 2048, nullptr, 3, nullptr) != pdPASS) logf("Could not start the knob task");

  logf("Loading settings");
  loadSettings();
  WiFi.persistent(false);
  WiFi.setHostname(cfg.name.c_str());
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSleep(true);   // modem sleep (default): keeps the 3.3 V rail steady for the screen
  apRetryAt = millis();
  if (cfg.ssid.length()) {
    apFallbackAt = millis() + STA_JOIN_MS;
    beginSta();
    if (!staJoining) {
      logf("Could not start home Wi-Fi. Trying setup mode.");
      apFallbackAt = millis();
      startAP(false);
    }
  } else {
    startAP(false);
  }

  logf("Starting controls and display");
  hapticsBegin();
  ledsBegin();
  storeLoad();
  for (const Message &m : store)  // quick replies that hadn't gone before a restart
    if (m.replyState == REPLY_PENDING && m.reaction >= 0) queueReply(m);
  notifyAt = lastRemind = millis();
  updNextAuto = millis() + 10 * 60000UL;  // the first automatic update check, 10 min after starting
  if (wokeByKnob) logf("Woken by the knob");
  screenBegin();
  if (apOn) {
    showUi();
  } else {
    resumeUi();
  }
  if (!mailLock || !networkOperationLock ||
      xTaskCreate(mailTaskFn, "mail", 12288, nullptr, 1, nullptr) != pdPASS) {
    logf("Could not start the Gmail task");
  }
  logf("Startup complete");
}

void loop() {
  wifiLoop();
  if (apOn) dns.processNextRequest();
  if (webStarted) server.handleClient();
  knobLoop();
  hapticsLoop();
  ledsLoop();
  drainInbox();
  drainReplies();
  confirmFirmware();

  uint32_t now = millis();
  updateLoop(now);
  switch (ui) {
    case UI_ENVELOPE:
      // a message is waiting: the ring spins for a minute every ten (settings), not at bedtime
      if (!overrideUntil) ringSet(envelopeSpinning(now) ? RING_NOTIFY : RING_OFF);
      if (cfg.ntfRemindMin && now - lastRemind > cfg.ntfRemindMin * 60000UL) {
        lastRemind = now;
        notifyBuzz();
      }
      break;
    case UI_ARRIVE: {
      // the next animation frame once the panel has shown this one
      const bool shown = refreshCount != arriveRefreshes || now - arriveShownAt > 2500;
      if (arriveFrame >= ARRIVE_FRAMES - 1) {
        if (shown && now - arriveShownAt > 2500) enterEnvelope();  // a moment on "You've got mail!"
      } else if (shown && now - arriveShownAt > 350) {  // about 0.8-1 s a frame
        arriveFrame++;
        showArriveFrame();
      }
      break;
    }
    case UI_READ:
    case UI_REACT:
      if (now - lastInput > READ_TIMEOUT_MS) {  // walked away: back to the envelope, or off
        logf("No knob use for a while");
        if (firstNew() >= 0) enterEnvelope();
        else goIdle(true);
      }
      break;
    case UI_SENT:
      if (now - sentAt > 2200) exitMessage();
      break;
    case UI_NOMSG:
      if (now - lastInput > NOMSG_TIMEOUT_MS) goIdle(false);
      break;
    case UI_CAROUSEL:
      if (now - lastInput > CAROUSEL_TIMEOUT_MS) resumeUi();
      break;
    case UI_MENU:
      if (now - lastInput > MENU_TIMEOUT_MS) {
        menuEditing = false;
        saveSettings();
        resumeUi();
      }
      break;
    case UI_SLEEP:
      if ((refreshCount != sleepRefreshes && now - sleepAt > 2500 && !hapticsBusy()) || now - sleepAt > 10000) goToSleep();
      break;
    case UI_HAPPY:
      if (now - happyAt > 3000) goIdle(true);  // shut down: brrr, ring out, screen blank
      break;
    default: break;
  }
  // the power-down animation has finished: bring back a "!led" glow, if one was set
  if (ui == UI_IDLE && ringMode == RING_OFF && !overrideUntil && (ambientColor || ambientRainbow)) ringForState();
  if (screenTestActive && (int32_t)(now - testScreenUntil) >= 0) {
    screenTestActive = false;
    showUi();
  }
  if (rebootPending && !firmwareUpdater.active() && (int32_t)(now - rebootAt) > 0) ESP.restart();
  delay(2);
}
