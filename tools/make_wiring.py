#!/usr/bin/env python3
"""Draws ../wiring.svg (the MessageBox wiring diagram). Run: python3 make_wiring.py"""
import os

W, H = 1400, 1034
out = []
def a(s): out.append(s)

def text(x, y, s, size=15, anchor="start", weight="normal", fill="var(--ink)", cls=""):
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    a(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{fill}" {cls}>{s}</text>')

def wire(points, color, width=4):
    d = " ".join(f"{'M' if i == 0 else 'L'}{x},{y}" for i, (x, y) in enumerate(points))
    a(f'<path d="{d}" fill="none" stroke="var(--halo)" stroke-width="{width + 4}" stroke-linejoin="round" stroke-linecap="round"/>')
    a(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round" stroke-linecap="round"/>')

POWER = {"5V": "#e03131", "3V3": "#f08c00", "GND": "var(--gnd)"}
def tag(x, y, net, direction):  # a power connection: same label = same wire
    c = POWER[net]
    w = 46 if net != "3V3" else 50
    x2 = x + 24 * direction
    a(f'<line x1="{x}" y1="{y}" x2="{x2}" y2="{y}" stroke="{c}" stroke-width="4"/>')
    bx = x2 if direction > 0 else x2 - w
    a(f'<rect x="{bx}" y="{y - 12}" width="{w}" height="24" rx="12" fill="{c}"/>')
    text(bx + w / 2, y + 5, net, 13, "middle", "bold", "#fff")

def pin(x, y):
    a(f'<circle cx="{x}" cy="{y}" r="5" fill="var(--pad)" stroke="var(--padline)" stroke-width="2"/>')

# ------------------------------------------------------------------ the board
BX0, BX1, BY0, BY1 = 560, 760, 260, 660
def lp(i): return 300 + i * 46
LEFT = ["5V", "GND", "3V3", "GPIO4", "GPIO3", "GPIO2", "GPIO1", "GPIO0"]
RIGHT = ["GPIO5", "GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10", "GPIO20", "GPIO21"]

C = dict(BUSY="#7048e8", SCL="#1c7ed6", DC="#0ca678", SDA="#e64980", CS="#d9480f", RES="#5c940d",
         DIN="#1098ad", CLK="#ae3ec9", DT="#f59f00", SW="#868e96", VIB="#c2255c")

# ------------------------------------------------------------------ wires (drawn first, under the parts)
# e-paper header pins (x = 1000)
EP = {"BUSY": 190, "DC": 230, "SDA": 270, "CS": 310, "SCL": 350, "RES": 390, "GND": 430, "VCC": 470}
wire([(BX1, lp(0)), (800, lp(0)), (800, EP["DC"]), (1000, EP["DC"])], C["DC"])              # GPIO5 -> D/C
wire([(BX1, lp(1)), (830, lp(1)), (830, EP["SDA"]), (1000, EP["SDA"])], C["SDA"])           # GPIO6 -> SDA
wire([(BX1, lp(2)), (860, lp(2)), (860, EP["CS"]), (1000, EP["CS"])], C["CS"])              # GPIO7 -> CS
wire([(BX1, lp(5)), (890, lp(5)), (890, EP["SCL"]), (1000, EP["SCL"])], C["SCL"])           # GPIO10 -> SCL
# LED ring connector (x = 360)
RG = {"5V": 250, "GND": 280, "DIN": 310}
wire([(BX0, lp(5)), (390, lp(5)), (390, RG["DIN"]), (360, RG["DIN"])], C["DIN"])            # GPIO2 -> DIN
# encoder (x = 360)
EN = {"GND": 690, "CLK": 720, "DT": 750, "SW": 780, "+": 810}
wire([(BX0, lp(7)), (470, lp(7)), (470, EN["CLK"]), (360, EN["CLK"])], C["CLK"])            # GPIO0 -> CLK
wire([(BX0, lp(6)), (490, lp(6)), (490, EN["DT"]), (360, EN["DT"])], C["DT"])               # GPIO1 -> DT
wire([(BX1, lp(7)), (785, lp(7)), (785, EN["SW"]), (360, EN["SW"])], C["SW"])               # GPIO21 -> SW
# vibration module (x = 1000)
VB = {"IN": 700, "VCC": 730, "GND": 760}
wire([(BX1, lp(6)), (820, lp(6)), (820, VB["IN"]), (1000, VB["IN"])], C["VIB"])             # GPIO20 -> IN

# 330 ohm resistor on the ring's data wire
a(f'<rect x="380" y="{(RG["DIN"] + lp(5)) / 2 - 28}" width="20" height="56" rx="4" fill="var(--res)" stroke="var(--ink)" stroke-width="1.5"/>')
for k in range(3):
    a(f'<rect x="380" y="{(RG["DIN"] + lp(5)) / 2 - 18 + k * 12}" width="20" height="5" fill="{["#f08c00", "#f08c00", "#5c3d1e"][k]}"/>')
text(410, (RG["DIN"] + lp(5)) / 2 + 5, "330 Ω", 14, weight="bold")

# ------------------------------------------------------------------ the ESP32-C3 SuperMini
a(f'<rect x="{BX0}" y="{BY0}" width="{BX1 - BX0}" height="{BY1 - BY0}" rx="12" fill="#1f1f24" stroke="#000" stroke-width="2"/>')
a(f'<rect x="{(BX0 + BX1) / 2 - 34}" y="{BY0 - 14}" width="68" height="34" rx="8" fill="#adb5bd" stroke="#495057" stroke-width="2"/>')
text((BX0 + BX1) / 2, BY0 + 8, "USB-C", 11, "middle", "bold", "#343a40")
text((BX0 + BX1) / 2, BY1 + 26, "ESP32-C3 SuperMini (top view)", 15, "middle", "bold")
text((BX0 + BX1) / 2, BY1 + 46, "middle pins free for the zip tie", 12, "middle", fill="var(--mute)")
for i, n in enumerate(LEFT):
    a(f'<circle cx="{BX0 + 14}" cy="{lp(i)}" r="8" fill="#c9a227" stroke="#8a6d00" stroke-width="2"/>')
    a(f'<circle cx="{BX0 + 14}" cy="{lp(i)}" r="3" fill="#1f1f24"/>')
    text(BX0 + 28, lp(i) + 5, n, 13, weight="bold", fill="#fff")
    a(f'<line x1="{BX0}" y1="{lp(i)}" x2="{BX0 + 6}" y2="{lp(i)}" stroke="#c9a227" stroke-width="4"/>')
for i, n in enumerate(RIGHT):
    a(f'<circle cx="{BX1 - 14}" cy="{lp(i)}" r="8" fill="#c9a227" stroke="#8a6d00" stroke-width="2"/>')
    a(f'<circle cx="{BX1 - 14}" cy="{lp(i)}" r="3" fill="#1f1f24"/>')
    text(BX1 - 28, lp(i) + 5, n, 13, "end", "bold", "#fff")
    a(f'<line x1="{BX1 - 6}" y1="{lp(i)}" x2="{BX1}" y2="{lp(i)}" stroke="#c9a227" stroke-width="4"/>')
tag(BX0, lp(0), "5V", -1)
tag(BX0, lp(1), "GND", -1)
tag(BX0, lp(2), "3V3", -1)
# the middle two pins on each side stay free: the zip tie goes round the board between them
zy = (lp(3) + lp(4)) / 2
a(f'<rect x="{BX0 - 14}" y="{zy - 4}" width="{BX1 - BX0 + 28}" height="8" rx="3" fill="#e9ecef" stroke="#868e96" stroke-width="1"/>')
for i in (3, 4):
    text(BX0 - 8, lp(i) + 5, "keep free", 12, "end", fill="var(--mute)")
    text(BX1 + 8, lp(i) + 5, "keep free", 12, fill="var(--mute)")
text(BX1 + 20, zy + 5, "← zip tie", 12, weight="bold", fill="#868e96")

# ------------------------------------------------------------------ e-paper
a('<rect x="1010" y="150" width="340" height="360" rx="10" fill="var(--part)" stroke="var(--ink)" stroke-width="2"/>')
a('<rect x="1090" y="168" width="242" height="324" rx="4" fill="var(--paper)" stroke="var(--mute)"/>')
text(1211, 300, "WeAct 3.7\"", 20, "middle", "bold")
text(1211, 326, "e-paper", 20, "middle", "bold")
text(1211, 352, "240 × 416", 15, "middle")
text(1211, 374, "(GDEY037T03)", 13, "middle", fill="var(--mute)")
for n, y in EP.items():
    a(f'<line x1="1000" y1="{y}" x2="1010" y2="{y}" stroke="var(--ink)" stroke-width="3"/>')
    pin(1000, y)
    text(1020, y + 5, {"DC": "D/C"}.get(n, n), 13, weight="bold")
tag(1000, EP["GND"], "GND", -1)
tag(1000, EP["RES"], "3V3", -1)
text(990, EP["BUSY"] + 5, "not connected", 12, "end", fill="var(--mute)")
tag(1000, EP["VCC"], "3V3", -1)

# ------------------------------------------------------------------ LED ring
cx, cy, r = 190, 280, 100
a(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="#1f1f24" stroke-width="34"/>')
import math
for k in range(12):
    ang = math.radians(k * 30 - 90)
    x, y = cx + r * math.cos(ang), cy + r * math.sin(ang)
    a(f'<rect x="{x - 11}" y="{y - 11}" width="22" height="22" rx="3" fill="#f8f9fa" transform="rotate({k * 30} {x} {y})"/>')
    a(f'<circle cx="{x}" cy="{y}" r="5" fill="#ff6b9d" opacity=".85"/>')
text(cx, cy - 6, "WS2812B", 15, "middle", "bold")
text(cx, cy + 14, "12-LED ring", 13, "middle")
a('<rect x="300" y="232" width="60" height="96" rx="6" fill="var(--part)" stroke="var(--ink)" stroke-width="2"/>')
for n, y in RG.items():
    pin(360, y)
    text(306, y + 5, {"DIN": "IN"}.get(n, n), 12, weight="bold")
tag(360, RG["5V"], "5V", 1)
tag(360, RG["GND"], "GND", 1)
# capacitor across the ring supply
a('<g transform="translate(150,420)">'
  '<line x1="0" y1="0" x2="0" y2="14" stroke="#e03131" stroke-width="3"/>'
  '<line x1="-14" y1="14" x2="14" y2="14" stroke="var(--ink)" stroke-width="3"/>'
  '<path d="M-14,24 Q0,16 14,24" fill="none" stroke="var(--ink)" stroke-width="3"/>'
  '<line x1="0" y1="21" x2="0" y2="36" stroke="var(--gnd)" stroke-width="3"/></g>')
text(172, 442, "470 µF", 14, weight="bold")
text(172, 460, "5V to GND, at the ring", 12, fill="var(--mute)")
text(172, 476, "(long leg / + to 5V)", 12, fill="var(--mute)")

# ------------------------------------------------------------------ rotary encoder
a('<rect x="150" y="668" width="190" height="166" rx="10" fill="#1c3f94" stroke="var(--ink)" stroke-width="2"/>')
a('<circle cx="218" cy="751" r="44" fill="#adb5bd" stroke="#495057" stroke-width="3"/>')
a('<circle cx="218" cy="751" r="22" fill="#343a40"/>')
text(218, 856, "EC11 / KY-040 encoder", 14, "middle", "bold")
for n, y in EN.items():
    a(f'<line x1="340" y1="{y}" x2="360" y2="{y}" stroke="var(--ink)" stroke-width="3"/>')
    pin(360, y)
    lab = {"CLK": "S1", "DT": "S2", "SW": "KEY", "+": "5V"}.get(n, n)
    text(334, y + 5, lab, 12, "end", "bold", "#fff")
tag(360, EN["GND"], "GND", 1)
tag(360, EN["+"], "3V3", 1)
text(440, EN["+"] + 5, "← 3V3, not 5V!", 13, weight="bold", fill="#e03131")
text(300, EN["CLK"] - 9, "CLK", 10, "end", fill="#dee2e6")
text(300, EN["DT"] - 9, "DT", 10, "end", fill="#dee2e6")
text(300, EN["SW"] - 9, "SW", 10, "end", fill="#dee2e6")

# ------------------------------------------------------------------ vibration module
a('<rect x="1010" y="672" width="190" height="120" rx="10" fill="#1f1f24" stroke="var(--ink)" stroke-width="2"/>')
a('<circle cx="1140" cy="718" r="30" fill="#adb5bd" stroke="#495057" stroke-width="3"/>')
text(1105, 816, "Vibration motor module", 14, "middle", "bold")
for n, y in VB.items():
    a(f'<line x1="1000" y1="{y}" x2="1010" y2="{y}" stroke="var(--ink)" stroke-width="3"/>')
    pin(1000, y)
    text(1018, y + 5, n, 12, weight="bold", fill="#fff")
tag(1000, VB["VCC"], "5V", -1)
tag(1000, VB["GND"], "GND", -1)

# ------------------------------------------------------------------ wire labels (GPIO -> pin)
def wl(x, y, s, c):
    a(f'<rect x="{x - 3}" y="{y - 14}" width="{len(s) * 7.4 + 8}" height="19" rx="4" fill="var(--bg)" opacity=".92"/>')
    text(x + 1, y, s, 12, weight="bold", fill=c)
wl(904, EP["DC"] - 7, "GPIO5 → D/C", C["DC"])
wl(904, EP["SDA"] - 7, "GPIO6 → SDA", C["SDA"])
wl(904, EP["CS"] - 7, "GPIO7 → CS", C["CS"])
wl(904, EP["SCL"] - 7, "GPIO10 → SCL", C["SCL"])
wl(398, lp(5) + 20, "GPIO2 → IN", C["DIN"])
wl(372, EN["CLK"] + 18, "GPIO0 → S1", C["CLK"])
wl(500, EN["DT"] - 7, "GPIO1 → S2", C["DT"])
wl(560, EN["SW"] - 7, "GPIO21 → KEY", C["SW"])
wl(830, VB["IN"] - 7, "GPIO20 → IN", C["VIB"])

# ------------------------------------------------------------------ title and notes
text(40, 52, "MessageBox wiring", 28, weight="bold")
text(40, 80, "ESP32-C3 SuperMini · 3.7\" e-paper · WS2812B ring · rotary encoder · vibration motor", 15, fill="var(--mute)")
a('<g transform="translate(40,105)">')
for k, (net, note) in enumerate([("5V", "USB 5 V: LED ring + motor"), ("3V3", "3.3 V: e-paper + encoder"), ("GND", "all GNDs together")]):
    a(f'<g transform="translate({k * 260},0)">')
    c = POWER[net]
    a(f'<rect x="0" y="0" width="50" height="24" rx="12" fill="{c}"/>')
    text(25, 17, net, 13, "middle", "bold", "#fff")
    text(60, 17, note, 13)
    a('</g>')
a('</g>')
text(40, 152, "Same label = same wire. Match the pins by the names printed on each board; the order on yours may differ.", 13, fill="var(--mute)")

notes = [
    "Power everything from the C3's USB-C. Keep LED brightness at or below 60 of 255 (the default): 12 LEDs at full white draw about 0.7 A.",
    "The encoder's + pin goes to 3V3. Its pull-up resistors would put 5 V on the C3's pins, and the C3 only takes 3.3 V.",
    "If the ring flickers or shows wrong colours, feed its 5V through a 1N4001 diode (4.3 V), so the C3's 3.3 V data counts as high.",
    "E-paper RES goes to 3V3 and BUSY isn't connected, so GPIO3, GPIO4, GPIO8 and GPIO9 stay free for the zip tie round the board.",
    "GPIO21 is also the C3's TX pin and chatters while it boots, so don't hold the knob down while plugging the box in.",
]
for k, n in enumerate(notes):
    text(40, 905 + k * 24, f"{k + 1}. {n}", 13)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="system-ui, -apple-system, Segoe UI, Roboto, sans-serif">
<style>
svg{{--bg:#ffffff;--ink:#1d1b1a;--mute:#6f6a66;--gnd:#343a40;--halo:#ffffff;--pad:#f1f3f5;--padline:#495057;--part:#f1f3f5;--paper:#e9ecef;--res:#e9d8a6}}
@media (prefers-color-scheme: dark){{svg{{--bg:#18181b;--ink:#f1ece8;--mute:#a1a1aa;--gnd:#adb5bd;--halo:#18181b;--pad:#27272a;--padline:#d4d4d8;--part:#27272a;--paper:#3f3f46;--res:#a68a4d}}}}
</style>
<rect width="{W}" height="{H}" fill="var(--bg)"/>
{chr(10).join(out)}
</svg>
'''
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "wiring.svg")
open(path, "w").write(svg)
print("wrote", os.path.normpath(path))
