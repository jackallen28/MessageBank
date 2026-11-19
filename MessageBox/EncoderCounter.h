#pragma once
#include <stdint.h>

// Polling keeps encoder interrupts out of flash and display operations.
class EncoderCounter {
 public:
  void begin(uint8_t pins) {
    previous_ = pins & 3;
    edges_ = 0;
  }

  int8_t update(uint8_t pins) {
    static const int8_t transitions[16] = {
        0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    pins &= 3;
    if ((pins ^ previous_) == 3) {
      // Both contacts changed at once: discard the incomplete step.
      previous_ = pins;
      edges_ = 0;
      return 0;
    }
    edges_ += transitions[(previous_ << 2) | pins];
    previous_ = pins;
    if (edges_ >= 4) {
      edges_ -= 4;
      return 1;
    }
    if (edges_ <= -4) {
      edges_ += 4;
      return -1;
    }
    return 0;
  }

 private:
  uint8_t previous_ = 0;
  int8_t edges_ = 0;
};
