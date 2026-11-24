#include <cmath>
// A small stand-in for Adafruit GFX that draws into memory, for previewing screens on a PC.
#pragma once
#include "Arduino.h"
#include <vector>
typedef struct { uint16_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; } GFXglyph;
typedef struct { uint8_t *bitmap; GFXglyph *glyph; uint16_t first, last; uint8_t yAdvance; } GFXfont;
#define GxEPD_BLACK 0x0000
#define GxEPD_WHITE 0xFFFF

class FakeGFX {
 public:
  int W = 240, H = 416, rot = 0;
  std::vector<uint8_t> px;  // 1 = black
  const GFXfont *font = nullptr;
  int cx = 0, cy = 0;
  uint16_t color = GxEPD_BLACK;
  void setRotation(int r) { rot = r & 3; W = (rot & 1) ? 416 : 240; H = (rot & 1) ? 240 : 416; px.assign(W * H, 0); }
  int width() const { return W; }
  int height() const { return H; }
  void drawPixel(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < W && y < H) px[y * W + x] = c == GxEPD_BLACK; }
  void fillRect(int x, int y, int w, int h, uint16_t c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) drawPixel(i, j, c); }
  void fillScreen(uint16_t c) { fillRect(0, 0, W, H, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) { fillRect(x, y, w, 1, c); fillRect(x, y + h - 1, w, 1, c); fillRect(x, y, 1, h, c); fillRect(x + w - 1, y, 1, h, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int i = 0; i <= n; i++) drawPixel(x0 + (x1 - x0) * i / std::max(1, n), y0 + (y1 - y0) * i / std::max(1, n), c);
  }
  void drawCircle(int x0, int y0, int r, uint16_t c) { for (int a = 0; a < 720; a++) { double t = a * 3.14159265 / 360; drawPixel(x0 + (int)lround(r * cos(t)), y0 + (int)lround(r * sin(t)), c); } }
  void fillCircle(int x0, int y0, int r, uint16_t c) { for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x * x + y * y <= r * r) drawPixel(x0 + x, y0 + y, c); }
  bool inRound(int x, int y, int w, int h, int r, int i, int j) {
    int dx = i < x + r ? x + r - i : (i >= x + w - r ? i - (x + w - r - 1) : 0);
    int dy = j < y + r ? y + r - j : (j >= y + h - r ? j - (y + h - r - 1) : 0);
    return dx * dx + dy * dy <= r * r;
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) if (inRound(x, y, w, h, r, i, j)) drawPixel(i, j, c); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++)
      if (inRound(x, y, w, h, r, i, j) && !inRound(x + 1, y + 1, w - 2, h - 2, std::max(0, r - 1), i, j)) drawPixel(i, j, c);
  }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2}), miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
      int a = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0), b = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1), d = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
      if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) drawPixel(x, y, c);
    }
  }
  void setFont(const GFXfont *f) { font = f; }
  void setTextColor(uint16_t c) { color = c; }
  void setCursor(int x, int y) { cx = x; cy = y; }
  void print(const String &s) {
    for (size_t k = 0; k < s.length(); k++) {
      uint8_t ch = s[k];
      if (ch < font->first || ch > font->last) continue;
      const GFXglyph &g = font->glyph[ch - font->first];
      const uint8_t *bm = font->bitmap + g.bitmapOffset;
      int bit = 0;
      for (int yy = 0; yy < g.height; yy++)
        for (int xx = 0; xx < g.width; xx++, bit++)
          if (bm[bit >> 3] & (0x80 >> (bit & 7))) drawPixel(cx + g.xOffset + xx, cy + g.yOffset + yy, color);
      cx += g.xAdvance;
    }
  }
};
