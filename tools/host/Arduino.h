// Just enough of Arduino to draw MessageBox's screens on a PC (see ../preview_screens.cpp).
#pragma once
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <algorithm>
using std::min;
using std::max;
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
class String {
 public:
  std::string s;
  String() {}
  String(const char *c) : s(c ? c : "") {}
  String(const std::string &x) : s(x) {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  size_t length() const { return s.size(); }
  char operator[](size_t i) const { return i < s.size() ? s[i] : 0; }
  int indexOf(char c, size_t f = 0) const { auto p = s.find(c, f); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String &c, size_t f = 0) const { auto p = s.find(c.s, f); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char *c, size_t f = 0) const { return indexOf(String(c), f); }
  int lastIndexOf(char c) const { auto p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(size_t a) const { return a >= s.size() ? String() : String(s.substr(a)); }
  String substring(size_t a, size_t b) const { if (b > s.size()) b = s.size(); if (a > b) std::swap(a, b); return String(s.substr(a, b - a)); }
  void trim() { size_t a = 0; while (a < s.size() && isspace((unsigned char)s[a])) a++; size_t b = s.size(); while (b > a && isspace((unsigned char)s[b - 1])) b--; s = s.substr(a, b - a); }
  void toUpperCase() { for (auto &c : s) c = toupper(c); }
  void toLowerCase() { for (auto &c : s) c = tolower(c); }
  bool startsWith(const String &p) const { return s.compare(0, p.s.size(), p.s) == 0; }
  bool endsWith(const String &p) const { return s.size() >= p.s.size() && s.compare(s.size() - p.s.size(), p.s.size(), p.s) == 0; }
  void replace(const String &a, const String &b) { size_t p = 0; while ((p = s.find(a.s, p)) != std::string::npos) { s.replace(p, a.s.size(), b.s); p += b.s.size(); } }
  void remove(size_t i) { s.erase(i); }
  void concat(const char *c, size_t n) { s.append(c, n); }
  long toInt() const { return atol(s.c_str()); }
  const char *c_str() const { return s.c_str(); }
  String &operator+=(const String &o) { s += o.s; return *this; }
  String &operator+=(const char *o) { s += o; return *this; }
  String &operator+=(char c) { s += c; return *this; }
  bool operator==(const String &o) const { return s == o.s; }
  bool operator==(const char *o) const { return s == o; }
  bool operator!=(const String &o) const { return s != o.s; }
};
inline String operator+(const String &a, const String &b) { return String(a.s + b.s); }
inline String operator+(const String &a, const char *b) { return String(a.s + b); }
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }
inline String operator+(const String &a, char b) { return String(a.s + b); }
inline String operator+(const String &a, int b) { return a + String(b); }
inline uint32_t millis() { return 0; }
inline void delay(int) {}
template <class T, class L, class H> T constrain(T v, L lo, H hi) { return v < lo ? lo : (v > hi ? hi : v); }
#ifndef PI
#define PI 3.14159265358979
#endif
struct FakeSerial { void print(const char *s) { fputs(s, stderr); } void println(const char *s) { fprintf(stderr, "%s\n", s); } };
static FakeSerial Serial;
typedef void *SemaphoreHandle_t;
inline int xSemaphoreTake(SemaphoreHandle_t, int) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
#define portMAX_DELAY 0
