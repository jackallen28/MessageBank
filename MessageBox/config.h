// Pins, settings (saved in flash with Preferences) and the shared log.
#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ---------------------------------------------------------------- pins

// Display reset owns GPIO8, which is shared with the onboard blue LED.
// Keep the BOOT/GPIO9 pin clear of external connections.
const int PIN_EPD_SCK = 10, PIN_EPD_MOSI = 6, PIN_EPD_CS = 7, PIN_EPD_DC = 5;
const int PIN_EPD_RST = 8;    // Screen RES, when "Screen wiring" is wired (default). Not wired: tie RES to 3V3.
const int PIN_EPD_BUSY = 4;   // Screen BUSY, when wired. Not wired: leave BUSY unconnected.
const int PIN_ENC_A = 0;      // CLK / S1
const int PIN_ENC_B = 1;      // DT  / S2
const int PIN_ENC_SW = 21;    // KEY (the C3's TX pin: it chatters at boot, so don't hold the knob while plugging in)
const int PIN_LEDS = 2;       // ring DIN
const int PIN_VIBE = 20;      // motor module IN

const int NUM_LEDS = 12;
const char *const FIRMWARE_VERSION = "MessageBox-main-v12";
const char *const AP_SSID = "MessageBox-Setup";
const uint32_t LONG_PRESS_MS = 5000;  // hold the knob this long for the setup Wi-Fi
const uint32_t READ_TIMEOUT_MS = 180000;  // reading with no knob use for 3 min: back to the envelope
const int SCROLL_LINES_PER_CLICK = 2;
const int MAX_MSGS = 40;

// Arduino generates sketch prototypes before .ino-local type declarations.
enum Ui : uint8_t { UI_IDLE, UI_ENVELOPE, UI_READ, UI_DECIDE, UI_HAPPY, UI_SETUP };

// ---------------------------------------------------------------- settings

enum LedStyle : uint8_t { STYLE_COMET, STYLE_TWIN, STYLE_RAINBOW, STYLE_CHASE, STYLE_FLASH, STYLE_COUNT };

struct Settings {
  // Wi-Fi and Gmail
  String ssid, wifiPass;
  String gmailUser, gmailPass, allowFrom;
  uint32_t pollSec = 60;
  String tz = "AEST-10AEDT,M10.1.0,M4.1.0/3";  // Sydney/Melbourne; see the README for others
  String name = "messagebox";
  String apPass = "messagebox";
  // lights for a new message
  uint8_t ledStyle = STYLE_COMET;
  uint32_t color1 = 0xFF2060, color2 = 0x6020FF;
  uint16_t ledSpeed = 1200;    // ms for one trip round the ring
  uint8_t ledTail = 5;         // comet tail, in LEDs
  uint16_t ledMinutes = 0;     // stop the lights after this long (0 = until read)
  uint8_t brightness = 60;     // 0-255; 12 LEDs at 255 white draw ~0.7 A
  // knob haptics
  bool knobHaptics = true;
  uint8_t tickPower = 200;     // 0-255
  uint8_t tickMs = 15;
  // notification buzz: tick tick tick tick tick TICK
  bool notifyVibe = true;
  uint8_t ntfTicks = 6;
  uint16_t ntfMsFirst = 20, ntfMsLast = 140;    // pulse length of the first and last tick
  uint8_t ntfPowFirst = 90, ntfPowLast = 255;   // strength of the first and last tick
  uint16_t ntfGap = 320;                        // ms between ticks
  uint16_t ntfRemindMin = 0;                    // buzz again every N minutes while unread (0 = off)
  // screen and knob
  uint8_t rotation = 1;        // 0 portrait, 1 landscape (as in the enclosure), 2 and 3 the same turned 180
  bool knobReverse = false;
  bool epdWired = true;        // screen BUSY (GPIO4) and RESET (GPIO8) connected: the tested setup
  uint8_t epdSpiMHz = 1;       // screen SPI clock: 1 (tested), 4 or 10 MHz
} cfg;

Preferences prefs;

uint32_t parseColor(String s, bool *ok = nullptr) {
  s.trim();
  s.toLowerCase();
  if (ok) *ok = true;
  if (s.startsWith("#") && s.length() == 7) return strtoul(s.c_str() + 1, nullptr, 16);
  if (s == "red") return 0xFF0000;
  if (s == "green") return 0x00FF20;
  if (s == "blue") return 0x0040FF;
  if (s == "pink") return 0xFF2060;
  if (s == "purple") return 0x8020FF;
  if (s == "orange") return 0xFF6000;
  if (s == "yellow") return 0xFFC000;
  if (s == "white") return 0xFFFFFF;
  if (s == "off" || s == "black") return 0;
  if (ok) *ok = false;
  return 0xFF2060;
}

String colorHex(uint32_t c) {
  char b[8];
  snprintf(b, sizeof(b), "#%06lx", (unsigned long)(c & 0xFFFFFF));
  return b;
}

void loadSettings() {
  prefs.begin("mbox", true);
  cfg.ssid = prefs.getString("ssid", "");
  cfg.wifiPass = prefs.getString("wpass", "");
  cfg.gmailUser = prefs.getString("guser", "");
  cfg.gmailPass = prefs.getString("gpass", "");
  cfg.allowFrom = prefs.getString("allow", "");
  cfg.pollSec = prefs.getUInt("poll", cfg.pollSec);
  cfg.tz = prefs.getString("tz", cfg.tz);
  cfg.name = prefs.getString("name", cfg.name);
  cfg.apPass = prefs.getString("appass", cfg.apPass);
  cfg.ledStyle = prefs.getUChar("lstyle", cfg.ledStyle);
  cfg.color1 = prefs.getUInt("col1", cfg.color1);
  cfg.color2 = prefs.getUInt("col2", cfg.color2);
  cfg.ledSpeed = prefs.getUShort("lspeed", cfg.ledSpeed);
  cfg.ledTail = prefs.getUChar("ltail", cfg.ledTail);
  cfg.ledMinutes = prefs.getUShort("lmin", cfg.ledMinutes);
  cfg.brightness = prefs.getUChar("bright", cfg.brightness);
  cfg.knobHaptics = prefs.getBool("hknob", cfg.knobHaptics);
  cfg.tickPower = prefs.getUChar("hpow", cfg.tickPower);
  cfg.tickMs = prefs.getUChar("hms", cfg.tickMs);
  cfg.notifyVibe = prefs.getBool("nvibe", cfg.notifyVibe);
  cfg.ntfTicks = prefs.getUChar("nticks", cfg.ntfTicks);
  cfg.ntfMsFirst = prefs.getUShort("nms0", cfg.ntfMsFirst);
  cfg.ntfMsLast = prefs.getUShort("nms1", cfg.ntfMsLast);
  cfg.ntfPowFirst = prefs.getUChar("npow0", cfg.ntfPowFirst);
  cfg.ntfPowLast = prefs.getUChar("npow1", cfg.ntfPowLast);
  cfg.ntfGap = prefs.getUShort("ngap", cfg.ntfGap);
  cfg.ntfRemindMin = prefs.getUShort("nrem", cfg.ntfRemindMin);
  cfg.rotation = prefs.getUChar("rot", cfg.rotation);
  cfg.knobReverse = prefs.getBool("krev", cfg.knobReverse);
  cfg.epdWired = prefs.getBool("epdwire", cfg.epdWired);
  cfg.epdSpiMHz = prefs.getUChar("epdspi", cfg.epdSpiMHz);
  if (cfg.epdSpiMHz != 1 && cfg.epdSpiMHz != 4 && cfg.epdSpiMHz != 10) cfg.epdSpiMHz = 1;
  prefs.end();
  if (cfg.apPass.length() < 8 || cfg.apPass.length() > 63) cfg.apPass = "messagebox";
  if (cfg.pollSec < 20) cfg.pollSec = 20;
  if (cfg.ledStyle >= STYLE_COUNT) cfg.ledStyle = STYLE_COMET;
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
  prefs.putString("name", cfg.name);
  prefs.putString("appass", cfg.apPass);
  prefs.putUChar("lstyle", cfg.ledStyle);
  prefs.putUInt("col1", cfg.color1);
  prefs.putUInt("col2", cfg.color2);
  prefs.putUShort("lspeed", cfg.ledSpeed);
  prefs.putUChar("ltail", cfg.ledTail);
  prefs.putUShort("lmin", cfg.ledMinutes);
  prefs.putUChar("bright", cfg.brightness);
  prefs.putBool("hknob", cfg.knobHaptics);
  prefs.putUChar("hpow", cfg.tickPower);
  prefs.putUChar("hms", cfg.tickMs);
  prefs.putBool("nvibe", cfg.notifyVibe);
  prefs.putUChar("nticks", cfg.ntfTicks);
  prefs.putUShort("nms0", cfg.ntfMsFirst);
  prefs.putUShort("nms1", cfg.ntfMsLast);
  prefs.putUChar("npow0", cfg.ntfPowFirst);
  prefs.putUChar("npow1", cfg.ntfPowLast);
  prefs.putUShort("ngap", cfg.ntfGap);
  prefs.putUShort("nrem", cfg.ntfRemindMin);
  prefs.putUChar("rot", cfg.rotation);
  prefs.putBool("krev", cfg.knobReverse);
  prefs.putBool("epdwire", cfg.epdWired);
  prefs.putUChar("epdspi", cfg.epdSpiMHz);
  prefs.end();
}

// ---------------------------------------------------------------- log (Serial + web page), any task

String logLines[16];
int logHead = 0;
SemaphoreHandle_t logLock;

void logf(const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  char stamp[12];
  snprintf(stamp, sizeof(stamp), "%6lus ", millis() / 1000);
  if (logLock) xSemaphoreTake(logLock, portMAX_DELAY);
  Serial.print(stamp);
  Serial.println(buf);
  logLines[logHead] = String(stamp) + buf;
  logHead = (logHead + 1) % 16;
  if (logLock) xSemaphoreGive(logLock);
}

String logText() {
  String s;
  if (logLock) xSemaphoreTake(logLock, portMAX_DELAY);
  for (int i = 0; i < 16; i++) {
    const String &l = logLines[(logHead + i) % 16];
    if (l.length()) s += l + "\n";
  }
  if (logLock) xSemaphoreGive(logLock);
  return s;
}
