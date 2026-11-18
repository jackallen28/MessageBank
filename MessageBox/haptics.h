// Vibration motor: knob ticks, the "tick tick tick tick tick TICK" notification and the
// shutdown buzz. Non-blocking: call hapticsLoop() often.
#pragma once
#include "config.h"

struct Pulse {
  uint8_t power;  // 0-255 PWM
  uint16_t on;    // ms
  uint16_t off;   // ms after it
};

const int MAX_PULSES = 32;
Pulse hq[MAX_PULSES];
int hqLen = 0, hqIdx = 0;
bool hqOn = false;
bool hqIsNotify = false;  // a notification is playing (a knob touch stops it)
uint32_t hqT = 0;

void motorWrite(uint8_t v) { analogWrite(PIN_VIBE, v); }

void hapticsBegin() {
  pinMode(PIN_VIBE, OUTPUT);
  analogWriteFrequency(PIN_VIBE, 20000);  // 20 kHz: above hearing, so the motor doesn't whine
  motorWrite(0);
}

void playPulses(const Pulse *p, int n, bool isNotify = false) {
  n = min(n, MAX_PULSES);
  memcpy(hq, p, n * sizeof(Pulse));
  hqLen = n;
  hqIdx = 0;
  hqOn = true;
  hqIsNotify = isNotify;
  hqT = millis();
  motorWrite(n ? hq[0].power : 0);
}

void hapticsStop() {
  hqLen = 0;
  hqIsNotify = false;
  motorWrite(0);
}

bool hapticsBusy() { return hqLen > 0; }

void hapticsLoop() {
  if (!hqLen) return;
  uint32_t t = millis() - hqT;
  if (hqOn) {
    if (t < hq[hqIdx].on) return;
    motorWrite(0);
    hqOn = false;
    hqT = millis();
    return;
  }
  if (t < hq[hqIdx].off) return;
  if (++hqIdx >= hqLen) {
    hqLen = 0;
    hqIsNotify = false;
    return;
  }
  hqOn = true;
  hqT = millis();
  motorWrite(hq[hqIdx].power);
}

// ---------------------------------------------------------------- the patterns

// One knob click. strong = a press. A tick cuts short anything else that is playing.
void hapticTick(bool strong = false) {
  if (!cfg.knobHaptics) return;
  Pulse p = { strong ? (uint8_t)min(255, cfg.tickPower + 40) : cfg.tickPower,
              (uint16_t)(strong ? cfg.tickMs * 2 + 10 : cfg.tickMs), 0 };
  playPulses(&p, 1);
}

// Knob turned past the end: two quick bumps.
void hapticBump() {
  if (!cfg.knobHaptics) return;
  Pulse p[2] = { { cfg.tickPower, cfg.tickMs, 60 }, { cfg.tickPower, cfg.tickMs, 0 } };
  playPulses(p, 2);
}

// "Can't do that": a short-long buzz.
void hapticNope() {
  if (!cfg.knobHaptics) return;
  Pulse p[2] = { { 200, 40, 80 }, { 200, 110, 0 } };
  playPulses(p, 2);
}

// A choice confirmed: saved = a soft double heartbeat, deleted = one firm thud.
void hapticConfirm(bool saved) {
  if (!cfg.knobHaptics) return;
  if (saved) {
    Pulse p[2] = { { 220, 60, 90 }, { 255, 90, 0 } };
    playPulses(p, 2);
  } else {
    Pulse p[1] = { { 255, 160, 0 } };
    playPulses(p, 1);
  }
}

// tick tick tick tick tick TICK: each tick longer and stronger than the one before.
// The values are passed in so the web page can try settings before saving them.
void hapticNotify(uint8_t ticks, uint16_t msFirst, uint16_t msLast, uint8_t powFirst, uint8_t powLast,
                  uint16_t gap, int repeats = 1) {
  Pulse p[MAX_PULSES];
  int n = 0;
  ticks = constrain(ticks, 1, 12);
  for (int r = 0; r < repeats; r++) {
    for (int i = 0; i < ticks && n < MAX_PULSES; i++) {
      float k = ticks > 1 ? (float)i / (ticks - 1) : 1;
      k = k * k;  // ease in: small steps first, so the last TICK stands out
      p[n].power = powFirst + (powLast - powFirst) * k;
      p[n].on = msFirst + (msLast - msFirst) * k;
      p[n].off = (i == ticks - 1) ? 900 : gap;  // a pause before a repeat
      n++;
    }
  }
  playPulses(p, n, true);
}

void hapticNotifyFromSettings(int repeats = 1) {
  if (!cfg.notifyVibe) return;
  hapticNotify(cfg.ntfTicks, cfg.ntfMsFirst, cfg.ntfMsLast, cfg.ntfPowFirst, cfg.ntfPowLast, cfg.ntfGap, repeats);
}

// Powering down: brrr rrr rr r, each pulse weaker and shorter.
void hapticShutdown() {
  if (!cfg.knobHaptics) return;
  Pulse p[5] = { { 255, 220, 70 }, { 200, 150, 70 }, { 150, 100, 70 }, { 110, 60, 70 }, { 80, 30, 0 } };
  playPulses(p, 5);
}
