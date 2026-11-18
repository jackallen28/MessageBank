/*
  MessageBox - test firmware
  ==========================
  A little box that shows messages from a friend or partner. They email a private Gmail
  inbox; the box reads it and shows the message on a 3.7" e-paper, lights the LED ring and
  buzzes. Turn the knob to look back through messages, press it to say "read".

  Board:  ESP32-C3 SuperMini (Arduino: "ESP32C3 Dev Module", USB CDC On Boot: Enabled)
  Parts:  WeAct 3.7" e-paper 240x416 (GDEY037T03), WS2812B 12-LED ring, EC11/KY-040 rotary
          encoder with push button, PWM coin vibration motor module.
  Libraries (Library Manager): GxEPD2, Adafruit GFX Library, Adafruit NeoPixel.

  Wiring (see ../wiring.svg and ../README.md):
    E-paper  VCC->3V3  GND->GND  SCL->GPIO4  SDA->GPIO6  CS->GPIO7  D/C->GPIO5  RES->GPIO10  BUSY->GPIO3
    Encoder  +->3V3 (not 5V!)  GND->GND  CLK(S1)->GPIO0  DT(S2)->GPIO1  SW(KEY)->GPIO9
    LED ring 5V->5V  GND->GND  DIN->330 ohm->GPIO2            (+ 470 uF across 5V/GND)
    Vibe     VCC->5V  GND->GND  IN->GPIO20

  First start: there is no Wi-Fi saved, so the box opens its own Wi-Fi "MessageBox-Setup"
  (password "messagebox"). Join it with a phone and open http://192.168.4.1 . From there you
  can test every part and enter the home Wi-Fi and the Gmail details.
  Later: the same page is at http://messagebox.local (or the IP shown on the screen).
  Hold the knob down for 5 seconds to open the setup Wi-Fi again.

  Messages: send an email to the box's Gmail address. The subject is the title, the text is
  the message. Subjects starting with "!" are instructions instead:
    !led pink | red | green | blue | purple | orange | white | #rrggbb | off
    !rainbow           !buzz 3          !clear          !ping
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include "imap.h"
#include "web_page.h"

// ---------------------------------------------------------------- pins

const int PIN_EPD_SCK = 4, PIN_EPD_MOSI = 6, PIN_EPD_CS = 7, PIN_EPD_DC = 5, PIN_EPD_RST = 10, PIN_EPD_BUSY = 3;
const int PIN_ENC_A = 0;     // CLK / S1
const int PIN_ENC_B = 1;     // DT  / S2
const int PIN_ENC_SW = 9;    // KEY (same pin as the board's BOOT button)
const int PIN_LEDS = 2;      // ring DIN
const int PIN_VIBE = 20;     // motor module IN
const int PIN_BOARD_LED = 8; // blue LED on the SuperMini, on when LOW

const int NUM_LEDS = 12;
const int ENC_STEPS_PER_DETENT = 4;  // EC11 knobs give 4 steps per click; try 2 if one click moves two
const char *AP_SSID = "MessageBox-Setup";
const uint32_t LONG_PRESS_MS = 5000;

// (declared up here so the Arduino IDE's automatic prototypes can use them)
enum LedMode { LED_OFF, LED_SOLID, LED_BREATHE, LED_SPIN, LED_RAINBOW, LED_CHASE, LED_HOLD };
enum ScreenJob { SCR_NONE, SCR_MESSAGE, SCR_SETUP, SCR_TEXT, SCR_PATTERN, SCR_CLEAR };

// ---------------------------------------------------------------- settings

struct Settings {
  String ssid, wifiPass;
  String gmailUser, gmailPass, allowFrom;
  uint32_t pollSec = 60;
  String tz = "AEST-10AEDT,M10.1.0,M4.1.0/3";  // Sydney/Melbourne; see the README for others
  String color = "#ff3366";
  uint8_t brightness = 60;
  uint8_t vibe = 200;
  uint8_t rotation = 1;
  String name = "messagebox";
  String apPass = "messagebox";
} cfg;

Preferences prefs;

void loadSettings() {
  prefs.begin("mbox", true);
  cfg.ssid = prefs.getString("ssid", "");
  cfg.wifiPass = prefs.getString("wpass", "");
  cfg.gmailUser = prefs.getString("guser", "");
  cfg.gmailPass = prefs.getString("gpass", "");
  cfg.allowFrom = prefs.getString("allow", "");
  cfg.pollSec = prefs.getUInt("poll", 60);
  cfg.tz = prefs.getString("tz", cfg.tz);
  cfg.color = prefs.getString("color", cfg.color);
  cfg.brightness = prefs.getUChar("bright", 60);
  cfg.vibe = prefs.getUChar("vibe", 200);
  cfg.rotation = prefs.getUChar("rot", 1);
  cfg.name = prefs.getString("name", cfg.name);
  cfg.apPass = prefs.getString("appass", cfg.apPass);
  prefs.end();
  if (cfg.pollSec < 20) cfg.pollSec = 20;
}

void saveSettings() {
  prefs.begin("mbox", false);
  prefs.putString("ssid", cfg.ssid);
  prefs.putString("wpass", cfg.wifiPass);
  prefs.putString("guser", cfg.gmailUser);
  prefs.putString("gpass", cfg.gmailPass);
  prefs.putString("allow", cfg.allowFrom);
  prefs.putUInt("poll", cfg.pollSec);
  prefs.putString("tz", cfg.tz);
  prefs.putString("color", cfg.color);
  prefs.putUChar("bright", cfg.brightness);
  prefs.putUChar("vibe", cfg.vibe);
  prefs.putUChar("rot", cfg.rotation);
  prefs.putString("name", cfg.name);
  prefs.putString("appass", cfg.apPass);
  prefs.end();
}

// ---------------------------------------------------------------- log (Serial + web page)

String logLines[16];
int logHead = 0;

void logf(const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  char stamp[12];
  snprintf(stamp, sizeof(stamp), "%6lus ", millis() / 1000);
  Serial.print(stamp);
  Serial.println(buf);
  logLines[logHead] = String(stamp) + buf;
  logHead = (logHead + 1) % 16;
}

String logText() {
  String s;
  for (int i = 0; i < 16; i++) {
    const String &l = logLines[(logHead + i) % 16];
    if (l.length()) s += l + "\n";
  }
  return s;
}

// ---------------------------------------------------------------- messages

struct Message {
  String from, subject, body, when;
};
const int MAX_MSGS = 8;
Message msgs[MAX_MSGS];  // msgs[0] is the newest
int msgCount = 0;
int viewIndex = 0;       // which message is on screen
bool unread = false;

void saveLatestMessage() {
  prefs.begin("mbox", false);
  if (msgCount) {
    prefs.putString("m_from", msgs[0].from);
    prefs.putString("m_sub", msgs[0].subject);
    prefs.putString("m_body", msgs[0].body);
    prefs.putString("m_when", msgs[0].when);
  } else {
    prefs.remove("m_from");
  }
  prefs.putBool("unread", unread);
  prefs.end();
}

void loadLatestMessage() {
  prefs.begin("mbox", true);
  if (prefs.isKey("m_from")) {
    msgs[0] = { prefs.getString("m_from"), prefs.getString("m_sub"), prefs.getString("m_body"), prefs.getString("m_when") };
    msgCount = 1;
    unread = prefs.getBool("unread", false);
  }
  prefs.end();
}

String nowText() {
  struct tm t;
  if (!getLocalTime(&t, 10)) return "";
  char b[24];
  strftime(b, sizeof(b), "%a %d %b, %H:%M", &t);
  return b;
}

// ---------------------------------------------------------------- LED ring

Adafruit_NeoPixel ring(NUM_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800);
LedMode ledMode = LED_OFF;
uint32_t ledColor = 0xFF3366;
uint32_t ledStart = 0;
float holdFraction = 0;  // for LED_HOLD: how far the long press has got

uint32_t parseColor(String s, bool *ok = nullptr) {
  s.trim();
  s.toLowerCase();
  if (ok) *ok = true;
  if (s.startsWith("#") && s.length() == 7) return strtoul(s.c_str() + 1, nullptr, 16);
  if (s == "red") return 0xFF0000;
  if (s == "green") return 0x00FF20;
  if (s == "blue") return 0x0040FF;
  if (s == "pink") return 0xFF3366;
  if (s == "purple") return 0x9020FF;
  if (s == "orange") return 0xFF6000;
  if (s == "yellow") return 0xFFC000;
  if (s == "white") return 0xFFFFFF;
  if (s == "off" || s == "black") return 0;
  if (ok) *ok = false;
  return 0xFF3366;
}

uint32_t scaled(uint32_t c, float k) {
  uint8_t r = ((c >> 16) & 0xFF) * k, g = ((c >> 8) & 0xFF) * k, b = (c & 0xFF) * k;
  return ring.Color(r, g, b);
}

void setLeds(LedMode m, uint32_t color) {
  ledMode = m;
  ledColor = color;
  ledStart = millis();
}

void updateLeds() {
  static uint32_t last = 0;
  if (millis() - last < 20) return;
  last = millis();
  uint32_t t = millis() - ledStart;
  ring.setBrightness(cfg.brightness);
  switch (ledMode) {
    case LED_OFF:
      ring.clear();
      break;
    case LED_SOLID:
      ring.fill(scaled(ledColor, 1));
      break;
    case LED_BREATHE: {
      float k = 0.08 + 0.92 * (0.5 - 0.5 * cos(t * 2 * PI / 3000.0));
      ring.fill(scaled(ledColor, k));
      break;
    }
    case LED_SPIN:
      for (int i = 0; i < NUM_LEDS; i++) {
        int d = ((int)(t / 80) - i) % NUM_LEDS;
        if (d < 0) d += NUM_LEDS;
        ring.setPixelColor(i, d < 4 ? scaled(ledColor, 1.0 - d * 0.3) : 0);
      }
      break;
    case LED_RAINBOW:
      for (int i = 0; i < NUM_LEDS; i++)
        ring.setPixelColor(i, ring.gamma32(ring.ColorHSV((t * 20 + i * 65536 / NUM_LEDS) & 0xFFFF)));
      break;
    case LED_CHASE: {  // each LED in turn red, green, blue: shows the order and any dead colour
      ring.clear();
      int step = t / 250;
      if (step >= NUM_LEDS * 3) { setLeds(LED_OFF, 0); break; }
      static const uint32_t c[3] = { 0xFF0000, 0x00FF00, 0x0000FF };
      ring.setPixelColor(step % NUM_LEDS, scaled(c[step / NUM_LEDS], 1));
      break;
    }
    case LED_HOLD: {
      ring.clear();
      int n = holdFraction * NUM_LEDS + 0.5;
      for (int i = 0; i < n && i < NUM_LEDS; i++) ring.setPixelColor(i, scaled(0xFFFFFF, 0.6));
      break;
    }
  }
  ring.show();
}

// ---------------------------------------------------------------- vibration

// A pattern is pairs of {on ms, off ms}, played at the vibration strength setting.
struct VibeStep { uint16_t on, off; };
const VibeStep VIBE_SHORT[] = { { 150, 0 } };
const VibeStep VIBE_LONG[] = { { 800, 0 } };
const VibeStep VIBE_HEART[] = { { 90, 110 }, { 140, 600 }, { 90, 110 }, { 140, 0 } };
const VibeStep VIBE_TICK[] = { { 25, 0 } };
const VibeStep *vibePat = nullptr;
int vibeLen = 0, vibeIdx = 0, vibeRepeats = 0;
bool vibeOn = false, vibeRamp = false;
uint32_t vibeT = 0;

void vibeWrite(uint8_t v) { analogWrite(PIN_VIBE, v); }

void vibrate(const VibeStep *p, int len, int repeats = 1) {
  vibePat = p;
  vibeLen = len;
  vibeIdx = 0;
  vibeRepeats = repeats;
  vibeOn = true;
  vibeRamp = false;
  vibeT = millis();
  vibeWrite(cfg.vibe);
}

void vibrateRamp() {
  vibePat = nullptr;
  vibeRamp = true;
  vibeT = millis();
}

void updateVibe() {
  uint32_t t = millis() - vibeT;
  if (vibeRamp) {  // 0 -> full over 2 s, then off: shows that PWM strength control works
    if (t >= 2000) { vibeWrite(0); vibeRamp = false; }
    else vibeWrite(t * 255 / 2000);
    return;
  }
  if (!vibePat) return;
  if (vibeOn) {
    if (t >= vibePat[vibeIdx].on) {
      vibeWrite(0);
      vibeOn = false;
      vibeT = millis();
    }
    return;
  }
  bool last = vibeIdx == vibeLen - 1;
  uint32_t off = vibePat[vibeIdx].off;
  if (last && vibeRepeats > 1 && off < 700) off = 700;  // a pause between repeats
  if (t < off) return;
  if (last) {
    if (--vibeRepeats <= 0) { vibePat = nullptr; return; }
    vibeIdx = 0;
  } else vibeIdx++;
  vibeOn = true;
  vibeT = millis();
  vibeWrite(cfg.vibe);
}

// ---------------------------------------------------------------- rotary encoder and button

volatile int32_t encRaw = 0;
volatile uint8_t encState = 0;

void IRAM_ATTR encISR() {
  static const int8_t table[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
  encState = ((encState << 2) | (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B)) & 0x0F;
  encRaw = encRaw + table[encState];
}

int32_t encPosition() {
  noInterrupts();
  int32_t r = encRaw;
  interrupts();
  return r / ENC_STEPS_PER_DETENT;  // rounds towards zero, fine for "which way and how far"
}

bool btnDown = false;           // debounced state
uint32_t btnChange = 0, btnDownAt = 0;
uint32_t pressCount = 0;
bool longFired = false;

// ---------------------------------------------------------------- e-paper

GxEPD2_BW<GxEPD2_370_GDEY037T03, GxEPD2_370_GDEY037T03::HEIGHT> epd(
  GxEPD2_370_GDEY037T03(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// Prints text word-wrapped inside a box; returns the y below the last line, or -1 if it didn't fit.
int drawWrapped(const String &text, int x, int y, int w, int maxY, const GFXfont *font, bool draw) {
  epd.setFont(font);
  int16_t bx, by;
  uint16_t bw, bh;
  epd.getTextBounds("Ag", 0, 0, &bx, &by, &bw, &bh);
  int lineH = font->yAdvance;
  int cy = y + lineH - 4;
  String line;
  size_t i = 0;
  auto flush = [&](const String &l) {
    if (draw && cy <= maxY) { epd.setCursor(x, cy); epd.print(l); }
    cy += lineH;
  };
  while (i <= text.length()) {
    int nl = text.indexOf('\n', i);
    if (nl < 0) nl = text.length();
    String para = text.substring(i, nl);
    i = nl + 1;
    line = "";
    size_t p = 0;
    while (p <= para.length()) {
      int sp = para.indexOf(' ', p);
      if (sp < 0) sp = para.length();
      String word = para.substring(p, sp);
      p = sp + 1;
      String trial = line.length() ? line + " " + word : word;
      epd.getTextBounds(trial, 0, 0, &bx, &by, &bw, &bh);
      if (bw > w && line.length()) {
        flush(line);
        line = word;
      } else line = trial;
      // a single word wider than the box: cut it
      while (true) {
        epd.getTextBounds(line, 0, 0, &bx, &by, &bw, &bh);
        if (bw <= w || line.length() < 2) break;
        int cut = line.length() - 1;
        while (cut > 1) {
          epd.getTextBounds(line.substring(0, cut), 0, 0, &bx, &by, &bw, &bh);
          if (bw <= w) break;
          cut--;
        }
        flush(line.substring(0, cut));
        line = line.substring(cut);
      }
      if (sp >= (int)para.length()) break;
    }
    flush(line);
    if (nl >= (int)text.length()) break;
  }
  int bottom = cy - lineH + 6;
  return bottom <= maxY ? bottom : -1;
}

void drawCentered(const String &s, int y, const GFXfont *font) {
  epd.setFont(font);
  int16_t bx, by;
  uint16_t bw, bh;
  epd.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  epd.setCursor((epd.width() - bw) / 2 - bx, y);
  epd.print(s);
}

void drawFooter(const String &s) {
  epd.setFont(&FreeSans9pt7b);
  epd.drawFastHLine(10, epd.height() - 28, epd.width() - 20, GxEPD_BLACK);
  epd.setCursor(12, epd.height() - 9);
  epd.print(s);
}

void epdStart() {
  epd.setRotation(cfg.rotation);
  epd.setFullWindow();
  epd.firstPage();
  epd.fillScreen(GxEPD_WHITE);
  epd.setTextColor(GxEPD_BLACK);
}

void epdFinish() {
  while (epd.nextPage()) {}
  epd.hibernate();  // panel off; the picture stays without power
}

void showSetupScreen() {
  logf("Screen: setup instructions");
  epdStart();
  int W = epd.width();
  epd.fillRect(0, 0, W, 44, GxEPD_BLACK);
  epd.setTextColor(GxEPD_WHITE);
  drawCentered("MessageBox setup", 31, &FreeSansBold12pt7b);
  epd.setTextColor(GxEPD_BLACK);
  String t = "1. On your phone, join the Wi-Fi\n    " + String(AP_SSID) + "\n    password: " + cfg.apPass +
             "\n2. Open http://192.168.4.1\n3. Test the parts, then enter\n    your Wi-Fi and Gmail details.";
  drawWrapped(t, 14, 56, W - 28, epd.height() - 34, &FreeSans12pt7b, true);
  drawFooter(cfg.ssid.length() ? "Saved Wi-Fi: " + cfg.ssid + " (retrying)" : "No Wi-Fi saved yet");
  epdFinish();
}

void showStatusScreen(const String &title, const String &line) {
  logf("Screen: %s", title.c_str());
  epdStart();
  drawCentered(title, epd.height() / 2 - 10, &FreeSansBold18pt7b);
  drawWrapped(line, 14, epd.height() / 2 + 6, epd.width() - 28, epd.height() - 34, &FreeSans12pt7b, true);
  drawFooter(WiFi.isConnected() ? WiFi.localIP().toString() + "  -  " + cfg.name + ".local" : "Not online");
  epdFinish();
}

void showMessage(int idx) {
  if (idx < 0 || idx >= msgCount) {
    showStatusScreen("No messages", "Waiting for your first one.");
    return;
  }
  const Message &m = msgs[idx];
  logf("Screen: message %d/%d from %s", idx + 1, msgCount, m.from.c_str());
  epdStart();
  int W = epd.width(), H = epd.height();
  // header bar
  epd.fillRect(0, 0, W, 34, GxEPD_BLACK);
  epd.setTextColor(GxEPD_WHITE);
  epd.setFont(&FreeSansBold9pt7b);
  epd.setCursor(10, 23);
  epd.print("From " + m.from);
  epd.setTextColor(GxEPD_BLACK);
  int y = 42;
  bool hasSubject = m.subject.length() && m.subject != "(no subject)";
  if (hasSubject) {
    y = drawWrapped(m.subject, 12, y, W - 24, H - 36, &FreeSansBold12pt7b, true);
    if (y < 0) y = H - 36;
    y += 2;
  }
  // body: the biggest font that fits
  String body = m.body.length() || hasSubject ? m.body : String("(empty message)");
  const GFXfont *fonts[] = { &FreeSansBold18pt7b, &FreeSans12pt7b, &FreeSans9pt7b };
  int pick = 2;
  for (int f = 0; f < 3; f++) {
    if (f == 0 && body.length() > 40) continue;  // the big font is for short notes
    if (drawWrapped(body, 12, y, W - 24, H - 34, fonts[f], false) >= 0) { pick = f; break; }
  }
  drawWrapped(body, 12, y, W - 24, H - 34, fonts[pick], true);
  String foot = m.when;
  if (msgCount > 1) foot += "   " + String(idx + 1) + "/" + String(msgCount);
  if (idx == 0 && unread) foot += "   press to say read";
  drawFooter(foot);
  epdFinish();
}

void showTestPattern() {
  logf("Screen: test pattern");
  epdStart();
  int W = epd.width(), H = epd.height();
  epd.drawRect(0, 0, W, H, GxEPD_BLACK);
  epd.drawRect(3, 3, W - 6, H - 6, GxEPD_BLACK);
  for (int i = 0; i < 8; i++) epd.fillRect(10 + i * ((W - 20) / 8), 10, (W - 20) / 16, 30, GxEPD_BLACK);
  epd.drawLine(0, 0, W - 1, H - 1, GxEPD_BLACK);
  epd.drawLine(W - 1, 0, 0, H - 1, GxEPD_BLACK);
  epd.fillCircle(W / 2, H / 2, 30, GxEPD_BLACK);
  drawCentered(String(W) + " x " + String(H), H / 2 + 60, &FreeSansBold12pt7b);
  drawCentered("The corners and edges should all show", H - 20, &FreeSans9pt7b);
  epdFinish();
}

// Long screen updates happen in loop(), not inside a web request, so the page gets its answer first.
ScreenJob screenJob = SCR_NONE;
String screenText;
uint32_t screenDue = 0;

void queueScreen(ScreenJob j, uint32_t delayMs = 0) {
  screenJob = j;
  screenDue = millis() + delayMs;
}

void runScreenJob() {
  if (screenJob == SCR_NONE || (int32_t)(millis() - screenDue) < 0) return;
  ScreenJob j = screenJob;
  screenJob = SCR_NONE;
  uint32_t t0 = millis();
  switch (j) {
    case SCR_MESSAGE: showMessage(viewIndex); break;
    case SCR_SETUP: showSetupScreen(); break;
    case SCR_TEXT:
      epdStart();
      drawWrapped(screenText, 12, 12, epd.width() - 24, epd.height() - 12, &FreeSansBold12pt7b, true);
      epdFinish();
      break;
    case SCR_PATTERN: showTestPattern(); break;
    case SCR_CLEAR:
      epdStart();
      epdFinish();
      break;
    default: break;
  }
  logf("Screen updated in %lu ms", millis() - t0);
}

// ---------------------------------------------------------------- new messages and instructions

void addMessage(const String &from, const String &subject, const String &body) {
  for (int i = MAX_MSGS - 1; i > 0; i--) msgs[i] = msgs[i - 1];
  msgs[0] = { from, subject, body, nowText() };
  if (msgCount < MAX_MSGS) msgCount++;
  viewIndex = 0;
  unread = true;
  saveLatestMessage();
  setLeds(LED_BREATHE, parseColor(cfg.color));
  vibrate(VIBE_HEART, 4);
  queueScreen(SCR_MESSAGE);
  logf("New message from %s: %s", from.c_str(), subject.c_str());
}

// Subject "!led red" etc. Returns true if it was an instruction.
bool runInstruction(const String &subjectIn, const String &body) {
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
    bool ok;
    uint32_t c = parseColor(arg, &ok);
    setLeds(c == 0 ? LED_OFF : LED_SOLID, c);
  } else if (cmd == "rainbow") {
    setLeds(LED_RAINBOW, 0);
  } else if (cmd == "buzz" || cmd == "vibrate") {
    int n = arg.toInt();
    vibrate(VIBE_HEART, 4, constrain(n, 1, 10));
  } else if (cmd == "clear") {
    msgCount = 0;
    unread = false;
    saveLatestMessage();
    setLeds(LED_OFF, 0);
    queueScreen(SCR_MESSAGE);
  } else if (cmd == "ping") {
    setLeds(LED_SPIN, 0x00FF40);
    vibrate(VIBE_SHORT, 1);
  } else {
    logf("Unknown instruction '%s', shown as a message", cmd.c_str());
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- Gmail

GmailImap imap;
uint32_t lastMailCheck = 0;
bool mailCheckedOnce = false, mailOk = false;
String mailError;
bool mailCheckNow = false;

void checkMail() {
  lastMailCheck = millis();
  mailCheckedOnce = true;
  if (!cfg.gmailUser.length() || !cfg.gmailPass.length()) {
    mailOk = false;
    mailError = "Gmail address or app password not set";
    return;
  }
  digitalWrite(PIN_BOARD_LED, LOW);
  uint32_t t0 = millis();
  if (!imap.connect(cfg.gmailUser, cfg.gmailPass)) {
    mailOk = false;
    mailError = imap.error;
    logf("Gmail: %s", mailError.c_str());
    digitalWrite(PIN_BOARD_LED, HIGH);
    return;
  }
  uint32_t uids[10];
  int n = 0;
  bool ok = imap.searchUnseen(cfg.allowFrom, uids, 10, n);
  if (ok) logf("Gmail: %d unread", n);
  for (int i = 0; ok && i < n && i < 5; i++) {  // at most 5 per check, oldest first
    MailItem m;
    if (!imap.fetch(uids[i], m)) { ok = false; break; }
    logf("Mail %lu from %s <%s>: '%s', %d chars", (unsigned long)m.uid, m.fromName.c_str(), m.fromAddr.c_str(),
         m.subject.c_str(), m.body.length());
    if (!runInstruction(m.subject, m.body)) addMessage(m.fromName, m.subject, m.body);
    imap.markSeen(m.uid);
    yield();
  }
  mailOk = ok;
  mailError = ok ? "" : imap.error;
  if (!ok) logf("Gmail: %s", mailError.c_str());
  imap.logout();
  logf("Gmail check took %lu ms", millis() - t0);
  digitalWrite(PIN_BOARD_LED, HIGH);
}

// ---------------------------------------------------------------- Wi-Fi

WebServer server(80);
DNSServer dns;
bool apOn = false;
bool apByUser = false;          // opened with the knob or the page (stays until left)
uint32_t staRetryAt = 0, apOnSince = 0;
bool mdnsUp = false, timeSet = false;

void startAP(bool byUser) {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  String pass = cfg.apPass.length() >= 8 ? cfg.apPass : String("messagebox");
  WiFi.softAP(AP_SSID, pass.c_str());
  delay(100);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
  apByUser = byUser;
  apOnSince = millis();
  logf("Setup Wi-Fi '%s' on, open http://%s", AP_SSID, WiFi.softAPIP().toString().c_str());
  setLeds(LED_SOLID, 0x0060FF);
  queueScreen(SCR_SETUP);
}

void stopAP() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
  apByUser = false;
  logf("Setup Wi-Fi off");
  if (ledMode == LED_SOLID && ledColor == 0x0060FF) setLeds(unread ? LED_BREATHE : LED_OFF, parseColor(cfg.color));
}

void beginSta() {
  if (!cfg.ssid.length()) return;
  logf("Joining Wi-Fi '%s'", cfg.ssid.c_str());
  WiFi.setHostname(cfg.name.c_str());
  WiFi.begin(cfg.ssid.c_str(), cfg.wifiPass.c_str());
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
  if (!apOn) {
    setLeds(unread ? LED_BREATHE : LED_OFF, parseColor(cfg.color));
    queueScreen(SCR_MESSAGE);
  }
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
  // While the setup Wi-Fi is on, joining the home Wi-Fi moves the radio's channel and drops the
  // phone, so only retry when nobody is connected to the setup Wi-Fi.
  if (apOn && (int32_t)(millis() - staRetryAt) > 0) {
    staRetryAt = millis() + 60000;
    if (WiFi.softAPgetStationNum() == 0) beginSta();
  }
}

// ---------------------------------------------------------------- web server

String jsonEsc(const String &s) {
  String o;
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

void handleStatus() {
  String j;
  j.reserve(2600);
  j += "{\"setup\":" + String(apOn ? "true" : "false");
  j += ",\"apSsid\":\"" + String(AP_SSID) + "\"";
  j += ",\"staOk\":" + String(WiFi.isConnected() ? "true" : "false");
  j += ",\"staSsid\":\"" + jsonEsc(WiFi.SSID()) + "\",\"staState\":\"" + staStateText() + "\"";
  j += ",\"ip\":\"" + (WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\"";
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"enc\":" + String(encPosition()) + ",\"btn\":" + String(btnDown ? "true" : "false") + ",\"presses\":" + String(pressCount);
  j += ",\"mail\":{\"ok\":" + String(!mailCheckedOnce ? "null" : (mailOk ? "true" : "false"));
  j += ",\"error\":\"" + jsonEsc(mailError) + "\",\"ago\":" + String(mailCheckedOnce ? (long)((millis() - lastMailCheck) / 1000) : -1L) + "}";
  j += ",\"msgCount\":" + String(msgCount) + ",\"unread\":" + String(unread ? "true" : "false");
  j += ",\"time\":\"" + jsonEsc(nowText().length() ? nowText() : String("not set (needs internet)")) + "\"";
  j += ",\"uptime\":" + String(millis() / 1000) + ",\"heap\":" + String(ESP.getFreeHeap());
  char col[8];
  snprintf(col, sizeof(col), "#%06lx", (unsigned long)(parseColor(cfg.color) & 0xFFFFFF));
  j += ",\"settings\":{\"ssid\":\"" + jsonEsc(cfg.ssid) + "\",\"hasWifiPass\":" + String(cfg.wifiPass.length() ? "true" : "false");
  j += ",\"gmailUser\":\"" + jsonEsc(cfg.gmailUser) + "\",\"hasGmailPass\":" + String(cfg.gmailPass.length() ? "true" : "false");
  j += ",\"allowFrom\":\"" + jsonEsc(cfg.allowFrom) + "\",\"pollSec\":" + String(cfg.pollSec);
  j += ",\"tz\":\"" + jsonEsc(cfg.tz) + "\",\"color\":\"" + col + "\",\"brightness\":" + String(cfg.brightness);
  j += ",\"vibe\":" + String(cfg.vibe) + ",\"rotation\":" + String(cfg.rotation) + ",\"name\":\"" + jsonEsc(cfg.name) + "\"}";
  j += ",\"log\":\"" + jsonEsc(logText()) + "\"}";
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
    for (int k = 0; k < i; k++) if (WiFi.SSID(k) == s) dup = true;  // the same network on several access points
    if (dup) continue;
    if (shown++) j += ",";
    j += "{\"ssid\":\"" + jsonEsc(s) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  j += "]}";
  WiFi.scanDelete();  // the next request starts a fresh scan
  server.send(200, "application/json", j);
}

bool rebootPending = false;
uint32_t rebootAt = 0;

void handleSettings() {
  auto arg = [](const char *k) { String v = server.arg(k); v.trim(); return v; };
  cfg.ssid = arg("ssid");
  if (server.arg("wpass").length()) cfg.wifiPass = server.arg("wpass");
  cfg.gmailUser = arg("guser");
  if (server.arg("gpass").length()) cfg.gmailPass = arg("gpass");
  cfg.allowFrom = arg("allow");
  cfg.pollSec = constrain(arg("poll").toInt(), 20, 3600);
  if (arg("tz").length()) cfg.tz = arg("tz");
  cfg.color = arg("color");
  cfg.brightness = constrain(arg("bright").toInt(), 5, 255);
  cfg.vibe = constrain(arg("vibe").toInt(), 0, 255);
  cfg.rotation = constrain(arg("rot").toInt(), 0, 3);
  if (arg("name").length()) cfg.name = arg("name");
  if (server.arg("appass").length()) {
    if (server.arg("appass").length() < 8) { sendText("Setup Wi-Fi password needs 8+ characters. Nothing saved."); return; }
    cfg.apPass = server.arg("appass");
  }
  saveSettings();
  logf("Settings saved, restarting");
  sendText("Saved. Restarting - if the home Wi-Fi works, the screen shows the new address.");
  rebootPending = true;
  rebootAt = millis() + 1500;
}

void handleTest() {
  String what = server.arg("what");
  if (what == "led") {
    String m = server.arg("mode");
    uint32_t c = parseColor(server.arg("color"));
    LedMode lm = m == "solid" ? LED_SOLID : m == "breathe" ? LED_BREATHE : m == "spin" ? LED_SPIN
               : m == "rainbow" ? LED_RAINBOW : m == "chase" ? LED_CHASE : LED_OFF;
    setLeds(lm, c);
    sendText(m == "chase" ? "Each LED goes red, then green, then blue, in order" : "LEDs: " + m);
  } else if (what == "vibe") {
    String p = server.arg("pattern");
    if (p == "long") vibrate(VIBE_LONG, 1);
    else if (p == "heart") vibrate(VIBE_HEART, 4);
    else if (p == "ramp") vibrateRamp();
    else vibrate(VIBE_SHORT, 1);
    sendText("Buzz: " + p);
  } else if (what == "epd") {
    screenText = foldToAscii(server.arg("text"));
    queueScreen(SCR_TEXT, 200);
    sendText("Drawing on the e-paper...");
  } else if (what == "epdpattern") {
    queueScreen(SCR_PATTERN, 200);
    sendText("Drawing the test pattern...");
  } else if (what == "epdclear") {
    queueScreen(SCR_CLEAR, 200);
    sendText("Clearing the e-paper...");
  } else sendText("Unknown test");
}

void handleFakeMessage() {
  String from = foldToAscii(server.arg("from")), sub = foldToAscii(server.arg("subject")), body = foldToAscii(server.arg("body"));
  if (!runInstruction(sub, body)) {
    addMessage(from, sub, body);
    screenDue = millis() + 200;
    sendText("Shown as a new message. Press the knob to mark it read.");
  } else sendText("Ran it as an instruction");
}

void setupWeb() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/message", HTTP_POST, handleFakeMessage);
  server.on("/api/checkmail", HTTP_POST, [] {
    if (!WiFi.isConnected()) { sendText("Not on the home Wi-Fi yet, so Gmail can't be reached"); return; }
    mailCheckNow = true;
    sendText("Checking Gmail - watch the status and log");
  });
  server.on("/api/leavesetup", HTTP_POST, [] {
    if (!WiFi.isConnected()) { sendText("Stays on: the home Wi-Fi isn't connected"); return; }
    sendText("Setup Wi-Fi turning off. The page stays at http://" + WiFi.localIP().toString());
    stopAP();
    queueScreen(SCR_MESSAGE);
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

// ---------------------------------------------------------------- knob actions

void knobLoop() {
  // button, debounced
  bool raw = digitalRead(PIN_ENC_SW) == LOW;
  if (raw != btnDown && millis() - btnChange > 30) {
    btnDown = raw;
    btnChange = millis();
    if (btnDown) {
      btnDownAt = millis();
      longFired = false;
    } else if (!longFired) {  // short press
      pressCount++;
      logf("Knob pressed");
      vibrate(VIBE_TICK, 1);
      if (ledMode == LED_HOLD) setLeds(unread ? LED_BREATHE : LED_OFF, parseColor(cfg.color));
      if (unread) {
        unread = false;
        saveLatestMessage();
        setLeds(LED_OFF, 0);
        if (viewIndex == 0) queueScreen(SCR_MESSAGE, 300);  // drop "press to say read"
      } else if (viewIndex != 0) {
        viewIndex = 0;
        queueScreen(SCR_MESSAGE, 300);
      }
    }
  }
  if (btnDown && !longFired) {
    uint32_t held = millis() - btnDownAt;
    if (held > 600) {  // show the hold filling the ring
      holdFraction = (float)held / LONG_PRESS_MS;
      if (ledMode != LED_HOLD) setLeds(LED_HOLD, 0);
    }
    if (held >= LONG_PRESS_MS) {
      longFired = true;
      logf("Knob held 5 s: setup Wi-Fi");
      vibrate(VIBE_LONG, 1);
      if (apOn) { apByUser = true; queueScreen(SCR_SETUP); setLeds(LED_SOLID, 0x0060FF); }
      else startAP(true);
    }
  }
  // turning: browse the kept messages (the screen redraws once the knob stops)
  static int32_t lastPos = 0;
  int32_t pos = encPosition();
  if (pos != lastPos) {
    int d = pos - lastPos;
    lastPos = pos;
    logf("Knob turned to %ld", (long)pos);
    if (msgCount > 1 && !apOn) {
      int v = constrain(viewIndex + (d > 0 ? 1 : -1), 0, msgCount - 1);
      if (v != viewIndex) {
        viewIndex = v;
        vibrate(VIBE_TICK, 1);
        queueScreen(SCR_MESSAGE, 700);
      }
    }
  }
}

// ---------------------------------------------------------------- setup and loop

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_BOARD_LED, OUTPUT);
  digitalWrite(PIN_BOARD_LED, HIGH);
  pinMode(PIN_VIBE, OUTPUT);
  vibeWrite(0);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  logf("MessageBox test firmware starting");
  loadSettings();
  loadLatestMessage();

  ring.begin();
  ring.setBrightness(cfg.brightness);
  ring.clear();
  ring.show();

  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
  epd.init(0, true, 2, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));

  setupWeb();
  WiFi.persistent(false);
  if (cfg.ssid.length()) {
    WiFi.mode(WIFI_STA);
    beginSta();
    staRetryAt = millis() + 20000;  // 20 s to join, else the setup Wi-Fi opens
    setLeds(LED_SPIN, 0x0060FF);
    showStatusScreen("Connecting", "Joining Wi-Fi " + cfg.ssid + "...");
  } else {
    logf("No Wi-Fi saved");
    startAP(false);
  }
}

void loop() {
  if (apOn) dns.processNextRequest();
  server.handleClient();
  knobLoop();
  updateLeds();
  updateVibe();
  wifiLoop();
  runScreenJob();
  if (WiFi.isConnected() && (mailCheckNow || millis() - lastMailCheck >= cfg.pollSec * 1000UL)) {
    mailCheckNow = false;
    checkMail();
  }
  if (rebootPending && (int32_t)(millis() - rebootAt) > 0) {
    epd.hibernate();
    ESP.restart();
  }
  // Setup Wi-Fi opened by hand closes after 15 minutes if the home Wi-Fi works and nobody is on it.
  if (apOn && apByUser && WiFi.isConnected() && millis() - apOnSince > 15 * 60000UL && WiFi.softAPgetStationNum() == 0) {
    apByUser = false;
    stopAP();
    queueScreen(SCR_MESSAGE);
  }
  delay(2);
}
