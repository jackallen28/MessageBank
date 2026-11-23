// Host tests for MessageBox's pure logic (no Arduino needed):
//   g++ -std=c++17 -I../MessageBox host_tests.cpp -o host_tests && ./host_tests
#include <cassert>
#include <cstdio>
#include <cstring>
#include "FirmwareValidation.h"
#include "RefreshPolicy.h"
#include "EncoderCounter.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
  // ---- update links: only GitHub release assets / raw .bin over HTTPS
  CHECK(githubFirmwareUrl("https://github.com/me/box/releases/download/v10/MessageBox-main-v10-OTA.bin"));
  CHECK(githubFirmwareUrl("https://github.com/me/box/releases/latest/download/fw.bin"));
  CHECK(githubFirmwareUrl("https://raw.githubusercontent.com/me/box/main/fw.bin"));
  CHECK(!githubFirmwareUrl("http://github.com/me/box/releases/download/v10/fw.bin"));      // not HTTPS
  CHECK(!githubFirmwareUrl("https://github.com/me/box/blob/main/fw.bin"));                  // page, not asset
  CHECK(!githubFirmwareUrl("https://github.com.evil.example/releases/download/x/fw.bin"));  // look-alike host
  CHECK(!githubFirmwareUrl("https://evil.example/github.com/releases/download/x/fw.bin"));
  CHECK(!githubFirmwareUrl("https://github.com/me/box/releases/download/v10/fw.zip"));
  CHECK(!githubFirmwareUrl("https://github.com/me/box/releases/download/v10/f w.bin"));     // space
  CHECK(githubFirmwareUrl("https://objects.githubusercontent.com/x/y?sig=1", false));       // redirect target
  CHECK(!githubFirmwareUrl("https://evil.example/x.bin", false));
  // ---- app header: an OTA app image passes, the merged USB image (bootloader first) fails
  {
    uint8_t app[288] = {0}; app[0] = 0xE9; app[1] = 6; app[12] = 5; app[13] = 0;
    app[32] = 0x32; app[33] = 0x54; app[34] = 0xCD; app[35] = 0xAB;
    CHECK(esp32c3ApplicationHeader(app, sizeof(app)));
    uint8_t esp32[288]; memcpy(esp32, app, 288); esp32[12] = 0;      // wrong chip
    CHECK(!esp32c3ApplicationHeader(esp32, sizeof(esp32)));
    uint8_t boot[288]; memcpy(boot, app, 288); boot[32] = 0;          // no app descriptor
    CHECK(!esp32c3ApplicationHeader(boot, sizeof(boot)));
    CHECK(!esp32c3ApplicationHeader(app, 100));
  }
  // ---- refresh policy: first frame full; reading partial; blank full once, then skipped
  {
    RefreshPolicy p;
    CHECK(p.plan(DisplayPage::Content, false, 1) == RefreshMode::Full); p.commit(DisplayPage::Content, 1);
    CHECK(p.plan(DisplayPage::Content, false, 1) == RefreshMode::Partial); p.commit(DisplayPage::Content, 1);
    CHECK(p.plan(DisplayPage::Blank, false, 1) == RefreshMode::Full); p.commit(DisplayPage::Blank, 1);
    CHECK(p.plan(DisplayPage::Blank, false, 1) == RefreshMode::Skip);
    CHECK(p.plan(DisplayPage::Content, false, 1) == RefreshMode::Partial);    // envelope after blank: no flash
    CHECK(p.plan(DisplayPage::Setup, false, 1) == RefreshMode::Full); p.commit(DisplayPage::Setup, 1);
    CHECK(p.plan(DisplayPage::Content, false, 3) == RefreshMode::Full);       // rotation changed
    CHECK(p.plan(DisplayPage::Pattern, false, 1) == RefreshMode::Full);
    p.commit(DisplayPage::Pattern, 1);
    CHECK(p.plan(DisplayPage::Content, false, 1) == RefreshMode::Full);     // clear the test pattern
  }
  // ---- knob: a full quadrature cycle is one click; bounce doesn't count; reverse works
  {
    EncoderCounter e; e.begin(3);
    const uint8_t cw[] = {1, 0, 2, 3};      // 11 -> 01 -> 00 -> 10 -> 11
    int clicks = 0;
    for (int n = 0; n < 5; n++) for (uint8_t s : cw) clicks += e.update(s);
    CHECK(clicks == 5);
    const uint8_t ccw[] = {2, 0, 1, 3};
    for (int n = 0; n < 3; n++) for (uint8_t s : ccw) clicks += e.update(s);
    CHECK(clicks == 2);
    for (int n = 0; n < 20; n++) { clicks += e.update(1); clicks += e.update(3); }   // contact bounce
    CHECK(clicks == 2);
  }
  printf(fails ? "%d test(s) FAILED\n" : "all host tests passed\n", fails);
  return fails != 0;
}
