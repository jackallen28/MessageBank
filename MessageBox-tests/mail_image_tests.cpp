// From the repository root:
//   cc -std=c11 -c MessageBox/src/tjpgd/tjpgd.c -o /tmp/messagebox-tjpgd.o
//   g++ -std=c++17 -IMessageBox MessageBox-tests/mail_image_tests.cpp MessageBox/MailImage.cpp /tmp/messagebox-tjpgd.o -lz -o /tmp/messagebox-image-tests && /tmp/messagebox-image-tests
#include "MailImage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <zlib.h>

using Bytes = std::vector<uint8_t>;

static void putWord(Bytes &bytes, uint16_t value) {
  bytes.push_back(value >> 8);
  bytes.push_back(value & 255);
}

static void putLong(Bytes &bytes, uint32_t value) {
  putWord(bytes, value >> 16);
  putWord(bytes, value & 65535);
}

static void chunk(Bytes &png, const char type[5], const Bytes &data) {
  putLong(png, data.size());
  const size_t start = png.size();
  png.insert(png.end(), type, type + 4);
  png.insert(png.end(), data.begin(), data.end());
  putLong(png, crc32(0, png.data() + start, png.size() - start));
}

static Bytes makePng(uint32_t width, uint32_t height, uint8_t pixelType, const Bytes &rows) {
  Bytes png = {137, 'P', 'N', 'G', 13, 10, 26, 10};
  Bytes header;
  putLong(header, width);
  putLong(header, height);
  header.insert(header.end(), {8, pixelType, 0, 0, 0});
  chunk(png, "IHDR", header);
  unsigned long size = compressBound(rows.size());
  Bytes deflate(size);
  assert(compress(deflate.data(), &size, rows.data(), rows.size()) == Z_OK);
  deflate.resize(size);
  // Exercise resuming the decompressor for arbitrary boundaries.
  for (uint8_t byte : deflate) chunk(png, "IDAT", {byte});
  chunk(png, "IEND", {});
  return png;
}

static void jpegSegment(Bytes &jpeg, uint8_t marker, const Bytes &data) {
  jpeg.insert(jpeg.end(), {255, marker});
  putWord(jpeg, data.size() + 2);
  jpeg.insert(jpeg.end(), data.begin(), data.end());
}

static Bytes makeJpeg(uint8_t frame = 0xc0) {
  Bytes jpeg{255, 0xd8};
  Bytes quantizers(65, 1);
  quantizers[0] = 0;
  jpegSegment(jpeg, 0xdb, quantizers);
  jpegSegment(jpeg, frame, {8, 0, 8, 0, 8, 1, 1, 0x11, 0});
  // A single-symbol DC table (zero delta) and a single-symbol AC table (end of block).
  Bytes tables(36, 0);
  tables[1] = tables[19] = 1;
  tables[18] = 0x10;
  jpegSegment(jpeg, 0xc4, tables);
  jpegSegment(jpeg, 0xda, {1, 1, 0, 0, 63, 0});
  jpeg.insert(jpeg.end(), {0x3f, 255, 0xd9});
  return jpeg;
}

int main() {
  MailImage raster;
  // Visible black, transparent black, visible black pixels followed by one padding bit.
  const Bytes transparent = makePng(3, 1, 6, {0, 0,0,0,255, 0,0,0,0, 0,0,0,255});
  assert(decodeMailImage(transparent.data(), transparent.size(), raster));
  assert(raster.width == 3 && raster.height == 1 && raster.pixels == Bytes{0xa0});

  // Same 3-pixel row repeated using each of the five PNG scanline filters.
  auto filters = makePng(3, 5, 0, {
      0, 0,255,0,  // None
      1, 0,255,1,  // Sub
      2, 0,0,0,    // Up
      3, 0,128,129, // Average
      4, 0,0,1     // Paeth
  });
  assert(decodeMailImage(filters.data(), filters.size(), raster));
  assert(raster.pixels == Bytes(5, 0xa0));
  filters[filters.size() - 1] ^= 0xff;
  assert(!decodeMailImage(filters.data(), filters.size(), raster));

  const Bytes large = makePng(512, 256, 0, Bytes(513 * 256, 0));
  assert(decodeMailImage(large.data(), large.size(), raster));
  assert(raster.width == 224 && raster.height == 112);
  assert(raster.pixels == Bytes(28 * 112, 0xff));

  const Bytes huge = makePng(0xffffffff, 1, 0, {0});
  assert(!decodeMailImage(huge.data(), huge.size(), raster));
  assert(raster.width == 224 && raster.height == 112);  // Failure keeps the caller's last image.

  auto jpeg = makeJpeg();
  assert(decodeMailImage(jpeg.data(), jpeg.size(), raster));
  assert(raster.width == 8 && raster.height == 8);
  assert((raster.pixels == Bytes{0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa}));
  jpeg.pop_back();
  assert(!decodeMailImage(jpeg.data(), jpeg.size(), raster));

  const Bytes progressive = makeJpeg(0xc2);
  assert(!decodeMailImage(progressive.data(), progressive.size(), raster));

  std::puts("mail image tests passed");
}
