// LED ring animations. Non-blocking: call ledsLoop() often.
#pragma once
#include <Adafruit_NeoPixel.h>
#include "config.h"

Adafruit_NeoPixel ring(NUM_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800);

enum RingMode : uint8_t {
  RING_OFF,
  RING_SOLID,      // one colour (instructions, setup = blue)
  RING_NOTIFY,     // a new message: spinning in the chosen style
  RING_RAINBOW,    // rainbow spin (happy, "!rainbow")
  RING_DIM,        // soft glow of colour 1 (reading)
  RING_POWERDOWN,  // LEDs go out one by one, anticlockwise
  RING_CHASE_TEST, // each LED red, green, blue in turn
};

struct RingLook {
  uint8_t style = STYLE_COMET;
  uint32_t c1 = 0xFF2060, c2 = 0x6020FF;
  uint16_t speed = 1200;
  uint8_t tail = 5;
};

RingMode ringMode = RING_OFF;
uint32_t ringColor = 0;
RingLook ringLook;
uint32_t ringStart = 0;
// overlays
float holdFill = -1;          // 0..1 while the knob is held down (setup in 5 s), <0 = none
int8_t choiceGlow = 0;        // -1 delete (ring pulses red), +1 keep/save (blue), 0 none
uint32_t overrideUntil = 0;   // a web-page test owns the ring until then
RingMode savedMode;
uint32_t savedColor;

void ringSet(RingMode m, uint32_t color = 0) {
  if (overrideUntil) {  // a test is running: change what comes back after it
    savedMode = m;
    savedColor = color;
    return;
  }
  if (m == ringMode && color == ringColor && m != RING_POWERDOWN) return;
  ringMode = m;
  ringColor = color;
  ringStart = millis();
}

void ringLookFromSettings() {
  ringLook.style = cfg.ledStyle;
  ringLook.c1 = cfg.color1;
  ringLook.c2 = cfg.color2;
  ringLook.speed = max<uint16_t>(200, cfg.ledSpeed);
  ringLook.tail = constrain(cfg.ledTail, 1, NUM_LEDS);
}

// Shows a mode for a while (web tests), then goes back to what the device was doing.
void ringTest(RingMode m, uint32_t color, uint32_t ms) {
  if (!overrideUntil) {
    savedMode = ringMode;
    savedColor = ringColor;
  }
  overrideUntil = 0;
  ringSet(m, color);
  overrideUntil = millis() + ms;
}

static uint32_t mix(uint32_t c, float k) {
  if (k <= 0) return 0;
  if (k > 1) k = 1;
  uint8_t r = ((c >> 16) & 0xFF) * k, g = ((c >> 8) & 0xFF) * k, b = (c & 0xFF) * k;
  return ring.Color(r, g, b);
}

static uint32_t blend(uint32_t a, uint32_t b, float t) {
  uint8_t r = ((a >> 16) & 0xFF) * (1 - t) + ((b >> 16) & 0xFF) * t;
  uint8_t g = ((a >> 8) & 0xFF) * (1 - t) + ((b >> 8) & 0xFF) * t;
  uint8_t bl = (a & 0xFF) * (1 - t) + (b & 0xFF) * t;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | bl;
}

// How bright LED i is for a comet whose head is at position head (in LEDs, fractional).
static float cometLevel(int i, float head, float tail) {
  float d = head - i;
  while (d < 0) d += NUM_LEDS;
  while (d >= NUM_LEDS) d -= NUM_LEDS;
  if (d > tail) return 0;
  float k = 1.0f - d / tail;
  return k * k;
}

static void drawNotify(uint32_t t) {
  const RingLook &L = ringLook;
  float turns = (float)t / L.speed;
  float head = turns * NUM_LEDS;
  float tail = L.tail + 0.5f;
  switch (L.style) {
    case STYLE_COMET:  // one comet, colour 1 at the head fading to colour 2 in the tail
      for (int i = 0; i < NUM_LEDS; i++) {
        float k = cometLevel(i, head, tail);
        ring.setPixelColor(i, mix(blend(L.c2, L.c1, k), k));
      }
      break;
    case STYLE_TWIN:  // two comets opposite each other, colour 1 and colour 2
      for (int i = 0; i < NUM_LEDS; i++) {
        float a = cometLevel(i, head, tail), b = cometLevel(i, head + NUM_LEDS / 2, tail);
        uint32_t ca = mix(L.c1, a), cb = mix(L.c2, b);
        ring.setPixelColor(i, ca > cb ? ca : cb);
      }
      break;
    case STYLE_RAINBOW:  // a comet that changes colour as it goes round
      for (int i = 0; i < NUM_LEDS; i++) {
        float k = cometLevel(i, head, tail);
        uint32_t c = ring.ColorHSV((uint16_t)(turns * 65536 / 3 + i * 65536 / NUM_LEDS));
        ring.setPixelColor(i, k > 0 ? mix(c, k) : 0);
      }
      break;
    case STYLE_CHASE: {  // every third LED, marching round, colours 1 and 2 taking turns
      int step = (int)(turns * NUM_LEDS);
      for (int i = 0; i < NUM_LEDS; i++)
        ring.setPixelColor(i, ((i + step) % 3 == 0) ? mix((step / NUM_LEDS) % 2 ? L.c2 : L.c1, 1) : 0);
      break;
    }
    case STYLE_FLASH: {  // comet in colour 1; at the end of each lap the whole ring flashes colour 2
      float lap = turns - floorf(turns);
      for (int i = 0; i < NUM_LEDS; i++) ring.setPixelColor(i, mix(L.c1, cometLevel(i, head, tail)));
      if (lap > 0.92f) ring.fill(mix(L.c2, 1));
      break;
    }
  }
}

void ledsBegin() {
  ring.begin();
  ring.clear();
  ring.show();
  ringLookFromSettings();
}

void ledsLoop() {
  static uint32_t last = 0;
  static bool wasDark = false;
  if (millis() - last < 16) return;  // ~60 frames a second
  last = millis();
  if (overrideUntil && (int32_t)(millis() - overrideUntil) > 0) {
    overrideUntil = 0;
    ringLookFromSettings();  // a "Try" may have changed the look
    ringMode = savedMode;
    ringColor = savedColor;
    ringStart = millis();
  }
  uint32_t t = millis() - ringStart;
  ring.setBrightness(cfg.brightness);
  ring.clear();
  switch (ringMode) {
    case RING_OFF: break;
    case RING_SOLID: ring.fill(mix(ringColor, 1)); break;
    case RING_NOTIFY: drawNotify(t); break;
    case RING_RAINBOW:
      for (int i = 0; i < NUM_LEDS; i++)
        ring.setPixelColor(i, ring.gamma32(ring.ColorHSV((uint16_t)(t * 25 + i * 65536 / NUM_LEDS))));
      break;
    case RING_DIM: ring.fill(mix(ringColor, 0.15f)); break;
    case RING_POWERDOWN: {  // all on, then out one by one (anticlockwise), over 0.9 s
      int lit = NUM_LEDS - (int)(t / 75);
      for (int i = 0; i < lit; i++) ring.setPixelColor(i, mix(ringColor, 0.6f));
      if (lit <= 0) ringMode = RING_OFF;
      break;
    }
    case RING_CHASE_TEST: {
      int step = t / 250;
      static const uint32_t c[3] = { 0xFF0000, 0x00FF00, 0x0000FF };
      if (step < NUM_LEDS * 3) ring.setPixelColor(step % NUM_LEDS, mix(c[step / NUM_LEDS], 1));
      break;
    }
  }
  // the decision: pulsing red for delete, blue for keep (save)
  if (choiceGlow) {
    ring.clear();
    uint32_t c = choiceGlow < 0 ? 0xFF1010 : 0x1040FF;
    float k = 0.55f + 0.45f * sinf(millis() / 160.0f);
    for (int i = 0; i < NUM_LEDS; i++) ring.setPixelColor(i, mix(c, k));
  }
  // holding the knob: the ring fills up, then the setup Wi-Fi opens
  if (holdFill >= 0) {
    ring.clear();
    int n = holdFill * NUM_LEDS + 0.5f;
    for (int i = 0; i < n && i < NUM_LEDS; i++) ring.setPixelColor(i, mix(0xFFFFFF, 0.6f));
  }
  // don't keep sending frames once the ring is dark
  bool dark = true;
  for (int i = 0; i < NUM_LEDS && dark; i++) if (ring.getPixelColor(i)) dark = false;
  if (dark && wasDark) return;
  wasDark = dark;
  ring.show();
}
