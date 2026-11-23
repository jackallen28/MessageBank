#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

constexpr uint16_t MAIL_IMAGE_MAX_WIDTH = 224;
constexpr uint16_t MAIL_IMAGE_MAX_HEIGHT = 176;

struct MailImage {
  uint16_t width = 0;
  uint16_t height = 0;
  std::vector<uint8_t> pixels;  // Row-padded, MSB first, 1 is black.

  size_t stride() const { return (width + 7u) / 8; }
  bool valid() const {
    return width && width <= MAIL_IMAGE_MAX_WIDTH && height && height <= MAIL_IMAGE_MAX_HEIGHT &&
           pixels.size() == stride() * height;
  }
};

using MailImageRead = bool (*)(void *context, size_t offset, uint8_t *data, size_t length);

bool decodeMailImage(MailImageRead read, void *context, size_t length, MailImage &out);
bool decodeMailImage(const uint8_t *data, size_t length, MailImage &out);
