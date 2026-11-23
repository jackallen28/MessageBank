#include "MailImage.h"
#include "src/tjpgd/tjpgd.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

#ifdef ARDUINO
#include <Arduino.h>
#include <miniz.h>
#else
#include <zlib.h>
#endif

namespace {

constexpr size_t maxEncodedBytes = 256 * 1024;
constexpr uint32_t maxJpegSide = 4096;
constexpr uint32_t maxPngSide = 2048;

struct Input {
  MailImageRead read;
  void *context;
  size_t length;

  bool bytes(size_t offset, uint8_t *dst, size_t count) const {
    return offset <= length && count <= length - offset && read(context, offset, dst, count);
  }
};

uint32_t big32(const uint8_t *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint16_t big16(const uint8_t *p) {
  return uint16_t(p[0]) << 8 | p[1];
}

uint8_t luminance(unsigned red, unsigned green, unsigned blue) {
  return (77 * red + 150 * green + 29 * blue + 128) >> 8;
}

uint8_t overWhite(uint8_t gray, uint8_t alpha) {
  return (gray * alpha + 255 * (255 - alpha) + 127) / 255;
}

// Transform the selected samples after resizing so reflected/rotated JPEG EXIF
// orientation does not require a second raster. PNG uses orientation 1.
class Raster {
 public:
  explicit Raster(MailImage &out) : image(out) {}

  void prepare(unsigned width, unsigned height, uint8_t exif = 1) {
    orientation = exif;
    const bool transpose = orientation >= 5;
    const unsigned limitWidth = transpose ? MAIL_IMAGE_MAX_HEIGHT : MAIL_IMAGE_MAX_WIDTH;
    const unsigned limitHeight = transpose ? MAIL_IMAGE_MAX_WIDTH : MAIL_IMAGE_MAX_HEIGHT;
    outWidth = width;
    outHeight = height;
    if (width > limitWidth || height > limitHeight) {
      if (width * limitHeight > height * limitWidth) {
        outWidth = limitWidth;
        outHeight = std::max(1u, height * limitWidth / width);
      } else {
        outHeight = limitHeight;
        outWidth = std::max(1u, width * limitHeight / height);
      }
    }
    image.width = transpose ? outHeight : outWidth;
    image.height = transpose ? outWidth : outHeight;
    image.pixels.assign(image.stride() * image.height, 0);
  }

  static unsigned sample(unsigned index, unsigned source, unsigned target) {
    return ((2 * index + 1) * source) / (2 * target);
  }

  static int selected(unsigned coord, unsigned source, unsigned target) {
    const unsigned index = ((2 * coord + 1) * target) / (2 * source);
    return sample(index, source, target) == coord ? int(index) : -1;
  }

  void pixel(unsigned x, unsigned y, uint8_t gray) {
    const unsigned right = outWidth - 1 - x, bottom = outHeight - 1 - y;
    unsigned u = x, v = y;
    switch (orientation) {
      case 2: u = right; break;
      case 3: u = right; v = bottom; break;
      case 4: v = bottom; break;
      case 5: u = y; v = x; break;
      case 6: u = bottom; v = x; break;
      case 7: u = bottom; v = right; break;
      case 8: u = y; v = right; break;
    }
    // Ordered dithering captures shades without a second image buffer.
    static constexpr uint8_t bayer[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}
    };
    if (gray < 8 + 16 * bayer[v & 3][u & 3]) {
      image.pixels[v * image.stride() + u / 8] |= 0x80u >> (u & 7);
    }
  }

  unsigned outWidth = 0, outHeight = 0;

 private:
  MailImage &image;
  uint8_t orientation = 1;
};

uint8_t exifOrientation(const Input &input, size_t offset, size_t length) {
  uint8_t header[14];
  if (length < sizeof header || !input.bytes(offset, header, sizeof header) ||
      std::memcmp(header, "Exif\0\0", 6) != 0) return 0;
  const bool little = header[6] == 'I' && header[7] == 'I';
  if (!little && !(header[6] == 'M' && header[7] == 'M')) return 0;
  auto tiffWord = [little](const uint8_t *p) { return little ? uint16_t(p[1]) << 8 | p[0] : big16(p); };
  auto number = [little](const uint8_t *p) {
    return little ? uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0] : big32(p);
  };
  if (tiffWord(header + 8) != 42) return 0;
  const uint32_t directory = number(header + 10);
  const size_t tiffLength = length - 6;
  uint8_t count[2];
  if (directory > tiffLength || tiffLength - directory < sizeof count ||
      !input.bytes(offset + 6 + directory, count, sizeof count)) return 0;
  const unsigned entries = tiffWord(count);
  size_t position = directory + sizeof count;
  if (entries > (tiffLength - position) / 12) return 0;
  for (unsigned i = 0; i < entries; i++, position += 12) {
    uint8_t entry[12];
    if (!input.bytes(offset + 6 + position, entry, sizeof entry)) return 0;
    if (tiffWord(entry) == 0x0112 && tiffWord(entry + 2) == 3 && number(entry + 4) == 1) {
      const unsigned value = tiffWord(entry + 8);
      return value >= 1 && value <= 8 ? value : 0;
    }
  }
  return 0;
}

bool jpegHeader(const Input &input, uint8_t &orientation) {
  // TJpgDec decodes MCUs without checking for the final EOI.
  uint8_t end[2];
  if (!input.bytes(input.length - 2, end, 2) || big16(end) != 0xffd9) return false;
  bool frame = false;
  uint8_t components = 0;
  for (size_t position = 2; position < input.length;) {
    uint8_t header[4];
    if (!input.bytes(position, header, 4) || header[0] != 0xff) return false;
    size_t length = big16(header + 2);
    if (length < 2 || length > input.length - position - 2) return false;
    const size_t payload = position + 4;
    length -= 2;
    switch (header[1]) {
      case 0xc0: {
        uint8_t info[6];
        if (frame || length < sizeof info || !input.bytes(payload, info, sizeof info)) return false;
        const unsigned height = big16(info + 1), width = big16(info + 3);
        components = info[5];
        if (info[0] != 8 || !width || width > maxJpegSide || !height || height > maxJpegSide ||
            (components != 1 && components != 3) || length != 6u + 3u * components) return false;
        frame = true;
        break;
      }
      case 0xc1: case 0xc2: case 0xc3: case 0xc5: case 0xc6: case 0xc7:
      case 0xc9: case 0xca: case 0xcb: case 0xcd: case 0xce: case 0xcf:
        return false;  // Progressive, arithmetic and non-baseline JPEG.
      case 0xe1: {
        const uint8_t found = exifOrientation(input, payload, length);
        if (found) orientation = found;
        break;
      }
      case 0xdd:
        if (length != 2) return false;
        break;
      case 0xda:
        return frame && length == 4u + 2u * components;
    }
    position = payload + length;
  }
  return false;
}

struct JpegDecode {
  const Input &input;
  Raster &raster;
  size_t offset = 0;
};

size_t readJpeg(JDEC *decoder, uint8_t *dst, size_t count) {
  auto &state = *static_cast<JpegDecode *>(decoder->device);
  count = std::min(count, state.input.length - state.offset);
  if (dst && count && !state.input.bytes(state.offset, dst, count)) return 0;
  state.offset += count;
  return count;
}

int drawJpeg(JDEC *decoder, void *data, JRECT *block) {
  auto &raster = static_cast<JpegDecode *>(decoder->device)->raster;
  const auto *samples = static_cast<const uint8_t *>(data);
  const unsigned width = decoder->width >> decoder->scale, height = decoder->height >> decoder->scale;
  const unsigned blockWidth = block->right - block->left + 1;
  for (unsigned y = block->top; y <= block->bottom; y++) {
    const int outY = Raster::selected(y, height, raster.outHeight);
    if (outY < 0) continue;
    const auto *row = samples + (y - block->top) * blockWidth;
    for (unsigned x = block->left; x <= block->right; x++) {
      const int outX = Raster::selected(x, width, raster.outWidth);
      if (outX >= 0) raster.pixel(outX, outY, row[x - block->left]);
    }
  }
#ifdef ARDUINO
  // Avoid starving the single-core ESP32-C3 when decoding many MCUs.
  if (block->left == 0) delay(1);
#endif
  return 1;
}

bool decodeJpeg(const Input &input, Raster &raster) {
  uint8_t orientation = 1;
  if (!jpegHeader(input, orientation)) return false;
  JDEC decoder{};
  alignas(4) uint8_t work[TJPGD_WORKSPACE_SIZE];
  JpegDecode state{input, raster};
  if (mb_jd_prepare(&decoder, readJpeg, work, sizeof work, &state) != JDR_OK) return false;
  raster.prepare(decoder.width, decoder.height, orientation);
  uint8_t scale = 0;
  while (scale < 3 && (unsigned(decoder.width) >> (scale + 1)) >= raster.outWidth &&
         (unsigned(decoder.height) >> (scale + 1)) >= raster.outHeight) scale++;
  return mb_jd_decomp(&decoder, drawJpeg, scale) == JDR_OK;
}

class PngRows {
 public:
  explicit PngRows(Raster &target) : raster(target) {}

  bool header(const uint8_t *info) {
    width = big32(info);
    height = big32(info + 4);
    depth = info[8];
    colorType = info[9];
    if (!width || width > maxPngSide || !height || height > maxPngSide ||
        info[10] || info[11] || info[12]) return false;  // Standard compression, filters, no interlace.
    switch (colorType) {
      case 0: channels = 1; break;
      case 2: channels = 3; break;
      case 3: channels = 1; break;
      case 4: channels = 2; break;
      case 6: channels = 4; break;
      default: return false;
    }
    if (depth != 8 && !(depth == 16 && colorType != 3) &&
        !((colorType == 0 || colorType == 3) && (depth == 1 || depth == 2 || depth == 4))) return false;
    rowSize = (width * channels * depth + 7) / 8;
    pixelBytes = (channels * depth + 7) / 8;
    return true;
  }

  bool palette(const uint8_t *data, size_t count) {
    if (paletteSize || transparent || colorType == 0 || colorType == 4 ||
        !count || count > 768 || count % 3) return false;
    paletteSize = count / 3;
    if (colorType == 3 && paletteSize > (1u << depth)) return false;
    for (size_t i = 0; i < paletteSize; i++) {
      colors[i] = luminance(data[3 * i], data[3 * i + 1], data[3 * i + 2]);
    }
    return true;
  }

  bool transparency(const uint8_t *data, size_t count) {
    if (transparent) return false;
    if (colorType == 3 && paletteSize && count && count <= paletteSize) {
      for (size_t i = 0; i < count; i++) colors[i] = overWhite(colors[i], data[i]);
    } else if ((colorType == 0 || colorType == 2) && count == channels * 2) {
      for (unsigned c = 0; c < channels; c++) key[c] = big16(data + 2 * c);
    } else {
      return false;
    }
    transparent = true;
    return true;
  }

  bool begin() {
    if (colorType == 3 && !paletteSize) return false;
    buffers.reset(new (std::nothrow) uint8_t[rowSize * 2]());
    if (!buffers) return false;
    prev = buffers.get();
    row = prev + rowSize;
    raster.prepare(width, height);
    return true;
  }

  bool push(const uint8_t *data, size_t length) {
    while (length) {
      if (rows >= height) return false;
      if (!filled) {
        filter = *data++;
        length--;
        if (filter > 4) return false;
        filled = 1;
      }
      const size_t count = std::min(length, rowSize + 1 - filled);
      std::memcpy(row + filled - 1, data, count);
      data += count;
      length -= count;
      filled += count;
      if (filled == rowSize + 1) {
        if (!finishRow()) return false;
        std::swap(prev, row);
        rows++;
        filled = 0;
#ifdef ARDUINO
        if ((rows & 15) == 0) delay(1);
#endif
      }
    }
    return true;
  }

  bool complete() const { return rows == height; }

 private:
  static int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int da = abs(p - a), db = abs(p - b), dc = abs(p - c);
    return da <= db && da <= dc ? a : db <= dc ? b : c;
  }

  unsigned channel(unsigned x, unsigned c = 0) const {
    if (depth >= 8) {
      const auto *sample = row + x * pixelBytes + c * (depth / 8);
      return depth == 8 ? *sample : big16(sample);
    }
    const unsigned bit = x * depth;
    return (row[bit / 8] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1);
  }

  unsigned displayByte(unsigned value) const {
    if (depth == 16) return value >> 8;
    if (depth == 8) return value;
    return value * 255 / ((1u << depth) - 1);
  }

  bool finishRow() {
    if (filter) {
      for (size_t i = 0; i < rowSize; i++) {
        const uint8_t left = i < pixelBytes ? 0 : row[i - pixelBytes];
        const uint8_t above = prev[i], corner = i < pixelBytes ? 0 : prev[i - pixelBytes];
        switch (filter) {
          case 1: row[i] += left; break;
          case 2: row[i] += above; break;
          case 3: row[i] += (left + above) / 2; break;
          case 4: row[i] += paeth(left, above, corner); break;
        }
      }
    }
    const int y = Raster::selected(rows, height, raster.outHeight);
    if (y < 0) return true;
    for (unsigned x = 0; x < raster.outWidth; x++) {
      const unsigned sx = Raster::sample(x, width, raster.outWidth);
      const unsigned first = channel(sx);
      unsigned gray = 255;
      if (colorType == 3) {
        if (first >= paletteSize) return false;
        gray = colors[first];
      } else if (colorType == 0 || colorType == 4) {
        gray = transparent && first == key[0] ? 255 : displayByte(first);
        if (colorType == 4) gray = overWhite(gray, displayByte(channel(sx, 1)));
      } else {
        const unsigned green = channel(sx, 1), blue = channel(sx, 2);
        if (!transparent || first != key[0] || green != key[1] || blue != key[2]) {
          gray = luminance(displayByte(first), displayByte(green), displayByte(blue));
        }
        if (colorType == 6) gray = overWhite(gray, displayByte(channel(sx, 3)));
      }
      raster.pixel(x, y, gray);
    }
    return true;
  }

  Raster &raster;
  unsigned width = 0, height = 0, depth = 0, colorType = 0, channels = 0, pixelBytes = 0;
  size_t rowSize = 0, filled = 0, paletteSize = 0;
  unsigned rows = 0, filter = 0;
  bool transparent = false;
  std::array<uint8_t, 256> colors{};
  unsigned key[3]{};
  std::unique_ptr<uint8_t[]> buffers;
  uint8_t *prev = nullptr, *row = nullptr;
};

class InflatePng {
 public:
  explicit InflatePng(PngRows &rows) : output(rows) {}

#ifdef ARDUINO
  bool begin() {
    state.reset(new (std::nothrow) tinfl_decompressor);
    window.reset(new (std::nothrow) uint8_t[TINFL_LZ_DICT_SIZE]);
    if (!state || !window) return false;
    tinfl_init(state.get());
    return true;
  }

  bool add(const uint8_t *data, size_t length) {
    if (complete) return length == 0;
    for (;;) {
      size_t used = length, written = TINFL_LZ_DICT_SIZE - position;
      const tinfl_status status = tinfl_decompress(state.get(), data, &used, window.get(),
          window.get() + position, &written, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT);
      if (status < 0 || !output.push(window.get() + position, written)) return false;
      position = (position + written) % TINFL_LZ_DICT_SIZE;
      data += used;
      length -= used;
      if (status == TINFL_STATUS_DONE) {
        complete = true;
        return length == 0;
      }
      if (status == TINFL_STATUS_NEEDS_MORE_INPUT) return length == 0;
      if (!used && !written) return false;
    }
  }

 private:
  std::unique_ptr<tinfl_decompressor> state;
  std::unique_ptr<uint8_t[]> window;
  size_t position = 0;
#else
  ~InflatePng() {
    if (initialized) inflateEnd(&state);
  }

  bool begin() {
    initialized = inflateInit(&state) == Z_OK;
    return initialized;
  }

  bool add(const uint8_t *data, size_t length) {
    if (complete || !length) return length == 0;
    state.next_in = const_cast<uint8_t *>(data);
    state.avail_in = length;
    do {
      state.next_out = buffer;
      state.avail_out = sizeof buffer;
      const int status = inflate(&state, Z_NO_FLUSH);
      if ((status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) ||
          !output.push(buffer, sizeof buffer - state.avail_out)) return false;
      if (status == Z_STREAM_END) {
        complete = true;
        return state.avail_in == 0;
      }
      if (status == Z_BUF_ERROR) return state.avail_in == 0;
    } while (state.avail_in || !state.avail_out);
    return true;
  }

 private:
  z_stream state{};
  bool initialized = false;
  uint8_t buffer[1024];
#endif

 public:
  bool done() const { return complete && output.complete(); }

 private:
  PngRows &output;
  bool complete = false;
};

uint32_t pngCrc(uint32_t crc, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; bit++) crc = crc >> 1 ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return crc;
}

constexpr uint32_t tag(char a, char b, char c, char d) {
  return uint32_t(a) << 24 | uint32_t(b) << 16 | uint32_t(c) << 8 | uint32_t(d);
}

bool decodePng(const Input &input, Raster &raster) {
  PngRows rows(raster);
  InflatePng inflater(rows);
  bool haveData = false, dataClosed = false;
  for (size_t position = 8; position < input.length;) {
    uint8_t header[8], checksum[4], buffer[1024];
    if (!input.bytes(position, header, sizeof header)) return false;
    const uint32_t length = big32(header), type = big32(header + 4);
    constexpr uint32_t IHDR = tag('I', 'H', 'D', 'R'), PLTE = tag('P', 'L', 'T', 'E');
    constexpr uint32_t IDAT = tag('I', 'D', 'A', 'T'), IEND = tag('I', 'E', 'N', 'D'), TRNS = tag('t', 'R', 'N', 'S');
    if ((position == 8) != (type == IHDR)) return false;
    position += sizeof header;
    if (length > input.length - position || input.length - position - length < sizeof checksum) return false;
    if (haveData && type != IDAT) dataClosed = true;
    switch (type) {
      case IHDR: case PLTE: case TRNS: case IEND:
        if (length > sizeof buffer) return false;
        break;
      case IDAT:
        if (dataClosed) return false;
        if (!haveData && (!rows.begin() || !inflater.begin())) return false;
        haveData = true;
        break;
      default:
        if (!(header[4] & 0x20)) return false;  // Unknown critical chunk.
    }
    uint32_t crc = pngCrc(0xffffffffu, header + 4, 4);
    for (size_t consumed = 0; consumed < length;) {
      const size_t count = std::min(sizeof buffer, size_t(length) - consumed);
      if (!input.bytes(position + consumed, buffer, count)) return false;
      crc = pngCrc(crc, buffer, count);
      if (type == IDAT && !inflater.add(buffer, count)) return false;
      consumed += count;
    }
    position += length;
    if (!input.bytes(position, checksum, sizeof checksum) || (crc ^ 0xffffffffu) != big32(checksum)) return false;
    position += sizeof checksum;
    switch (type) {
      case IHDR: if (length != 13 || !rows.header(buffer)) return false; break;
      case PLTE: if (haveData || !rows.palette(buffer, length)) return false; break;
      case TRNS: if (haveData || !rows.transparency(buffer, length)) return false; break;
      case IEND: return length == 0 && haveData && position == input.length && inflater.done();
    }
  }
  return false;
}

bool readMemory(void *context, size_t offset, uint8_t *dst, size_t length) {
  std::memcpy(dst, static_cast<const uint8_t *>(context) + offset, length);
  return true;
}

}  // namespace

bool decodeMailImage(MailImageRead read, void *context, size_t length, MailImage &out) {
  if (!read || length < 8 || length > maxEncodedBytes) return false;
  const Input input{read, context, length};
  uint8_t signature[8];
  if (!input.bytes(0, signature, sizeof signature)) return false;
  MailImage decoded;
  Raster raster(decoded);
  try {
    static constexpr uint8_t pngSignature[] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    bool success = false;
    if (std::memcmp(signature, pngSignature, sizeof pngSignature) == 0) success = decodePng(input, raster);
    else if (big16(signature) == 0xffd8) success = decodeJpeg(input, raster);
    if (!success || !decoded.valid()) return false;
  } catch (const std::bad_alloc &) {
    return false;
  }
  out = std::move(decoded);
  return true;
}

bool decodeMailImage(const uint8_t *data, size_t length, MailImage &out) {
  return data && decodeMailImage(readMemory, const_cast<uint8_t *>(data), length, out);
}
