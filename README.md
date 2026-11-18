# MessageBox

A small box for messages from a friend or partner. They send an email to a private Gmail
address. The box reads it, shows it on the e-paper, lights the LED ring and buzzes. Turn the
knob to look back through older messages, and press it to mark a message as read.

This is the **test firmware**: it gets every part working and sets up Wi-Fi and Gmail from your
phone.

## Parts

| Part | What it does |
|---|---|
| ESP32-C3 SuperMini | The brains: Wi-Fi, web page, Gmail |
| WeAct 3.7" e-paper, black/white, 240 × 416 (GDEY037T03) | Shows the message. Keeps the picture with no power. |
| WS2812B 12-LED ring | Glows (breathes) while a message is unread |
| EC11 / KY-040 rotary encoder with push button | Turn to browse, press to say "read", hold 5 s for setup |
| PWM coin vibration motor module (GND, VCC, IN) | Buzzes when a message arrives |
| 330 Ω resistor, 470 µF capacitor (6.3 V or more) | Protect the LED ring |

## Wiring

![Wiring diagram](wiring.svg)

| From | To | Note |
|---|---|---|
| E-paper **VCC** | C3 **3V3** | |
| E-paper **GND** | C3 **GND** | |
| E-paper **SCL** (clock) | **GPIO4** | |
| E-paper **SDA** (data) | **GPIO6** | |
| E-paper **CS** | **GPIO7** | |
| E-paper **D/C** | **GPIO5** | |
| E-paper **RES** | **GPIO10** | |
| E-paper **BUSY** | **GPIO3** | |
| Ring **5V** | C3 **5V** | 470 µF across the ring's 5V and GND (+ leg to 5V) |
| Ring **GND** | C3 **GND** | |
| Ring **IN** (DIN) | 330 Ω → **GPIO2** | Leave the ring's OUT unconnected |
| Encoder **+** (marked 5V) | C3 **3V3** | **Not 5V.** The module's pull-ups would put 5 V on the C3. |
| Encoder **GND** | C3 **GND** | |
| Encoder **CLK** (S1) | **GPIO0** | |
| Encoder **DT** (S2) | **GPIO1** | |
| Encoder **SW** (KEY) | **GPIO9** | The same pin as the C3's BOOT button |
| Vibration **VCC** | C3 **5V** | |
| Vibration **GND** | C3 **GND** | |
| Vibration **IN** | **GPIO20** | |

GPIO8 drives the C3's own blue LED, which lights while Gmail is being checked. GPIO21 is free.

Things to know:

- **Power:** everything runs from the C3's USB-C, so use a phone charger or computer port that
  gives 1 A. The ring's brightness is set to 60 of 255 by default. At full white, 12 LEDs draw
  about 0.7 A.
- **If the ring flickers or shows the wrong colours**, the C3's 3.3 V data signal may be too low
  for a ring running on 5 V. Feed the ring's 5V through a 1N4001 diode (stripe towards the ring).
  That gives the ring about 4.3 V, and 3.3 V then counts as "high".
- **GPIO2 and GPIO9 are "strapping" pins** that the C3 reads at power-up. They're fine for these
  jobs. If you hold the knob down while plugging in, the C3 starts in download mode. That's
  handy for flashing; just don't hold it by accident.
- The e-paper uses the C3's hardware SPI on GPIO4 and GPIO6.
- On the encoder, if one click moves two messages, set `ENC_STEPS_PER_DETENT` to 2 at the top
  of the sketch.

## Build and flash

1. Arduino IDE 2, then **Boards Manager**, then install **esp32 by Espressif**, version 3.x.
2. **Library Manager**, then install **GxEPD2**, **Adafruit GFX Library** and
   **Adafruit NeoPixel**.
3. Open `MessageBox/MessageBox.ino`. Under **Tools**, set:
   - Board: **ESP32C3 Dev Module**
   - **USB CDC On Boot: Enabled** (otherwise the Serial Monitor stays blank)
   - Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**, for room to grow
4. Upload. If no port shows, hold **BOOT** while plugging the C3 in, upload, then press **RST**.
5. Open the **Serial Monitor at 115200**. Everything the box does is logged there and on the web
   page.

The sketch compiles with esp32 3.0.7, GxEPD2 1.6, Adafruit GFX 1.13 and NeoPixel 1.15: it uses
1.2 MB of flash and 53 KB of RAM.

## Test it (setup Wi-Fi)

On first start no Wi-Fi is saved, so the box opens its own:

1. The e-paper shows the setup steps and the ring turns blue.
2. On your phone, join the Wi-Fi network **MessageBox-Setup** (password **messagebox**). The page
   usually pops up by itself. If it doesn't, open **http://192.168.4.1**.
3. Under **Test the hardware**, check each part:
   - [ ] **LED ring:** *Test each LED* lights each LED red, then green, then blue, in order. A
         missing colour means a bad LED or joint. *Breathe* is what an unread message looks like.
   - [ ] **Vibration:** *Short*, *Long*, *Heartbeat*. *Ramp* goes from soft to strong, which
         shows the PWM strength control works.
   - [ ] **E-paper:** *Test pattern* fills the screen with a frame, crossed lines and bars. All
         four edges should show. *Show text* draws your own text.
   - [ ] **Encoder:** turn the knob and watch **Position** and the dial move: one step per
         click, up for clockwise. If it counts the wrong way, swap the CLK and DT wires. Press
         it and the dot lights and the press count goes up.
   - [ ] **Pretend message:** shows a fake message on the screen with the breathing ring and
         buzz. Press the knob, and the ring goes off ("read").

You can do all of this before Wi-Fi or Gmail are set up.

## Set up Gmail

Use a **new Gmail account just for the box**. Don't use your own. Messages sent to it are what
the box shows.

1. Create the account, then go to **myaccount.google.com → Security** and turn on **2-Step
   Verification**.
2. Go to **myaccount.google.com/apppasswords**, create an app password (name it
   "MessageBox"), and copy the 16 letters.
3. On the box's page, fill in:
   - **1. Home Wi-Fi:** use *Scan for networks*, pick yours and enter the password (2.4 GHz only).
   - **2. Gmail inbox:** enter the box's Gmail address and the app password.
     *Only show mail from* is a list of the addresses allowed to send messages, such as your
     friend's and yours. Leave it empty to show everything (spam included).
   - **Time zone:** the default is Sydney/Melbourne (`AEST-10AEDT,M10.1.0,M4.1.0/3`).
     Brisbane is `AEST-10`, Adelaide `ACST-9:30ACDT,M10.1.0,M4.1.0/3`, Perth `AWST-8`.
4. Press **Save settings and restart**. The box joins your Wi-Fi and the setup Wi-Fi closes. The
   screen shows its address, for example `192.168.1.42 - messagebox.local`. The same page is
   now at **http://messagebox.local** (or the IP) from any phone or computer on your home
   Wi-Fi.
5. Send an email to the box's address. Within a minute (*Check every*, default 60 s) the box
   buzzes and shows it. You can also press **Check mail now**. The **Status** and **Log**
   boxes show what happened, for example `Login refused` if the app password is wrong.

If the home Wi-Fi can't be joined within 20 s, or drops for 30 s, the setup Wi-Fi opens again
on its own. While nobody is connected to it, the box keeps retrying the home Wi-Fi every minute.
**Hold the knob for 5 seconds**, until the ring fills up, to open the setup Wi-Fi by hand, for
example to change networks.

## Sending messages

- **The subject** is the title and **the text** is the message. Short notes get a big font, and
  long ones get smaller so they fit. Quoted replies ("On ... wrote:"), signatures and "Sent from
  my iPhone" are left out. Emoji can't be drawn: hearts become `<3`, smileys `:)`, and the rest
  are dropped.
- **A subject that starts with `!`** is an instruction instead of a message:

  | Subject | Does |
  |---|---|
  | `!led pink` (or red, green, blue, purple, orange, yellow, white, `#rrggbb`, off) | Sets the ring colour |
  | `!rainbow` | Rainbow ring |
  | `!buzz 3` | Heartbeat buzz, 3 times |
  | `!ping` | Green spin and a short buzz (a "thinking of you") |
  | `!clear` | Clears the messages and the screen |

- After reading a mail, the box marks it read in Gmail. Each check takes at most 5 mails, oldest
  first.

## On the device

| Do | Result |
|---|---|
| Turn the knob | Older or newer messages (the screen redraws when you stop turning) |
| Press | Marks the message read (ring off), or jumps back to the newest |
| Hold 5 s | Opens the setup Wi-Fi and shows the steps on the screen |

The newest message is saved, so it's still there after a power cut.

## Files

- `MessageBox/MessageBox.ino`: the firmware (pins and settings at the top)
- `MessageBox/imap.h`: the Gmail (IMAP over TLS) client and the mail text decoding
- `MessageBox/web_page.h`: the setup and test page
- `MessageBox/certs.h`: Google's root certificates, so the box checks it's really talking to Gmail
- `wiring.svg`: the wiring diagram (made by `tools/make_wiring.py`)

## Next steps

- Send messages back, for example with a knob press, using Gmail SMTP and the same app password.
- Partial e-paper refresh for the clock and status line.
- A case: the ring behind a diffuser around the screen.
