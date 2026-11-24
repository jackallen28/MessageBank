// The e-paper. Drawing runs in its own task, so the LED ring, haptics and knob keep going
// while the panel refreshes. The main loop describes what should be on screen (a View) and
// calls screenShow(); the task draws the newest View it has, skipping any it couldn't get to.
//
// The display uses SPI (1 MHz by default, a setting), and RESET on GPIO8 and BUSY on GPIO4 when
// "Screen wiring" is set to wired (the default, tested). The driver uses fixed waits if BUSY
// is unwired or never asserts. With RESET unwired, RES must be tied to 3V3. Routine
// reading and control changes use fast partial updates. Full refreshes happen
// on initialization, clearing to blank, entering setup, rotation changes, or
// an explicit full redraw/test pattern. Recovery also uses a full refresh.
// Keep controller RAM available while blank; do not hibernate between sessions.
#pragma once
#include <memory>
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
#include "MailImage.h"
#include "RefreshPolicy.h"
#include "reactions.h"

#ifndef HOST_PREVIEW
#include "MB_EPD370.h"
// Created in screenBegin(), once the settings say whether BUSY and RESET are wired.
typedef GxEPD2_BW<MB_EPD370, MB_EPD370::HEIGHT> EpdDisplay;
EpdDisplay *epdDisplay = nullptr;
#define epd (*epdDisplay)
#endif

enum FontId : uint8_t { F_REG9, F_BOLD9, F_REG12, F_BOLD12, F_BOLD18 };
const GFXfont *const FONTS[] = { &FreeSans9pt7b, &FreeSansBold9pt7b, &FreeSans12pt7b, &FreeSansBold12pt7b, &FreeSansBold18pt7b };

enum Screen : uint8_t { SCR_BLANK, SCR_ENVELOPE, SCR_READ, SCR_PICTURE, SCR_DECIDE, SCR_HAPPY, SCR_SETUP, SCR_TEXT, SCR_PATTERN,
                        SCR_ARRIVE, SCR_NOMSG, SCR_CAROUSEL, SCR_REACT, SCR_SENT, SCR_MENU, SCR_SLEEP };

const int ARRIVE_FRAMES = 6;

struct MenuItem {
  String label, value;
};

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
  std::shared_ptr<const MailImage> picture;
  bool pictureOnly = false;
  int scroll = 0;           // read: first line shown
  bool savedList = false;   // reading saved messages (not new ones)
  int8_t choice = 0;        // decide: -1 delete, +1 save, 0 not chosen yet
  String text;              // setup/text screens
  int frame = 0;            // arrival animation frame
  int8_t reaction = -1;     // react: the one selected; sent: the one sent
  int8_t sentReaction = -1; // react/carousel: the reaction already sent for this message
  bool replyQueued = false; // sent: waiting for Wi-Fi
  int savedCount = 0;       // no-messages screen
  std::vector<MenuItem> menu;
  int menuSel = 0;
  bool menuEditing = false;
};

// ---------------------------------------------------------------- sizes and text layout

int scrW() { return (cfg.rotation & 1) ? 416 : 240; }
int scrH() { return (cfg.rotation & 1) ? 240 : 416; }
const int HEADER_H = 30, FOOTER_H = 26, MARGIN = 10, SCROLLBAR_W = 6;
int contentTop() { return HEADER_H + 6; }
int contentBottom(int height) { return height - FOOTER_H - 2; }
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
  int w = textWidthAvail(), room = contentBottom(scrH()) - contentTop();
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
  int room = contentBottom(scrH()) - contentTop(), h = 0;
  for (int i = (int)v.size() - 1; i >= 0; i--) {
    h += lineHeight(v[i].font);
    if (h > room) return i + 1;
  }
  return 0;
}

// How many lines fit from 'first'.
int linesThatFit(const std::vector<Line> &v, int first, int room) {
  int h = 0, n = 0;
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

// The hummingbird (32 x 20 cells), wings up and wings swept back. Its beak tip is cell (31, 7).
static const char *const BIRD_UP[20] = {
  ".....##.........................", ".....###........................", "......###.......................",
  "......####......................", ".......####.....................", ".......#####....####............",
  "........#####..######...........", ".........####.###.#############.", "..........###.###########.......",
  "..............##########........", "...........###########..........", "..........##########............",
  ".........#########..............", "........########................", ".......######...................",
  ".....#######....................", "...####..##.....................", "..###.....#.....................",
  "..#.............................", "................................" };
static const char *const BIRD_DOWN[20] = {
  "................................", "................................", "................................",
  "................................", "................................", "................####............",
  "...............######...........", "..............###.#############.", "..#######################.......",
  "....####################........", ".......###############..........", "..........##########............",
  ".........#########..............", "........########................", ".......######...................",
  ".....#######....................", "...####..##.....................", "..###.....#.....................",
  "..#.............................", "................................" };

void drawBird(int x, int y, int s, bool wingsUp) {
  const char *const *art = wingsUp ? BIRD_UP : BIRD_DOWN;
  // a white outline first, so the bird stands out in front of the envelope's lines
  for (int r = 0; r < 20; r++)
    for (int c = 0; c < 32; c++)
      if (art[r][c] == '#') epd.fillRect(x + (c - 1) * s, y + (r - 1) * s, 3 * s, 3 * s, GxEPD_WHITE);
  for (int r = 0; r < 20; r++)
    for (int c = 0; c < 32; c++)
      if (art[r][c] == '#') epd.fillRect(x + c * s, y + r * s, s, s, GxEPD_BLACK);
}

// A line of cells (for the pixel-art envelopes).
void cellLine(int ox, int oy, int S, int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    epd.fillRect(ox + x0 * S, oy + y0 * S, S, S, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// The open envelope: flap up, a letter with a heart standing out of it by 'rise' cells.
// (ox, oy) is the top-left of the envelope's body, which is 26 x 18 cells; the flap reaches
// 9 cells above it.
void drawOpenEnvelope(int ox, int oy, int S, int rise) {
  auto cell = [&](int x, int y, uint16_t c) { epd.fillRect(ox + x * S, oy + y * S, S, S, c); };
  cellLine(ox, oy, S, 0, 0, 13, -9, GxEPD_BLACK);  // the open flap, behind everything
  cellLine(ox, oy, S, 25, 0, 12, -9, GxEPD_BLACK);
  // the letter
  int top = -rise;
  epd.fillRect(ox + 3 * S, oy + top * S, 20 * S, (14 - top) * S, GxEPD_WHITE);
  for (int x = 3; x <= 22; x++) cell(x, top, GxEPD_BLACK);
  for (int y = top; y <= 13; y++) { cell(3, y, GxEPD_BLACK); cell(22, y, GxEPD_BLACK); }
  static const char *HEART6[6] = { ".##..##.", "########", "########", ".######.", "..####..", "...##..." };
  for (int y = 0; y < 6; y++)
    for (int x = 0; x < 8; x++)
      if (HEART6[y][x] == '#') cell(9 + x, top + 2 + y, GxEPD_BLACK);
  // the back of the envelope: sides and bottom; then the front pocket over the letter
  for (int y = 0; y < 18; y++) { cell(0, y, GxEPD_BLACK); cell(25, y, GxEPD_BLACK); }
  for (int x = 0; x < 26; x++) cell(x, 17, GxEPD_BLACK);
  for (int y = 1; y < 17; y++)  // the front pocket: white below a V from the top corners
    for (int x = 1; x < 25; x++)
      if (y * 25 >= abs(x * 2 - 25) * 10 + 15 && y >= 1) {
        int vy = (25 - abs(x * 2 - 25)) * 10 / 25;  // the V's height at this column
        if (y > vy) cell(x, y, GxEPD_WHITE);
      }
  cellLine(ox, oy, S, 0, 0, 12, 10, GxEPD_BLACK);   // the pocket's V
  cellLine(ox, oy, S, 25, 0, 13, 10, GxEPD_BLACK);
}

void drawReactionIcon(int x, int y, int i, int scale, uint16_t c) {
  if (i < 0 || i >= REACTION_COUNT) return;
  const uint8_t *bmp = REACTIONS[i].bitmap;
  const int stride = (REACTION_PX + 7) / 8;
  for (int r = 0; r < REACTION_PX; r++)
    for (int b = 0; b < REACTION_PX; b++)
      if (pgm_read_byte(bmp + r * stride + b / 8) & (0x80 >> (b % 8))) epd.fillRect(x + b * scale, y + r * scale, scale, scale, c);
}

// ---------------------------------------------------------------- drawing helpers

void printAt(int x, int y, const String &s, uint8_t f, uint16_t c = GxEPD_BLACK) {
  epd.setFont(FONTS[f]);
  epd.setTextColor(c);
  epd.setCursor(x, y);
  epd.print(s);
}

void printCentered(int y, const String &s, uint8_t f, uint16_t c = GxEPD_BLACK, int cx = -1) {
  if (cx < 0) cx = epd.width() / 2;
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
  int W = epd.width();
  epd.fillRect(0, 0, W, HEADER_H, GxEPD_BLACK);
  String right = String(v.pos) + "/" + String(v.total);
  int rw = textWidth(right, F_BOLD9);
  printAt(W - MARGIN - rw, 21, right, F_BOLD9, GxEPD_WHITE);
  String left = "From " + v.from;
  printAt(MARGIN, 21, fit(left, F_BOLD9, W - 3 * MARGIN - rw), F_BOLD9, GxEPD_WHITE);
}

void drawFooter(const String &left, const String &right, bool arrowDown) {
  int W = epd.width(), H = epd.height();
  epd.drawFastHLine(MARGIN, H - FOOTER_H, W - 2 * MARGIN, GxEPD_BLACK);
  int rw = textWidth(right, F_BOLD9);
  int x = W - MARGIN - rw - (arrowDown ? 16 : 0);
  printAt(MARGIN, H - 8, fit(left, F_REG9, x - MARGIN - 8), F_REG9);
  printAt(x, H - 8, right, F_BOLD9);
  if (arrowDown) epd.fillTriangle(W - MARGIN - 12, H - 17, W - MARGIN, H - 17, W - MARGIN - 6, H - 9, GxEPD_BLACK);
}

// ---------------------------------------------------------------- the screens

void drawBadge(int bx, int by, int count) {
  int r = 24;
  epd.fillCircle(bx, by, r + 3, GxEPD_WHITE);
  epd.fillCircle(bx, by, r, GxEPD_BLACK);
  String n = count > 99 ? String("99") : String(count);
  printCentered(by + 9, n, F_BOLD18, GxEPD_WHITE, bx);
}

void drawEnvelopeScreen(const View &v, bool happy) {
  int W = epd.width(), H = epd.height();
  bool land = W > H;
  if (!happy) {  // a message is waiting: the open envelope, the letter showing, and the count
    int S = land ? 5 : 8;
    int ew = 26 * S, eh = 18 * S;
    int ox = (W - ew) / 2, oy = land ? 58 : 150;
    drawOpenEnvelope(ox, oy, S, 6);
    drawBadge(ox + ew - 2, oy - 2, v.count);
    int ty = oy + eh + (land ? 28 : 50);
    printCentered(ty, v.count == 1 ? String("1 new message") : String(v.count) + " new messages", F_BOLD12);
    printCentered(ty + (land ? 22 : 30), "press the knob to read", F_REG9);
    return;
  }
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
  int W = epd.width();
  drawHeader(v);
  int y = contentTop();
  int room = contentBottom(epd.height()) - contentTop();
  int n = linesThatFit(v.lines, v.scroll, room);
  int used = linesHeight(v.lines, v.scroll, v.scroll + n);
  if (v.scroll == 0 && n == (int)v.lines.size() && used < room / 2) y += (room - used) / 3;  // a short note sits nearer the middle
  for (int i = v.scroll; i < v.scroll + n; i++) {
    const Line &l = v.lines[i];
    y += lineHeight(l.font);
    if (l.text.length()) printAt(MARGIN, y - lineHeight(l.font) / 4 - 2, l.text, l.font);
  }
  // scroll bar
  int total = linesHeight(v.lines, 0, v.lines.size());
  bool more = v.scroll + n < (int)v.lines.size();
  if (total > room) {
    int top = contentTop(), h = room;
    int above = linesHeight(v.lines, 0, v.scroll);
    int barH = max(16, h * room / total);
    int barY = top + (h - barH) * min(above, total - room) / (total - room);
    epd.drawRect(W - SCROLLBAR_W - 2, top, SCROLLBAR_W, h, GxEPD_BLACK);
    epd.fillRect(W - SCROLLBAR_W - 2, barY, SCROLLBAR_W, barH, GxEPD_BLACK);
  }
  const bool hasPicture = v.picture && v.picture->valid();
  const char *hint = "turn to reply";
  if (more) hint = "turn for more";
  else if (hasPicture) hint = "turn for picture";
  drawFooter(more || hasPicture ? v.when : String("press: done   hold: delete"), hint, true);
}

void drawPictureScreen(const View &v) {
  drawHeader(v);
  const int bodyHeight = contentBottom(epd.height()) - contentTop();
  if (v.picture && v.picture->valid() &&
      v.picture->width <= epd.width() && v.picture->height <= bodyHeight) {
    const MailImage &picture = *v.picture;
    const int left = (epd.width() - picture.width) / 2;
    const int top = contentTop() + (bodyHeight - picture.height) / 2;
    const size_t stride = picture.stride();
    for (int y = 0; y < picture.height; y++) {
      const uint8_t *row = picture.pixels.data() + y * stride;
      for (int x = 0; x < picture.width; x++) {
        if (row[x / 8] & (0x80 >> (x % 8))) epd.drawPixel(left + x, top + y, GxEPD_BLACK);
      }
    }
  } else {
    printCentered(contentTop() + bodyHeight / 2, "Picture unavailable", F_BOLD12);
  }
  drawFooter("press: done   hold: delete", "turn to reply", true);
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
  int W = epd.width(), H = epd.height();
  bool land = W > H;
  drawHeader(v);
  printCentered(contentTop() + (land ? 24 : 34), fit(v.title, F_BOLD12, W - 2 * MARGIN), F_BOLD12);
  printCentered(contentTop() + (land ? 46 : 62), "Keep this message?", F_REG12);
  int top = contentTop() + (land ? 60 : 92), bh = min(land ? 130 : 230, contentBottom(H) - top - 10);
  int gap = 12, bw = (W - 2 * MARGIN - gap) / 2;
  drawChoiceBox(MARGIN, top, bw, bh, v.choice < 0, true, "DELETE", "<< turn left");
  drawChoiceBox(MARGIN + bw + gap, top, bw, bh, v.choice > 0, false, "SAVE", "turn right >>");
  drawFooter("", v.choice ? String("press to confirm") : String("turn to choose"), false);
}

void drawSetupScreen(const View &v) {
  int W = epd.width();
  epd.fillRect(0, 0, W, 40, GxEPD_BLACK);
  printCentered(28, "MessageBox setup", F_BOLD12, GxEPD_WHITE);
  std::vector<Line> lines;
  wrapInto(lines, v.text, F_REG12, W - 2 * MARGIN);
  int y = 48;
  for (const Line &l : lines) {
    y += lineHeight(F_REG12);
    if (y > epd.height() - 4) break;
    printAt(MARGIN, y - 6, l.text, F_REG12);
  }
}

void drawPattern() {
  int W = epd.width(), H = epd.height();
  epd.drawRect(0, 0, W, H, GxEPD_BLACK);
  epd.drawRect(3, 3, W - 6, H - 6, GxEPD_BLACK);
  for (int i = 0; i < 8; i++) epd.fillRect(10 + i * ((W - 20) / 8), 10, (W - 20) / 16, 30, GxEPD_BLACK);
  epd.drawLine(0, 0, W - 1, H - 1, GxEPD_BLACK);
  epd.drawLine(W - 1, 0, 0, H - 1, GxEPD_BLACK);
  epd.fillCircle(W / 2, H / 2, 30, GxEPD_BLACK);
  printCentered(H / 2 + 64, String(W) + " x " + String(H), F_BOLD12);
  printCentered(H - 16, "all four edges should show", F_REG9);
}

// The hummingbird delivery: it flies in, pecks the seal twice, the envelope pops open.
void drawArriveScreen(const View &v) {
  int W = epd.width(), H = epd.height();
  bool land = W > H;
  int S = land ? 6 : 8, B = 3;
  int ew = 26 * S;
  int ox = (W - ew) / 2, oy = land ? 78 : 160;
  int sealX = ox + 13 * S, sealY = oy + 11 * S;          // where the flap meets
  int peckX = sealX - 31 * B, peckY = sealY - 7 * B - B / 2;  // bird position with its beak on the seal
  const int f = constrain(v.frame, 0, ARRIVE_FRAMES - 1);
  if (f < 4) drawEnvelope(ox, oy, S, false);
  else drawOpenEnvelope(ox, oy + 2 * S, land ? 5 : S, f == 4 ? 3 : 6);
  switch (f) {
    case 0: drawBird(-20, land ? 8 : 40, B, true); break;
    case 1: drawBird((peckX - 20) / 2 - 10, (land ? 8 : 40) + (peckY - (land ? 8 : 40)) / 2, B, false); break;
    case 2: drawBird(peckX - 14, peckY - 6, B, true); break;
    case 3:  // peck! little marks round the beak
      drawBird(peckX, peckY, B, false);
      for (int k = -1; k <= 1; k++) epd.drawLine(sealX + 6, sealY + k * 8, sealX + 16, sealY + k * 12, GxEPD_BLACK);
      break;
    case 4: drawBird(peckX - 8, peckY - 30, B, true); break;
    case 5: drawBird(W - 32 * B - 4, 2, B, false); break;
  }
  if (f >= 3) printCentered(H - (land ? 14 : 40), f >= 4 ? "You've got mail!" : "tap tap!", F_BOLD12);
}

// Pressed with nothing new: a happy envelope, and the saved ones are a turn away.
void drawNoMsgScreen(const View &v) {
  int W = epd.width(), H = epd.height();
  bool land = W > H;
  int S = land ? 5 : 7;
  int ew = 26 * S, eh = 18 * S;
  int ox = (W - ew) / 2, oy = land ? 40 : 110;
  drawEnvelope(ox, oy, S, true);
  int ty = oy + eh + (land ? 34 : 56);
  printCentered(ty, "No new messages", F_BOLD18);
  String hint = v.savedCount > 0 ? "turn the knob to see your " + String(v.savedCount) + " saved" : String("all caught up!");
  printCentered(ty + (land ? 26 : 34), hint, F_REG12);
}

// One saved message as a card: the sender big, the date, an open envelope.
void drawCarouselScreen(const View &v) {
  int W = epd.width(), H = epd.height();
  bool land = W > H;
  printCentered(land ? 30 : 56, fit(v.from, F_BOLD18, W - 2 * MARGIN - 40), F_BOLD18);
  printCentered(land ? 52 : 86, v.when, F_REG12);
  int S = land ? 4 : 6;
  int ew = 26 * S, eh = 18 * S;
  int ox = (W - ew) / 2, oy = land ? 64 + 9 * S : 110 + 9 * S;
  drawOpenEnvelope(ox, oy, S, 5);
  if (v.sentReaction >= 0) {  // the reaction you sent, on the corner of the card
    int ix = ox + ew - 18, iy = oy + eh - 30;
    epd.fillCircle(ix + 14, iy + 14, 22, GxEPD_WHITE);
    epd.drawCircle(ix + 14, iy + 14, 22, GxEPD_BLACK);
    for (int r = 0; r < REACTION_PX; r += 2)  // half size
      for (int c = 0; c < REACTION_PX; c += 2)
        if (pgm_read_byte(REACTIONS[v.sentReaction].bitmap + r * ((REACTION_PX + 7) / 8) + c / 8) & (0x80 >> (c % 8)))
          epd.drawPixel(ix + c / 2, iy + r / 2, GxEPD_BLACK);
  }
  String sub = v.title;
  if (sub.length()) printCentered(oy + eh + (land ? 16 : 30), fit(sub, F_REG9, W - 2 * MARGIN), F_REG9);
  // arrows when there are more cards either side
  int ay = oy + eh / 2;
  if (v.pos > 1) epd.fillTriangle(MARGIN, ay, MARGIN + 14, ay - 14, MARGIN + 14, ay + 14, GxEPD_BLACK);
  if (v.pos < v.total) epd.fillTriangle(W - MARGIN, ay, W - MARGIN - 14, ay - 14, W - MARGIN - 14, ay + 14, GxEPD_BLACK);
  // position dots (up to 12), then the hint
  int n = min(v.total, 12), dotsW = n * 12;
  int dx = (W - dotsW) / 2 + 6, dy = H - FOOTER_H - (land ? 7 : 12);
  for (int i = 0; i < n; i++) {
    bool on = (v.total <= 12) ? (i == v.pos - 1) : (i == (v.pos - 1) * 12 / v.total);
    if (on) epd.fillCircle(dx + i * 12, dy, 4, GxEPD_BLACK);
    else epd.drawCircle(dx + i * 12, dy, 3, GxEPD_BLACK);
  }
  drawFooter(String(v.pos) + " of " + String(v.total), "press to open", false);
}

// Quick reply: six reactions to choose from.
void drawReactScreen(const View &v) {
  int W = epd.width(), H = epd.height();
  bool land = W > H;
  View hv = v;
  hv.savedList = false;
  drawHeader(hv);
  epd.fillRect(0, 0, W, HEADER_H, GxEPD_BLACK);
  printAt(MARGIN, 21, fit("Reply to " + v.from, F_BOLD9, W - 2 * MARGIN), F_BOLD9, GxEPD_WHITE);
  int cols = land ? 3 : 2, rows = land ? 2 : 3;
  int top = HEADER_H + 4, bottom = H - FOOTER_H - 4;
  int cw = (W - 2 * MARGIN) / cols, ch = (bottom - top) / rows;
  for (int i = 0; i < REACTION_COUNT; i++) {
    int cx = MARGIN + (i % cols) * cw, cy = top + (i / cols) * ch;
    bool sel = i == v.reaction;
    uint16_t fg = sel ? GxEPD_WHITE : GxEPD_BLACK;
    if (sel) epd.fillRoundRect(cx + 3, cy + 2, cw - 6, ch - 4, 10, GxEPD_BLACK);
    int iy = cy + (ch - REACTION_PX - 18) / 2;
    drawReactionIcon(cx + (cw - REACTION_PX) / 2, iy, i, 1, fg);
    String label = REACTIONS[i].label;
    if (i == v.sentReaction) label += " (sent)";
    printCentered(iy + REACTION_PX + 16, fit(label, F_BOLD9, cw - 8), F_BOLD9, fg, cx + cw / 2);
  }
  drawFooter("turn back: message", v.reaction >= 0 ? "press to send" : "turn to choose", false);
}

void drawSentScreen(const View &v) {
  int W = epd.width(), H = epd.height();
  bool land = W > H;
  int sc = 2, size = REACTION_PX * sc;
  int y = land ? 24 : 90;
  drawReactionIcon((W - size) / 2, y, v.reaction, sc, GxEPD_BLACK);
  printCentered(y + size + (land ? 30 : 44), fit("Sent to " + v.from + "!", F_BOLD18, W - 2 * MARGIN), F_BOLD18);
  if (v.replyQueued) printCentered(y + size + (land ? 54 : 74), "(it goes when the Wi-Fi is back)", F_REG9);
}

void drawMenuScreen(const View &v) {
  int W = epd.width(), H = epd.height();
  epd.fillRect(0, 0, W, HEADER_H + 4, GxEPD_BLACK);
  printCentered(24, "Settings", F_BOLD12, GxEPD_WHITE);
  const int top = HEADER_H + 8, rowH = 28, n = (int)v.menu.size();
  const int fits = max(1, (H - FOOTER_H - top - 4) / rowH);
  int first = constrain(v.menuSel - fits / 2, 0, max(0, n - fits));  // scrolls to keep the selection in view
  if (first > 0) epd.fillTriangle(W / 2 - 6, top - 1, W / 2 + 6, top - 1, W / 2, top - 7, GxEPD_WHITE);
  if (first + fits < n) epd.fillTriangle(W / 2 - 6, H - FOOTER_H - 6, W / 2 + 6, H - FOOTER_H - 6, W / 2, H - FOOTER_H, GxEPD_BLACK);
  for (int i = first; i < n && i < first + fits; i++) {
    int y = top + (i - first) * rowH;
    bool sel = i == v.menuSel;
    bool editing = sel && v.menuEditing;
    uint16_t fg = sel && !editing ? GxEPD_WHITE : GxEPD_BLACK;
    if (sel && !editing) epd.fillRoundRect(MARGIN - 4, y, W - 2 * MARGIN + 8, rowH - 2, 6, GxEPD_BLACK);
    if (editing) epd.drawRoundRect(MARGIN - 4, y, W - 2 * MARGIN + 8, rowH - 2, 6, GxEPD_BLACK);
    int base = y + rowH / 2 + 6;
    printAt(MARGIN + 4, base, v.menu[i].label, F_BOLD9, fg);
    const String &val = v.menu[i].value;
    if (val.length()) {
      String shown = editing ? "< " + val + " >" : val;
      int vw = textWidth(shown, F_BOLD9);
      if (editing) epd.fillRoundRect(W - MARGIN - vw - 10, y + 2, vw + 12, rowH - 6, 5, GxEPD_BLACK);
      printAt(W - MARGIN - vw - 4, base, shown, F_BOLD9, editing ? GxEPD_WHITE : fg);
    }
  }
  drawFooter(v.menuEditing ? "turn to change" : "turn to move", v.menuEditing ? "press: done" : "press: choose", false);
}

void drawSleepScreen() {
  int W = epd.width(), H = epd.height();
  bool land = W > H;
  int S = land ? 4 : 6;
  int ew = 26 * S, eh = 18 * S;
  int ox = (W - ew) / 2, oy = land ? 40 : 120;
  drawEnvelope(ox, oy, S, false);
  printAt(ox + ew + 6, oy + 30, "z", F_BOLD12);
  printAt(ox + ew + 22, oy + 14, "z", F_BOLD12);
  printAt(ox + ew + 40, oy + 2, "Z", F_BOLD18);
  printCentered(oy + eh + (land ? 40 : 60), "Sleeping", F_BOLD18);
  printCentered(oy + eh + (land ? 64 : 92), "turn the knob to wake me", F_REG12);
}

// Draws a View into the picture in RAM (no refresh).
void drawView(const View &v, uint8_t rotation = cfg.rotation) {
  epd.setRotation(rotation);
  epd.fillScreen(GxEPD_WHITE);
  switch (v.scr) {
    case SCR_BLANK: break;
    case SCR_ENVELOPE: drawEnvelopeScreen(v, false); break;
    case SCR_HAPPY: drawEnvelopeScreen(v, true); break;
    case SCR_READ: drawReadScreen(v); break;
    case SCR_PICTURE: drawPictureScreen(v); break;
    case SCR_DECIDE: drawDecideScreen(v); break;
    case SCR_SETUP: drawSetupScreen(v); break;
    case SCR_TEXT: {
      std::vector<Line> lines;
      wrapInto(lines, v.text, F_BOLD12, epd.width() - 2 * MARGIN);
      int y = 8;
      for (const Line &l : lines) { y += lineHeight(F_BOLD12); printAt(MARGIN, y - 6, l.text, F_BOLD12); }
      break;
    }
    case SCR_PATTERN: drawPattern(); break;
    case SCR_ARRIVE: drawArriveScreen(v); break;
    case SCR_NOMSG: drawNoMsgScreen(v); break;
    case SCR_CAROUSEL: drawCarouselScreen(v); break;
    case SCR_REACT: drawReactScreen(v); break;
    case SCR_SENT: drawSentScreen(v); break;
    case SCR_MENU: drawMenuScreen(v); break;
    case SCR_SLEEP: drawSleepScreen(); break;
  }
}

// ---------------------------------------------------------------- the drawing task
#ifndef HOST_PREVIEW

SemaphoreHandle_t viewLock;
TaskHandle_t screenTask;
View pending;
uint8_t pendingRotation = 0;
bool pendingDirty = false;
bool pendingForceFull = false;
volatile uint32_t lastRefreshMs = 0;
volatile uint32_t refreshCount = 0;
volatile uint32_t fullRefreshCount = 0;
volatile uint32_t partialRefreshCount = 0;
volatile bool lastRefreshFull = false;

DisplayPage pageKind(Screen screen) {
  if (screen == SCR_BLANK) return DisplayPage::Blank;
  if (screen == SCR_SETUP || screen == SCR_SLEEP) return DisplayPage::Setup;  // a clean full refresh
  if (screen == SCR_PATTERN) return DisplayPage::Pattern;
  return DisplayPage::Content;
}

void screenShow(const View &v, bool forceFull = false) {
  if (!screenTask || !viewLock) return;
  xSemaphoreTake(viewLock, portMAX_DELAY);
  pending = v;
  pendingRotation = cfg.rotation;
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
  uint8_t rotation = 0;
  bool recovering = false;
  int recoverTries = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      bool requestedFull = recovering;
      xSemaphoreTake(viewLock, portMAX_DELAY);
      if (pendingDirty) {
        v = pending;
        rotation = pendingRotation;
        requestedFull = requestedFull || pendingForceFull;
        pendingDirty = false;
      } else if (!recovering) {
        xSemaphoreGive(viewLock);
        break;
      }
      xSemaphoreGive(viewLock);

      const DisplayPage page = pageKind(v.scr);
      const RefreshMode mode = policy.plan(page, requestedFull, rotation);
      if (mode == RefreshMode::Skip) continue;
      const bool sameHeader = v.scr == lastScreen &&
          (v.scr == SCR_READ || v.scr == SCR_DECIDE) &&
          v.from == lastFrom && v.pos == lastPos && v.total == lastTotal &&
          v.savedList == lastSavedList;
      const uint32_t t0 = millis();
      drawView(v, rotation);
      if (mode == RefreshMode::Full) {
        epd.display(false);
      } else if (sameHeader) {
        // Start at y=24 to include the bottom of the 30px header. All
        // dimensions remain byte-aligned in both portrait and landscape.
        epd.displayWindow(0, 24, epd.width(), epd.height() - 24);
      } else {
        epd.display(true);  // Fast waveform even when the whole view changes.
      }
      if (epd.epd2.busyTimedOut()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!recovering) {
          logf("Screen BUSY timed out. Resetting the panel and retrying a full refresh.");
          recovering = true;
        } else if (++recoverTries > 3) {
          // Never wait forever: give up on this frame and let the next view try again.
          logf("Screen still not responding after 3 resets; will retry on the next update.");
          recovering = false;
          recoverTries = 0;
          epd.init(0, true, 50, false);
          break;
        } else {
          logf("Screen reset %d of 3", recoverTries);
          for (int i = 0; i < 10 && epd.epd2.isBusy(); ++i) vTaskDelay(pdMS_TO_TICKS(1000));
        }
        epd.init(0, true, 50, false);
        continue;
      }
      if (recovering) logf("Screen refresh recovered");
      recovering = false;
      recoverTries = 0;
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
      if (lastRefreshFull) fullRefreshCount = fullRefreshCount + 1;
      else partialRefreshCount = partialRefreshCount + 1;
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
