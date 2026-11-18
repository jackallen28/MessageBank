#pragma once
#include "Arduino.h"
// Settings are not stored on a PC: every getter returns its default.
struct Preferences {
  bool begin(const char *, bool) { return true; }
  void end() {}
  String getString(const char *, String d = "") { return d; }
  uint32_t getUInt(const char *, uint32_t d = 0) { return d; }
  uint16_t getUShort(const char *, uint16_t d = 0) { return d; }
  uint8_t getUChar(const char *, uint8_t d = 0) { return d; }
  bool getBool(const char *, bool d = false) { return d; }
  void putString(const char *, const String &) {}
  void putUInt(const char *, uint32_t) {}
  void putUShort(const char *, uint16_t) {}
  void putUChar(const char *, uint8_t) {}
  void putBool(const char *, bool) {}
};
