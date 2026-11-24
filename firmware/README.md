# MessageBox firmware v15

**v15** adds **automatic updates**. Once a day (and 10 minutes after it starts) the box reads
[`latest.txt`](latest.txt) in this repo. If it names a newer version, the box installs it the
next time it has been idle for a minute (blank, or showing the waiting envelope), shows
"Updating to v.." with a progress bar, and restarts. A bad update rolls back by itself, as
before. Turn it off, or check straight away, from the box's settings (hold the knob 20 s:
*Automatic updates*, *Check for updates*) or the web page's *Firmware update* section.

**Publishing a new version:** add the new `MessageBox-main-vNN-OTA.bin` here, then change
`latest.txt` to `version=NN` and `file=MessageBox-main-vNN-OTA.bin`. The number in
`FIRMWARE_VERSION` (config.h) must match, or boxes keep reinstalling or never update.


**v14** added the new knob flow, quick replies and the settings screen (v13's screen fixes
included):

- **A hummingbird** flies in and pecks the envelope open when a message arrives. The screen then
  stays on the envelope with the count, and the ring spins for 1 minute every 10 minutes until
  it's read (*Lights* settings).
- **Reading:** press when done (the message is kept), hold 2 s to delete. Turning on past the end
  opens **quick replies**: six reactions (the Jack-in-a-Box hand-drawn symbols), sent as an
  email reply in the same thread.
- **Press with nothing new:** "No new messages", a happy envelope and a light show; turn for the
  **saved messages** as cards (open envelope, sender or nickname, dd/mm/yy).
- **Hold 20 s:** settings on the box: lights, buzz, bedtime (quiet time), change Wi-Fi, shut down
  (turn the knob to wake). This replaces the old 5 s hold for the setup Wi-Fi.
- **Web page:** nicknames by email address, bedtime, lights on/off and the spin schedule. *Send
  to the box* takes a reply address so you can try a quick reply to yourself.

The message store moves to a new format that keeps each mail's reply details. v14 reads
messages saved by older firmware, but older firmware can't read v14's, so going back to v13
would empty the box.

v12 fixes an OTA download bug that could report a valid update as a merged USB image when the
start of the file arrived in separate TLS records. It includes v11's Wi-Fi and display fixes
and emailed pictures. The source is in [`../MessageBox/`](../MessageBox/).

| File | Use |
|---|---|
| `MessageBox-main-v15-OTA.bin` | App image for the web page's **Firmware update**. **Keeps settings and messages.** |
| `MessageBox-main-v15-USB-recovery.bin` | USB recovery flashed at **0xe000** on the standard MessageBox OTA layout. **Keeps settings and messages.** |
| `MessageBox-main-v15-USB-installer.bin` | Full 4 MB image, flashed over USB at **0x0**. Installs the bootloader, partition table and app. **Erases settings and messages.** |
| `SHA256SUMS.txt` | Checksums of the firmware images |

Built with Arduino CLI 1.6, Arduino-ESP32 **3.3.7**, GxEPD2 1.6.9, Adafruit GFX 1.13.0, BusIO 1.17.4
and NeoPixel 1.15.5. It uses the same partition table as v9 (two 1.77 MB app slots, LittleFS at
`0x370000`).

## Flash it

**Web update (keeps your settings and messages).** Paste the raw URL below into the
device page's **Firmware update** and select **Install and restart**. After it restarts,
leave it running for at least 30 seconds.

```text
https://raw.githubusercontent.com/jackallen28/MessageBank/main/firmware/MessageBox-main-v15-OTA.bin
```

### USB recovery for an existing MessageBox

Use this if the web updater on v9, v10, or v11 reports a merged USB image when given the OTA
link. The recovery file is for the standard MessageBox partition table installed by the
project's USB installer. It updates the OTA selector and first app slot. It keeps the saved
settings, saved messages, and pictures.

1. Open the [Espressif browser flasher](https://espressif.github.io/esptool-js/) in Chrome or Edge and press **Connect**.
2. Use one file row and set its flash address to **`0xe000`**. Leave flash mode, frequency, and size on **keep**.
3. Select `MessageBox-main-v15-USB-recovery.bin` and press **Program** with **Erase all** unchecked.
4. Press RESET and check that the device status shows `MessageBox-main-v15`.

### Fresh USB install (erases settings and messages)

1. Open https://espressif.github.io/esptool-js/ in Chrome or Edge and press **Connect**.
   If no port appears, hold BOOT while plugging in.
2. Use the flash settings **DIO, 40 MHz, 4 MB**.
3. Set the address to **0x0**, choose `MessageBox-main-v15-USB-installer.bin` and press **Program**.
4. Press RESET.

If the flasher can't get into download mode with the screen connected, unplug the screen's RES
wire (GPIO8) while flashing, as before.

## Emailed pictures

Send a message with a JPEG or PNG attachment to the box's Gmail address. Open the message
and turn clockwise past its text to see the picture. Turn anticlockwise to go back to the text.
An image-only email opens on the picture. A saved message keeps its picture after a restart.

The box displays one picture per message and scales it to fit a 224 × 176 pixel monochrome area.
Attachments can be up to 64 KiB, with dimensions up to 4096 × 4096 for baseline JPEG and
2048 × 2048 for noninterlaced PNG. Attach a resized copy if a phone photo exceeds these limits.
The box keeps the caption or message text when it cannot show the attachment.

JPEG decoding uses ChaN's TJpgDec. [Source and license](../MessageBox/src/tjpgd/LICENSE.txt)

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
      `MessageBox-main-v15`.
- [ ] **Hardware tests:**
  - [ ] *Test each LED* (12 × red, green, blue)
  - [ ] the ticks and the new-message buzz
  - [ ] the screen test pattern (full refresh)
  - [ ] *Hummingbird*, *Envelope*, *Quick replies* and *No new messages* (fast refresh)
- [ ] **Knob:** turn it quickly. The position on the page should move one step per click, with
      no skips. One tick per click.
- [ ] **Send to the box** (put your own email in *Reply address*): the hummingbird, the
      envelope with "1"; press, scroll, keep turning to the reactions, pick one and press. "Sent
      to ..." shows, and the reply arrives in your inbox as "Re: Thinking of you" (a pretend
      message has no email thread to join, so it starts a new one).
- [ ] **Delete:** open a message and hold the knob 2 s (the ring fills red).
- [ ] **No new messages:** press while blank: the happy envelope and the light show. Turn: the
      saved cards; press to open one, press again to come back.
- [ ] **Settings:** hold the knob 20 s. Toggle Lights and Buzz, set Bedtime, try Shut down and
      wake it by turning the knob.
- [ ] **Nicknames:** add one on the web page for a sender; their cards and messages use it.
- [ ] **A real thread:** from your own email, send the box a message, react to it on the box, and
      check the reply sits under your message in the same conversation.
- [ ] **Home Wi-Fi and Gmail:** send an email to the box's address and check it arrives within a
      minute.
- [ ] **Wi-Fi recovery:** turn the router off and back on. The box rejoins the saved network and
      closes its setup access point after its clients disconnect.
- [ ] **Picture:** email a JPEG and a PNG, with a caption on one of them. Check that each picture
      is upright, turn back to the caption, then save a message and reopen it after a restart.
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
