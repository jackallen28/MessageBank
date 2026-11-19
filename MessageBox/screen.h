// The e-paper. Drawing runs in its own task, so the LED ring, haptics and knob keep going
// while the panel refreshes. The main loop describes what should be on screen (a View) and
// calls screenShow(); the task draws the newest View it has, skipping any it couldn't get to.
//
// The display uses SPI (1 MHz by default, a setting), and RESET on GPIO8 and BUSY on GPIO4 when
// "Screen wiring" is set to wired (the default, tested). Set to "not wired", the driver waits
// fixed, generous times instead of reading BUSY, and RES must be tied to 3V3. Routine
// reading and control changes use fast partial updates. Full refreshes happen
// on initialization, clearing to blank, entering setup, rotation changes, or
// an explicit full redraw/test pattern. No refresh-count cleanup is scheduled.
// Keep controller RAM available while blank; do not hibernate between sessions.
#pragma once
#include <vector>
#ifndef HOST_PREVIEW  // (HOST_PREVIEW: tools/preview_screens.cpp draws these screens on a PC)
#include <SPI.h>
#include <GxEPD2_BW.h>
#endif
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include "config.h"
#include "RefreshPolicy.h"

#ifndef HOST_PREVIEW
#include "MB_EPD370.h"
// Created in screenBegin(), once the settings say whether BUSY and RESET are wired.
typedef GxEPD2_BW<MB_EPD370, MB_EPD370::HEIGHT> EpdDisplay;
EpdDisplay *epdDisplay = nullptr;
#define epd (*epdDisplay)
#endif

enum FontId : uint8_t { F_REG9, F_BOLD9, F_REG12, F_BOLD12, F_BOLD18 };
const GFXfont *const FONTS[] = { &FreeSans9pt7b, &FreeSansBold9pt7b, &FreeSans12pt7b, &FreeSansBold12pt7b, &FreeSansBold18pt7b };

enum Screen : uint8_t { SCR_BLANK, SCR_ENVELOPE, SCR_READ, SCR_DECIDE, SCR_HAPPY, SCR_SETUP, SCR_TEXT, SCR_PATTERN };

struct Line {
  String text;
  uint8_t font;
};

struct View {
  Screen scr = SCR_BLANK;
  int count = 0;            // envelope badge
  String from, when, title; // read/decide header
  int pos = 0, total = 0;   // "2/5"
  std::vector<Line> lines;  // read: the laid-out message
  int scroll = 0;           // read: first line shown
  bool savedList = false;   // reading saved messages (not new ones)
  int8_t choice = 0;        // decide: -1 delete, +1 save, 0 not chosen yet
  String text;              // setup/text screens
};

// ---------------------------------------------------------------- sizes and text layout

int scrW() { return (cfg.rotation & 1) ? 416 : 240; }
int scrH() { return (cfg.rotation & 1) ? 240 : 416; }
const int HEADER_H = 30, FOOTER_H = 26, MARGIN = 10, SCROLLBAR_W = 6;
int contentTop() { return HEADER_H + 6; }
int contentBottom() { return scrH() - FOOTER_H - 2; }
int textWidthAvail() { return scrW() - 2 * MARGIN - SCROLLBAR_W; }
int lineHeight(uint8_t f) { return FONTS[f]->yAdvance; }

// Width in pixels, from the font's own glyph table (no drawing, safe from any task).
int textWidth(const String &s, uint8_t f) {
  const GFXfont *font = FONTS[f];
  int w = 0;
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c < font->first || c > font->last) c = '?';
    w += font->glyph[c - font->first].xAdvance;
  }
  return w;
}

void wrapInto(std::vector<Line> &out, const String &text, uint8_t f, int width) {
  size_t i = 0;
  while (i <= text.length()) {
    int nl = text.indexOf('\n', i);
    if (nl < 0) nl = text.length();
    String para = text.substring(i, nl);
    i = nl + 1;
    String line;
    size_t p = 0;
    while (p < para.length()) {
      int sp = para.indexOf(' ', p);
      if (sp < 0) sp = para.length();
      String word = para.substring(p, sp);
      p = sp + 1;
      if (!word.length()) continue;
      String trial = line.length() ? line + " " + word : word;
      if (textWidth(trial, f) <= width) { line = trial; continue; }
      if (line.length()) out.push_back({ line, f });
      line = word;
      while (textWidth(line, f) > width && line.length() > 1) {  // a word wider than the screen
        int cut = line.length() - 1;
        while (cut > 1 && textWidth(line.substring(0, cut), f) > width) cut--;
        out.push_back({ line.substring(0, cut), f });
        line = line.substring(cut);
      }
    }
    out.push_back({ line, f });  // also keeps empty lines (paragraph gaps)
    if (nl >= (int)text.length()) break;
  }
  while (out.size() && out.back().text.length() == 0) out.pop_back();
}

int linesHeight(const std::vector<Line> &v, int from, int to) {
  int h = 0;
  for (int i = from; i < to && i < (int)v.size(); i++) h += lineHeight(v[i].font);
  return h;
}

// The subject (bold) then the text. Short notes get the big font.
std::vector<Line> layoutMessage(const String &subject, const String &body) {
  std::vector<Line> out;
  int w = textWidthAvail(), room = contentBottom() - contentTop();
  bool hasSubject = subject.length() && subject != "(no subject)";
  String text = body.length() ? body : (hasSubject ? "" : "(empty message)");
  if (text.length() <= 80) {
    std::vector<Line> big;
    if (hasSubject) wrapInto(big, subject, F_BOLD12, w);
    if (text.length()) wrapInto(big, text, F_BOLD18, w);
    if (linesHeight(big, 0, big.size()) <= room) return big;
  }
  if (hasSubject) {
    wrapInto(out, subject, F_BOLD12, w);
    out.push_back({ "", F_REG9 });  // a small gap
  }
  if (text.length()) wrapInto(out, text, F_REG12, w);
  return out;
}

// The largest first line that still lets the screen be full.
int maxScroll(const std::vector<Line> &v) {
  int room = contentBottom() - contentTop(), h = 0;
  for (int i = (int)v.size() - 1; i >= 0; i--) {
    h += lineHeight(v[i].font);
    if (h > room) return i + 1;
  }
  return 0;
}

// How many lines fit from 'first'.
int linesThatFit(const std::vector<Line> &v, int first) {
  int room = contentBottom() - contentTop(), h = 0, n = 0;
  for (int i = first; i < (int)v.size(); i++) {
    h += lineHeight(v[i].font);
    if (h > room) break;
    n++;
  }
  return n;
}

// ---------------------------------------------------------------- pixel art

// 26 x 18 cell envelope; the seal in the middle is a heart, or a smiley when happy.
static const char *HEART[6] = { ".##..##.", "########", "########", ".######.", "..####..", "...##..." };
static const char *SMILEY[8] = { "..####..", ".#....#.", "#.#..#.#", "#......#", "#.#..#.#", "#..##..#", ".#....#.", "..####.." };

void drawEnvelope(int ox, int oy, int S, bool happy) {
  auto cell = [&](int x, int y, uint16_t c) { epd.fillRect(ox + x * S, oy + y * S, S, S, c); };
  for (int x = 0; x < 26; x++) { cell(x, 0, GxEPD_BLACK); cell(x, 17, GxEPD_BLACK); }
  for (int y = 0; y < 18; y++) { cell(0, y, GxEPD_BLACK); cell(25, y, GxEPD_BLACK); }
  for (int k = 1; k <= 12; k++) { cell(k, k, GxEPD_BLACK); cell(25 - k, k, GxEPD_BLACK); }  // the flap
  for (int k = 1; k <= 6; k++) { cell(k, 17 - k, GxEPD_BLACK); cell(25 - k, 17 - k, GxEPD_BLACK); }  // bottom folds
  // the seal: a white patch where the flap meets, with a heart or a smiley on it
  int rows = happy ? 8 : 6, top = happy ? 8 : 9;
  for (int y = top - 1; y <= top + rows; y++)
    for (int x = 8; x <= 17; x++) cell(x, y, GxEPD_WHITE);
  const char **art = happy ? SMILEY : HEART;
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < 8; x++)
      if (art[y][x] == '#') cell(9 + x, top + y, GxEPD_BLACK);
  if (happy) {  // two little hearts floating above
    for (int k = 0; k < 2; k++) {
      int hx = ox + (k ? 21 : 3) * S, hy = oy - (k ? 5 : 3) * S, s = max(2, S / 2);
      for (int y = 0; y < 6; y++)
        for (int x = 0; x < 8; x++)
          if (HEART[y][x] == '#') epd.fillRect(hx + x * s, hy + y * s, s, s, GxEPD_BLACK);
    }
  }
}

void drawTrash(int cx, int cy, int s, uint16_t c) {  // pixel bin
  static const char *art[9] = { "..###..", "#######", ".......", ".#####.", ".#.#.#.", ".#.#.#.", ".#.#.#.", ".#.#.#.", ".#####." };
  for (int y = 0; y < 9; y++)
    for (int x = 0; x < 7; x++)
      if (art[y][x] == '#') epd.fillRect(cx - 7 * s / 2 + x * s, cy - 9 * s / 2 + y * s, s, s, c);
}

void drawHeart(int cx, int cy, int s, uint16_t c) {
  for (int y = 0; y < 6; y++)
    for (int x = 0; x < 8; x++)
      if (HEART[y][x] == '#') epd.fillRect(cx - 4 * s + x * s, cy - 3 * s + y * s, s, s, c);
}

// ---------------------------------------------------------------- drawing helpers

void printAt(int x, int y, const String &s, uint8_t f, uint16_t c = GxEPD_BLACK) {
  epd.setFont(FONTS[f]);
  epd.setTextColor(c);
  epd.setCursor(x, y);
  epd.print(s);
}

void printCentered(int y, const String &s, uint8_t f, uint16_t c = GxEPD_BLACK, int cx = -1) {
  if (cx < 0) cx = scrW() / 2;
  printAt(cx - textWidth(s, f) / 2, y, s, f, c);
}

// Cuts text to fit a width, with "..." if it was too long.
String fit(const String &s, uint8_t f, int w) {
  if (textWidth(s, f) <= w) return s;
  String t = s;
  while (t.length() && textWidth(t + "...", f) > w) t.remove(t.length() - 1);
  return t + "...";
}

void drawHeader(const View &v) {
  int W = scrW();
  epd.fillRect(0, 0, W, HEADER_H, GxEPD_BLACK);
  String right = String(v.pos) + "/" + String(v.total);
  int rw = textWidth(right, F_BOLD9);
  printAt(W - MARGIN - rw, 21, right, F_BOLD9, GxEPD_WHITE);
  String left = (v.savedList ? "Saved: " : "From ") + v.from;
  printAt(MARGIN, 21, fit(left, F_BOLD9, W - 3 * MARGIN - rw), F_BOLD9, GxEPD_WHITE);
}

void drawFooter(const String &left, const String &right, bool arrowDown) {
  int W = scrW(), H = scrH();
  epd.drawFastHLine(MARGIN, H - FOOTER_H, W - 2 * MARGIN, GxEPD_BLACK);
  int rw = textWidth(right, F_BOLD9);
  int x = W - MARGIN - rw - (arrowDown ? 16 : 0);
  printAt(MARGIN, H - 8, fit(left, F_REG9, x - MARGIN - 8), F_REG9);
  printAt(x, H - 8, right, F_BOLD9);
  if (arrowDown) epd.fillTriangle(W - MARGIN - 12, H - 17, W - MARGIN, H - 17, W - MARGIN - 6, H - 9, GxEPD_BLACK);
}

// ---------------------------------------------------------------- the screens

void drawEnvelopeScreen(const View &v, bool happy) {
  int W = scrW(), H = scrH();
  bool land = cfg.rotation & 1;
  int S = land ? 6 : 8;
  int ew = 26 * S, eh = 18 * S;
  int ox = (W - ew) / 2, oy = land ? 44 : 120;
  drawEnvelope(ox, oy, S, happy);
  if (!happy) {
    // the badge: how many new messages, at the top right corner
    int bx = ox + ew - 4, by = oy + 2, r = 24;
    epd.fillCircle(bx, by, r + 3, GxEPD_WHITE);
    epd.fillCircle(bx, by, r, GxEPD_BLACK);
    String n = v.count > 99 ? String("99") : String(v.count);
    printCentered(by + 9, n, F_BOLD18, GxEPD_WHITE, bx);
    int ty = oy + eh + (land ? 30 : 50);
    printCentered(ty, v.count == 1 ? String("1 new message") : String(v.count) + " new messages", F_BOLD12);
    printCentered(ty + (land ? 22 : 30), "press the knob to read", F_REG9);
  } else {
    int ty = oy + eh + (land ? 34 : 56);
    printCentered(ty, "All read!", F_BOLD18);
  }
}

void drawReadScreen(const View &v) {
  int W = scrW();
  drawHeader(v);
  int y = contentTop();
  int n = linesThatFit(v.lines, v.scroll);
  int used = linesHeight(v.lines, v.scroll, v.scroll + n), room0 = contentBottom() - contentTop();
  if (v.scroll == 0 && n == (int)v.lines.size() && used < room0 / 2) y += (room0 - used) / 3;  // a short note sits nearer the middle
  for (int i = v.scroll; i < v.scroll + n; i++) {
    const Line &l = v.lines[i];
    y += lineHeight(l.font);
    if (l.text.length()) printAt(MARGIN, y - lineHeight(l.font) / 4 - 2, l.text, l.font);
  }
  // scroll bar
  int total = linesHeight(v.lines, 0, v.lines.size());
  int room = contentBottom() - contentTop();
  bool more = v.scroll + n < (int)v.lines.size();
  if (total > room) {
    int top = contentTop(), h = room;
    int above = linesHeight(v.lines, 0, v.scroll);
    int barH = max(16, h * room / total);
    int barY = top + (h - barH) * above / max(1, total - room);
    epd.drawRect(W - SCROLLBAR_W - 2, top, SCROLLBAR_W, h, GxEPD_BLACK);
    epd.fillRect(W - SCROLLBAR_W - 2, barY, SCROLLBAR_W, barH, GxEPD_BLACK);
  }
  drawFooter(v.when, more ? "turn for more" : "press: next", more);
}

void drawChoiceBox(int x, int y, int w, int h, bool selected, bool del, const String &label, const String &hint) {
  uint16_t fg = selected ? GxEPD_WHITE : GxEPD_BLACK;
  if (selected) epd.fillRoundRect(x, y, w, h, 10, GxEPD_BLACK);
  else {
    epd.drawRoundRect(x, y, w, h, 10, GxEPD_BLACK);
    epd.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, GxEPD_BLACK);
  }
  if (h < 150) {  // short box (landscape): icon on the left, words on the right
    int ix = x + 34, tx = x + 34 + (w - 34) / 2 + 8;
    if (del) drawTrash(ix, y + h / 2, 5, fg);
    else drawHeart(ix, y + h / 2, 5, fg);
    printCentered(y + h / 2 + 2, label, F_BOLD12, fg, tx);
    printCentered(y + h / 2 + 24, hint, F_REG9, fg, tx);
    return;
  }
  int cx = x + w / 2;
  if (del) drawTrash(cx, y + h / 2 - 22, 5, fg);
  else drawHeart(cx, y + h / 2 - 22, 6, fg);
  printCentered(y + h / 2 + 30, label, F_BOLD12, fg, cx);
  printCentered(y + h - 12, hint, F_REG9, fg, cx);
}

void drawDecideScreen(const View &v) {
  int W = scrW(), H = scrH();
  bool land = cfg.rotation & 1;
  drawHeader(v);
  printCentered(contentTop() + (land ? 24 : 34), fit(v.title, F_BOLD12, W - 2 * MARGIN), F_BOLD12);
  printCentered(contentTop() + (land ? 46 : 62), "Keep this message?", F_REG12);
  int top = contentTop() + (land ? 60 : 92), bh = min(land ? 130 : 230, contentBottom() - top - 10);
  int gap = 12, bw = (W - 2 * MARGIN - gap) / 2;
  drawChoiceBox(MARGIN, top, bw, bh, v.choice < 0, true, "DELETE", "<< turn left");
  drawChoiceBox(MARGIN + bw + gap, top, bw, bh, v.choice > 0, false, "SAVE", "turn right >>");
  drawFooter("", v.choice ? String("press to confirm") : String("turn to choose"), false);
  (void)H;
}

void drawSetupScreen(const View &v) {
  int W = scrW();
  epd.fillRect(0, 0, W, 40, GxEPD_BLACK);
  printCentered(28, "MessageBox setup", F_BOLD12, GxEPD_WHITE);
  std::vector<Line> lines;
  wrapInto(lines, v.text, F_REG12, W - 2 * MARGIN);
  int y = 48;
  for (const Line &l : lines) {
    y += lineHeight(F_REG12);
    if (y > scrH() - 4) break;
    printAt(MARGIN, y - 6, l.text, F_REG12);
  }
}

void drawPattern() {
  int W = scrW(), H = scrH();
  epd.drawRect(0, 0, W, H, GxEPD_BLACK);
  epd.drawRect(3, 3, W - 6, H - 6, GxEPD_BLACK);
  for (int i = 0; i < 8; i++) epd.fillRect(10 + i * ((W - 20) / 8), 10, (W - 20) / 16, 30, GxEPD_BLACK);
  epd.drawLine(0, 0, W - 1, H - 1, GxEPD_BLACK);
  epd.drawLine(W - 1, 0, 0, H - 1, GxEPD_BLACK);
  epd.fillCircle(W / 2, H / 2, 30, GxEPD_BLACK);
  printCentered(H / 2 + 64, String(W) + " x " + String(H), F_BOLD12);
  printCentered(H - 16, "all four edges should show", F_REG9);
}

// Draws a View into the picture in RAM (no refresh).
void drawView(const View &v) {
  epd.setRotation(cfg.rotation);
  epd.fillScreen(GxEPD_WHITE);
  switch (v.scr) {
    case SCR_BLANK: break;
    case SCR_ENVELOPE: drawEnvelopeScreen(v, false); break;
    case SCR_HAPPY: drawEnvelopeScreen(v, true); break;
    case SCR_READ: drawReadScreen(v); break;
    case SCR_DECIDE: drawDecideScreen(v); break;
    case SCR_SETUP: drawSetupScreen(v); break;
    case SCR_TEXT: {
      std::vector<Line> lines;
      wrapInto(lines, v.text, F_BOLD12, scrW() - 2 * MARGIN);
      int y = 8;
      for (const Line &l : lines) { y += lineHeight(F_BOLD12); printAt(MARGIN, y - 6, l.text, F_BOLD12); }
      break;
    }
    case SCR_PATTERN: drawPattern(); break;
  }
}

// ---------------------------------------------------------------- the drawing task
#ifndef HOST_PREVIEW

SemaphoreHandle_t viewLock;
TaskHandle_t screenTask;
View pending;
bool pendingDirty = false;
bool pendingForceFull = false;
volatile uint32_t lastRefreshMs = 0;
volatile uint32_t refreshCount = 0;
volatile uint32_t fullRefreshCount = 0;
volatile uint32_t partialRefreshCount = 0;
volatile bool lastRefreshFull = false;

DisplayPage pageKind(Screen screen) {
  if (screen == SCR_BLANK) return DisplayPage::Blank;
  if (screen == SCR_SETUP) return DisplayPage::Setup;
  if (screen == SCR_PATTERN) return DisplayPage::Pattern;
  return DisplayPage::Content;
}

void screenShow(const View &v, bool forceFull = false) {
  if (!screenTask || !viewLock) return;
  xSemaphoreTake(viewLock, portMAX_DELAY);
  pending = v;
  pendingDirty = true;
  // This intent belongs to the newest queued view. A superseded diagnostic
  // frame must not force a full flash on a later routine reading frame.
  pendingForceFull = forceFull;
  xSemaphoreGive(viewLock);
  xTaskNotifyGive(screenTask);
}

void screenTaskFn(void *) {
  RefreshPolicy policy;
  Screen lastScreen = SCR_BLANK;
  String lastFrom;
  int lastPos = 0, lastTotal = 0;
  bool lastSavedList = false;
  View v;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      bool requestedFull;
      xSemaphoreTake(viewLock, portMAX_DELAY);
      if (!pendingDirty) { xSemaphoreGive(viewLock); break; }
      v = pending;
      pendingDirty = false;
      requestedFull = pendingForceFull;
      pendingForceFull = false;
      xSemaphoreGive(viewLock);

      const uint8_t rotation = cfg.rotation;
      const DisplayPage page = pageKind(v.scr);
      const RefreshMode mode = policy.plan(page, requestedFull, rotation);
      if (mode == RefreshMode::Skip) continue;
      const bool sameHeader = v.scr == lastScreen &&
          (v.scr == SCR_READ || v.scr == SCR_DECIDE) &&
          v.from == lastFrom && v.pos == lastPos && v.total == lastTotal &&
          v.savedList == lastSavedList;
      const uint32_t t0 = millis();
      drawView(v);
      if (mode == RefreshMode::Full) {
        epd.display(false);
        fullRefreshCount = fullRefreshCount + 1;
      } else if (sameHeader) {
        // Start at y=24 to include the bottom of the 30px header. All
        // dimensions remain byte-aligned in both portrait and landscape.
        epd.displayWindow(0, 24, epd.width(), epd.height() - 24);
        partialRefreshCount = partialRefreshCount + 1;
      } else {
        epd.display(true);  // Fast waveform even when the whole view changes.
        partialRefreshCount = partialRefreshCount + 1;
      }
      // The driver powers the panel's high voltage off after each refresh.
      // Retain controller RAM while blank so the next arrival stays partial.
      policy.commit(page, rotation);
      lastScreen = v.scr;
      lastFrom = v.from;
      lastPos = v.pos;
      lastTotal = v.total;
      lastSavedList = v.savedList;
      lastRefreshMs = millis() - t0;
      lastRefreshFull = mode == RefreshMode::Full;
      refreshCount = refreshCount + 1;
    }
  }
}

void screenBegin() {
  viewLock = xSemaphoreCreateMutex();
  if (!viewLock || !SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS)) {
    logf("Screen initialization failed; Wi-Fi setup continues");
    return;
  }
  const bool wired = cfg.epdWired;
  if (wired) pinMode(PIN_EPD_BUSY, INPUT_PULLUP);
  epdDisplay = new EpdDisplay(MB_EPD370(PIN_EPD_CS, PIN_EPD_DC, wired ? PIN_EPD_RST : -1, wired ? PIN_EPD_BUSY : -1));
  if (!epdDisplay) {
    logf("Not enough memory for the screen; Wi-Fi setup continues");
    return;
  }
  const uint32_t spiHz = (uint32_t)cfg.epdSpiMHz * 1000000UL;
  logf("Screen: %s, %u MHz SPI", wired ? "BUSY on GPIO4 + RESET on GPIO8" : "no BUSY/RESET wires (fixed timing)",
       (unsigned)cfg.epdSpiMHz);
  delay(200);
  epd.init(0, true, 50, false, SPI, SPISettings(spiHz, MSBFIRST, SPI_MODE0));
  epd.setFullWindow();
  if (xTaskCreate(screenTaskFn, "screen", 8192, nullptr, 1, &screenTask) != pdPASS) {
    screenTask = nullptr;
    logf("Screen task allocation failed; Wi-Fi setup continues");
  }
}
#endif  // HOST_PREVIEW
