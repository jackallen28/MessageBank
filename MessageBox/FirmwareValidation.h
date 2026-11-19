#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

inline bool githubFirmwareUrl(const char *url, bool initial = true) {
  if (!url || strlen(url) > 2048 || strncmp(url, "https://", 8) != 0) return false;
  for (const char *p = url; *p; ++p)
    if ((unsigned char)*p <= 32 || *p == '\\' || *p == '#' || (unsigned char)*p == 127) return false;
  const char *host = url + 8;
  const char *path = strchr(host, '/');
  if (!path) return false;
  const size_t hostLength = path - host;
  const auto isHost = [host, hostLength](const char *name) {
    return strlen(name) == hostLength && strncmp(host, name, hostLength) == 0;
  };
  const bool github = isHost("github.com");
  const bool raw = isHost("raw.githubusercontent.com");
  if (initial) {
    if (!github && !raw) return false;
    const char *end = strchr(path, '?');
    if (!end) end = path + strlen(path);
    if (end - path < 4 || strncmp(end - 4, ".bin", 4) != 0) return false;
    if (github && !strstr(path, "/releases/download/") &&
        !strstr(path, "/releases/latest/download/")) return false;
    return true;
  }
  return github || raw || isHost("release-assets.githubusercontent.com") ||
         isHost("objects.githubusercontent.com") || isHost("github-releases.githubusercontent.com");
}

// Application images contain esp_app_desc at byte 32. A merged USB installer
// starts with a bootloader instead, and must never be written to an OTA slot.
inline bool esp32c3ApplicationHeader(const uint8_t *data, size_t length) {
  if (!data || length < 288 || data[0] != 0xE9 || data[1] == 0 || data[1] > 16) return false;
  const uint16_t chip = uint16_t(data[12]) | (uint16_t(data[13]) << 8);
  const uint32_t appMagic = uint32_t(data[32]) | (uint32_t(data[33]) << 8) |
      (uint32_t(data[34]) << 16) | (uint32_t(data[35]) << 24);
  return chip == 5 && appMagic == 0xABCD5432;
}
