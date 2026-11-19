# MessageBox firmware v10

v10 is Codex's v9 with a review pass and four improvements. The source is in
[`../MessageBox/`](../MessageBox/).

| File | Use |
|---|---|
| `MessageBox-main-v10-USB-installer.bin` | Full 4 MB image, flashed over USB at **0x0**. Installs the bootloader, partition table and app. **Erases settings and messages.** |
| `MessageBox-main-v10-OTA.bin` | App only, for the web page's **Firmware update** (from a GitHub release link). **Keeps settings and messages.** |
| `SHA256SUMS.txt` | Checksums of both |

Built with Arduino CLI 1.6, Arduino-ESP32 **3.3.7**, GxEPD2 1.6.9, Adafruit GFX 1.13.0, BusIO 1.17.4
and NeoPixel 1.15.5. It uses the same partition table as v9 (two 1.77 MB app slots, LittleFS at
`0x370000`), so web updates keep working between v9 and v10.

## Flash it

**From v9 (keeps your settings and messages):** upload `MessageBox-main-v10-OTA.bin` to a public
GitHub release, then paste its download link into the device page's **Firmware update**. After
it restarts, **leave it running for 30 seconds** (see below).

**Fresh, over USB (erases settings):**

1. Open https://espressif.github.io/esptool-js/ in Chrome or Edge and press **Connect**.
   If no port appears, hold BOOT while plugging in.
2. Use the flash settings **DIO, 40 MHz, 4 MB**.
3. Set the address to **0x0**, choose `MessageBox-main-v10-USB-installer.bin` and press **Program**.
4. Press RESET.

If the flasher can't get into download mode with the screen connected, unplug the screen's RES
wire (GPIO8) while flashing, as before.

## Wiring

Unchanged from v9. These are the tested connections:

| Part | Signal | ESP32-C3 |
|---|---|---|
| Screen | VCC, GND | 3V3, GND |
| Screen | SCL, SDA, CS, D/C | GPIO10, GPIO6, GPIO7, GPIO5 |
| Screen | **RES, BUSY** | **GPIO8, GPIO4** (see "Running without BUSY and RESET") |
| Encoder | CLK, DT, SW, +, GND | GPIO0, GPIO1, GPIO21, 3V3, GND |
| LED ring | DIN, 5V, GND | GPIO2 through 330 Ω (1 kΩ is fine too), 5V, GND |
| Vibration module | IN, VCC, GND | GPIO20, 5V, GND |

Leave **GPIO9** (BOOT) and **GPIO3** unconnected. The zip tie in the case goes over those two pads.

## What's new in v10

1. **Optional BUSY and RESET wires.** The setup page has **Screen BUSY and RESET are wired**,
   ticked by default, the tested setup. Untick it to run with neither wire. See below.
2. **Screen SPI speed setting.** The default stays at the tested **1 MHz**. 4 MHz and 10 MHz
   are options:
   - At 1 MHz, sending each picture takes about 0.2 s on top of the panel's own refresh time.
   - At 4 MHz it takes about 0.05 s, and at 10 MHz about 0.02 s.
   - If the picture is ever garbled at a faster speed, go back to 1 MHz.
3. **The knob is read every 1 ms in its own task.** v9 read it from the main loop, which
   pauses while saving messages to flash, serving the web page or queueing a screen picture.
   Fast turns could lose clicks then. It's still Codex's polled decoder with no interrupt, so
   flash writes can't crash it.
4. **Bad updates roll back by themselves.** The core used to mark a freshly updated firmware
   "good" the moment it started. If an update crashed during startup, only a USB reflash would
   fix it. Now a new firmware is only kept after it has **run for 30 s with its web page up**.
   If it crashes or restarts before that, the bootloader goes back to the previous firmware.
   So after a web update, don't unplug it for half a minute.

The status line now shows `MessageBox-main-v10`.

## Running without BUSY and RESET

**What I think went wrong in your Codex test.** Without BUSY, the screen driver can't tell when
the panel has finished, so it waits fixed times instead. GxEPD2's built-in waits for this panel
are 50 ms for power-on and 350 ms for a fast refresh. The panel actually takes about 46 ms and
343 ms, which leaves only 2 % spare. A command sent while the panel is still busy is ignored or
garbles the picture.

Without RESET, the controller only starts cleanly if its RES pin is held **high**. Left floating,
it can sit in reset. (Codex's note said "do not tie RES to 3V3", but holding RES high is how this
driver is designed to run with no reset wire.)

**What v10 does.** With the box unticked, it uses its own copy of the driver
([`MB_EPD370.h`](../MessageBox/MB_EPD370.h)) with generous waits: 200 ms for power-on and 600 ms
for a fast refresh. A fast update then takes about 0.6–0.8 s instead of about 0.4–0.6 s. With
the box ticked, BUSY is read as before and those waits don't apply.

**To try it:**

1. Untick **Screen BUSY and RESET are wired** on the setup page. The box restarts.
2. Power off. Move the screen's **RES wire from GPIO8 to 3V3**, and **disconnect BUSY**.
3. Power on and run the checklist below.
4. If the screen misbehaves, tick the box again and reconnect the wires. The web page works
   either way.

Once it works without them, GPIO4 and GPIO8 are free too.

## Test checklist

- [ ] **First boot:** join `MessageBox-Setup` and open 192.168.4.1. The status shows
      `MessageBox-main-v10`.
- [ ] **Hardware tests:**
  - [ ] *Test each LED* (12 × red, green, blue)
  - [ ] the ticks and the new-message buzz
  - [ ] the screen test pattern (full refresh)
  - [ ] the envelope and keep-or-delete screens (fast refresh)
- [ ] **Knob:** turn it quickly. The position on the page should move one step per click, with
      no skips. One tick per click.
- [ ] **Send to the box:** the envelope, then press, scroll, press, choose, press, then "All
      read!", then blank. Count the full (flashing) refreshes on the status line. There should
      be one, for the blank at the end.
- [ ] **Home Wi-Fi and Gmail:** send an email to the box's address and check it arrives within a
      minute.
- [ ] **Screen SPI speed:** try 4 MHz, then 10 MHz, and watch the "last refresh … ms" line. Keep
      the fastest speed that never garbles.
- [ ] **No BUSY/RESET (optional):** follow the steps above, and repeat the screen tests.
- [ ] **Web update:** install the same OTA file again from a GitHub release link. After 30 s the
      log shows "Firmware update confirmed".

## Review of v9

What Codex got right, and v10 keeps:

- **Startup order:** Wi-Fi starts before the web server and captive DNS open their sockets. That
  was the cause of the v6 startup stop.
- **Updater:** it only accepts HTTPS GitHub links, including redirects, and checks the
  certificate. It refuses the merged USB image, writes to the inactive app slot, and only
  switches over after the image checks out. Gmail checks wait while an update runs. A token stops
  other web pages from starting an update.
- **Pins:** RESET moved to GPIO8, freeing GPIO3. GPIO9 (BOOT) is kept clear. The onboard LED
  writes were removed, since the screen owns GPIO8.
- **Refresh policy:** fast refreshes for everything routine, and a full refresh only to clear
  to blank, for setup, rotation changes or the test pattern. Repeated blank frames are skipped.
  Every session ends with a full refresh to blank, which also clears any ghosting, so the
  removed refresh counter isn't missed. The screen isn't put to sleep between messages, so the
  next envelope is a fast refresh. The driver still switches the panel's high voltage off after
  every update.
- **Knob decoder:** polled instead of interrupt-driven, which removes a real crash risk during
  flash writes. A step where both contacts change at once is thrown away.
- **Robustness:** the screen and its task fail safely if memory runs out, so Wi-Fi setup always
  comes up.

What I changed or flagged:

| v9 | Issue | v10 |
|---|---|---|
| Knob polled in `loop()` | Missed clicks while the loop is busy | Its own 1 ms task |
| Updates marked good at boot | A crash-on-start update needs a USB reflash | Confirmed after 30 s; rolls back otherwise |
| BUSY and RESET required | GxEPD2's fixed waits have no margin; RES must not float | Optional, with a wider-margin driver copy |
| 1 MHz SPI fixed | About 0.2 s added to every refresh | A setting: 1, 4 or 10 MHz |
| "Do not tie RES to 3V3" (`config.h`) | That's the right way to run without a reset wire | Comment corrected |
| `checks/` mentioned but not in the notes | — | Host tests in [`../MessageBox-tests/`](../MessageBox-tests/) for the URL checks, image header check, refresh policy and knob decoder |

Not changed, but worth knowing:

- **Quiet hours:** the Gmail check runs every 60 s around the clock. That's fine, but a quiet
  period at night would be easy to add later.
- **Unplugging after a web update:** if you unplug within 30 s, it rolls back. Plug it back in
  and update again.
