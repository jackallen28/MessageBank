#!/usr/bin/env python3
"""
MessageBox enclosure: builds a STEP assembly of the housing with every component in place.

    pip install cadquery          (tested with CadQuery 2.8)
    python3 make_enclosure.py     -> MessageBox-enclosure.step  (the assembly, with colours)
                                     print/*.stl                (the four printed parts)

Shape (side view): a low, flat front deck with the knob on top, rising at the back into a
block whose sloped face holds the screen.

  * the 3.7" e-paper sits landscape on the sloped face (SCREEN_TILT from vertical), behind a
    window with a bevelled edge
  * the knob stands up out of the deck, inside a black light shroud and a WHITE diffuser ring
    that covers the WS2812 ring, lying flat. The shroud's outer and inner walls stop the light
    bleeding into the green body.
  * inside, at the back: the ESP32-C3 SuperMini on a cradle, its USB-C reaching a cut-out in the
    rear wall
  * the vibration motor is screwed under the shroud, so the haptic ticks come through the knob

Printed parts: body (green), base (green), shroud (black), diffuser (white: print in white PLA/
PETG, 2.5 mm thick; or 1 mm natural/clear + light sanding for a smoother glow).

All sizes are in mm. Measure your own parts and change the numbers in the PARAMETERS section.
The e-paper module is WeAct's own 3D model (downloaded on first run); everything else is drawn
here from datasheet sizes.
"""
import math
import os
import urllib.request

import cadquery as cq
from cadquery import Vector as V

HERE = os.path.dirname(os.path.abspath(__file__))

# ============================================================== PARAMETERS

W = 140.0            # width
DECK_H = 30.0        # height of the flat front deck
DECK_L = 84.0        # deck length, front edge to the bottom of the screen face
SCREEN_TILT = 30.0   # screen face leans back from vertical (degrees)
FACE_L = 72.0        # length of the screen face, along the slope
TOP_L = 25.0         # flat top behind the screen
WALL = 2.5           # wall thickness
BASE_T = 3.0         # base plate thickness
EDGE_R = 4.0         # outside corner rounding

# WeAct 3.7" e-paper module (from WeAct's outline drawing), mounted landscape
EPD_PCB_L, EPD_PCB_W, EPD_PCB_T = 105.79, 53.80, 1.69
EPD_HOLE_INSET = 2.80          # M3 holes (3.2 mm), 2.8 mm in from each edge
PANEL_L, PANEL_W, PANEL_T = 92.99, 53.00, 1.20   # glass panel on the front of the board
PANEL_END_GAP = 6.40           # board edge to panel edge (both ends)
ACTIVE_L, ACTIVE_W = 81.54, 47.04
ACTIVE_FPC_BORDER = 7.00       # glass border on the ribbon-cable end (the header end): check yours
WINDOW_MARGIN = 0.60           # window is this much bigger than the active area on each side
EPD_TAPE = 0.4                 # foam tape between glass and the wall

# LED ring, diffuser, shroud, knob
RING_OD, RING_ID, RING_T = 50.0, 36.0, 1.6
LED = 5.0                      # WS2812B 5050
LED_H = 1.6
DIFF_T = 2.5                   # diffuser thickness
DIFF_GAP = 2.5                 # air gap diffuser -> LEDs (spreads the light)
HUB_R = 15.0                   # black centre disc around the knob
SLEEVE_R_IN, SLEEVE_R_OUT = 27.0, 29.0   # black outer wall
LEDGE_R = 26.0                 # the diffuser rests on this step
PLATE_T = 2.0                  # shroud back plate (the encoder mounts to it)
FLANGE_R = 37.0
SCREW_R = 33.0                 # shroud screws (M2.5 x 8, self-tapping)
KNOB_D, KNOB_H = 15.0, 16.0
KNOB_LIFT = 0.5                # knob skirt above the deck
ENC_SHAFT_L = 20.0             # EC11 shaft length from the mounting face

# ESP32-C3 SuperMini
ESP_L, ESP_W, ESP_T = 22.52, 18.0, 1.0
ESP_X = W - 35.0               # centre, across the width (near the screen's header end)
ESP_PCB_Z = 12.0               # underside of the board above the bottom (room for wires/pins)
USB_W, USB_L, USB_H, USB_OVERHANG = 8.94, 7.35, 3.2, 1.0
USB_CUT_W, USB_CUT_H, USB_CUT_R = 12.5, 7.5, 2.0   # rear-wall hole: fits a USB-C plug's overmould

# Vibration module (measure yours)
VIB_W, VIB_H, VIB_T = 25.0, 21.0, 1.6
VIB_HOLE_DX, VIB_HOLE_DY = 9.0, 7.5   # holes from the board centre (two, along one edge)
VIB_MOTOR_D, VIB_MOTOR_T = 10.0, 3.0

GREEN = cq.Color(0.16, 0.55, 0.30)
BLACK = cq.Color(0.06, 0.06, 0.07)
WHITE = cq.Color(0.97, 0.97, 0.95)
PCB_BLACK = cq.Color(0.10, 0.10, 0.10)
PCB_BLUE = cq.Color(0.10, 0.25, 0.65)
METAL = cq.Color(0.75, 0.76, 0.78)
EINK = cq.Color(0.86, 0.86, 0.82)
GLASS = cq.Color(0.25, 0.25, 0.27)
KNOB_BLUE = cq.Color(0.20, 0.50, 0.95)
GOLD = cq.Color(0.80, 0.65, 0.25)

# ============================================================== derived sizes

S, C = math.sin(math.radians(SCREEN_TILT)), math.cos(math.radians(SCREEN_TILT))
TOTAL_H = DECK_H + FACE_L * C
DEPTH = DECK_L + FACE_L * S + TOP_L
EPD_U0 = (W - EPD_PCB_L) / 2            # module centred on the face
EPD_V0 = (FACE_L - EPD_PCB_W) / 2
RING_U, RING_V = W / 2, DECK_L / 2      # knob centred on the deck

# ============================================================== helpers
# Parts are drawn in a local frame: u across (X), w into the box from the outside surface (Y),
# v along the surface (Z). F() puts a part on the sloped screen face, D() on the flat deck
# (where v runs from the front edge backwards).


def F(shape):
    return shape.rotate(V(0, 0, 0), V(1, 0, 0), -SCREEN_TILT).translate(V(0, DECK_L, DECK_H))


def D(shape):
    return shape.rotate(V(0, 0, 0), V(1, 0, 0), -90).translate(V(0, 0, DECK_H))


def box(x0, x1, y0, y1, z0, z1):
    return cq.Solid.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, y0, z0))


def cyl_w(u, v, r, w0, w1):
    """Cylinder along w (into the box), from w0 to w1, in local coordinates."""
    return cq.Solid.makeCylinder(r, w1 - w0, V(u, w0, v), V(0, 1, 0))


def ring_w(u, v, r_in, r_out, w0, w1):
    return cyl_w(u, v, r_out, w0, w1).cut(cyl_w(u, v, r_in, w0 - 1, w1 + 1))


def cyl_z(x, y, r, z0, z1):
    return cq.Solid.makeCylinder(r, z1 - z0, V(x, y, z0), V(0, 0, 1))


def union(*shapes):
    out = shapes[0]
    for s in shapes[1:]:
        out = out.fuse(s)
    return out.clean()


def polar(r, deg):
    return r * math.cos(math.radians(deg)), r * math.sin(math.radians(deg))


# ============================================================== the body (green)

profile = [(0, 0), (0, DECK_H), (DECK_L, DECK_H), (DECK_L + FACE_L * S, TOTAL_H), (DEPTH, TOTAL_H), (DEPTH, 0)]
body = (cq.Workplane("YZ").polyline(profile).close()
        .extrude(W).edges("not(<Z)").fillet(EDGE_R).faces("<Z").shell(-WALL))
body = body.val().Solids()[0]   # (booleans on the shell's compound wrapper lose the solid)

# -- the screen window (with a 45 degree bevel on the outside)
panel_u0 = EPD_U0 + PANEL_END_GAP
act_u0 = panel_u0 + (PANEL_L - ACTIVE_L - ACTIVE_FPC_BORDER)   # header/ribbon end is on the right
act_v0 = EPD_V0 + (EPD_PCB_W - ACTIVE_W) / 2
win = (act_u0 - WINDOW_MARGIN, act_u0 + ACTIVE_L + WINDOW_MARGIN,
       act_v0 - WINDOW_MARGIN, act_v0 + ACTIVE_W + WINDOW_MARGIN)
bevel = 1.2
cut = box(win[0], win[1], -1, WALL + 1, win[2], win[3])
bevel_plane = cq.Plane(origin=(0, -0.01, 0), xDir=(-1, 0, 0), normal=(0, 1, 0))  # local x = -u, y = v
cut = cut.fuse(cq.Workplane(bevel_plane).center(-(win[0] + win[1]) / 2, (win[2] + win[3]) / 2)
               .rect(win[1] - win[0] + 2 * bevel, win[3] - win[2] + 2 * bevel).extrude(bevel + 0.01, taper=45).val())
body = body.cut(F(cut))
# -- the round opening in the deck for the shroud
body = body.cut(D(cyl_w(RING_U, RING_V, SLEEVE_R_OUT + 0.2, -1, WALL + 1)))

# -- screw bosses inside the screen face and under the deck
pcb_w0 = WALL + EPD_TAPE + PANEL_T              # front of the e-paper board
bosses, pilots = [], []
for du in (EPD_HOLE_INSET, EPD_PCB_L - EPD_HOLE_INSET):
    for dv in (EPD_HOLE_INSET, EPD_PCB_W - EPD_HOLE_INSET):
        u, v = EPD_U0 + du, EPD_V0 + dv
        bosses.append(cyl_w(u, v, 3.0, WALL - 0.5, pcb_w0))
        pilots.append(cyl_w(u, v, 1.1, pcb_w0 - 3.5, pcb_w0 + 0.1))     # M2.5 x 5 self-tapping
body = body.fuse(F(union(*bosses))).cut(F(union(*pilots)))
plate_w0 = DIFF_T + DIFF_GAP + LED_H + RING_T  # top of the shroud's back plate (under the deck)
SCREW_ANGLES = (90, 210, 330)
bosses, pilots = [], []
for a in SCREW_ANGLES:
    du, dv = polar(SCREW_R, a)
    bosses.append(cyl_w(RING_U + du, RING_V + dv, 3.5, WALL - 0.5, plate_w0))
    pilots.append(cyl_w(RING_U + du, RING_V + dv, 1.1, plate_w0 - 6, plate_w0 + 0.1))  # M2.5 x 8
body = body.fuse(D(union(*bosses))).cut(D(union(*pilots)))

# -- base screw posts (vertical, in the corners, merged into the walls) and the USB-C hole
cx0, cy0, cy1 = WALL + 3.5, WALL + 3.5, DEPTH - WALL - 3.5
corner = [(cx0, cy0), (W - cx0, cy0), (cx0, cy1), (W - cx0, cy1)]
for x, y in corner:
    body = body.fuse(cyl_z(x, y, 4.0, BASE_T, BASE_T + 10)).cut(cyl_z(x, y, 1.25, BASE_T - 0.1, BASE_T + 9))
usb_z = ESP_PCB_Z + ESP_T + USB_H / 2
usb_cut = (cq.Workplane("XZ", origin=(0, DEPTH + 1, 0)).center(ESP_X, usb_z)
           .rect(USB_CUT_W, USB_CUT_H).extrude(WALL + 2).edges("|Y").fillet(USB_CUT_R)).val()
body = body.cut(usb_cut).clean()

# ============================================================== base plate (green)

bx0, bx1 = WALL + 0.25, W - WALL - 0.25
by0, by1 = WALL + 0.25, DEPTH - WALL - 0.25
base = cq.Workplane("XY").box(bx1 - bx0, by1 - by0, BASE_T, centered=False).translate((bx0, by0, 0)) \
    .edges("|Z").fillet(1.5).val()
for x, y in corner:   # countersunk M3 holes from underneath
    base = base.cut(cyl_z(x, y, 1.7, -1, BASE_T + 1))
    base = base.cut(cq.Solid.makeCone(3.3, 1.7, 1.6, V(x, y, -0.001), V(0, 0, 1)))
for x, y in [(20, 20), (W - 20, 20), (20, DEPTH - 20), (W - 20, DEPTH - 20)]:
    base = base.cut(cyl_z(x, y, 6.2, -1, 1.0))   # 12 mm stick-on rubber feet sit in these
# -- ESP32-C3 cradle: a slot holds the far end, a post under the USB end
esp_y1 = DEPTH - WALL - 0.3 - USB_OVERHANG      # board's USB edge
esp_y0 = esp_y1 - ESP_L
ex0, ex1 = ESP_X - ESP_W / 2, ESP_X + ESP_W / 2
slot_block = box(ex0 - 3, ex1 + 3, esp_y0 - 3, esp_y0 + 1.2, BASE_T - 0.01, ESP_PCB_Z + ESP_T + 3)
slot_block = slot_block.cut(box(ex0 - 0.2, ex1 + 0.2, esp_y0 - 1.0, esp_y0 + 2, ESP_PCB_Z - 0.1, ESP_PCB_Z + ESP_T + 0.1))
slot_block = slot_block.cut(box(ex0 + 2, ex1 - 2, esp_y0 - 1.0, esp_y0 + 2, ESP_PCB_Z - 0.1, ESP_PCB_Z + ESP_T + 3.1))
post = box(ESP_X - 3, ESP_X + 3, esp_y1 - 6, esp_y1 - 1, BASE_T - 0.01, ESP_PCB_Z)
base = union(base, slot_block, post)

# ============================================================== light shroud (black)

w_led_front = DIFF_T + DIFF_GAP
w_ring_front = w_led_front + LED_H
plate_w1 = plate_w0 + PLATE_T
sh = union(
    ring_w(RING_U, RING_V, SLEEVE_R_IN, SLEEVE_R_OUT, 0, DIFF_T),          # top lip (holds the diffuser)
    ring_w(RING_U, RING_V, LEDGE_R, SLEEVE_R_OUT, DIFF_T, plate_w0),       # wall with the diffuser ledge
    cyl_w(RING_U, RING_V, HUB_R, 0, plate_w0),                             # black hub round the knob
    cyl_w(RING_U, RING_V, FLANGE_R, plate_w0, plate_w1),                   # back plate / flange
)
sh = sh.cut(cyl_w(RING_U, RING_V, 5.5, -1, plate_w0))                      # pocket for the encoder nut
sh = sh.cut(cyl_w(RING_U, RING_V, 3.6, plate_w0 - 1, plate_w1 + 1))        # 7 mm bushing hole
for a in SCREW_ANGLES:
    du, dv = polar(SCREW_R, a)
    sh = sh.cut(cyl_w(RING_U + du, RING_V + dv, 1.4, plate_w0 - 1, plate_w1 + 1))
sh = sh.cut(box(RING_U - 4, RING_U + 4, plate_w0 - 0.5, plate_w1 + 1, RING_V + 18.5, RING_V + 24.5))  # ring wires
# vibration-module standoffs under the plate (towards the front of the box)
vib_cv = RING_V - 27.0
for sx in (-1, 1):
    hu, hv = RING_U + sx * VIB_HOLE_DX, vib_cv - VIB_HOLE_DY
    sh = sh.fuse(cyl_w(hu, hv, 2.5, plate_w1 - 0.01, plate_w1 + 3)).cut(cyl_w(hu, hv, 0.9, plate_w1, plate_w1 + 3.1))
shroud = sh.clean()

# ============================================================== diffuser (white)

diffuser = ring_w(RING_U, RING_V, HUB_R + 0.2, SLEEVE_R_IN - 0.2, 0, DIFF_T)

# ============================================================== bought parts

# LED ring
led_ring_pcb = ring_w(RING_U, RING_V, RING_ID / 2, RING_OD / 2, w_ring_front, w_ring_front + RING_T)
leds = []
r_led = (RING_OD + RING_ID) / 4
for k in range(12):
    du, dv = polar(r_led, 90 - k * 30)
    leds.append(box(RING_U + du - LED / 2, RING_U + du + LED / 2, w_led_front, w_ring_front,
                    RING_V + dv - LED / 2, RING_V + dv + LED / 2)
                .rotate(V(RING_U + du, 0, RING_V + dv), V(RING_U + du, 1, RING_V + dv), k * 30))
leds = union(*leds)

# EC11 encoder module: body, bushing, nut, shaft, round board with 5 pins
enc_body = box(RING_U - 6, RING_U + 6, plate_w1, plate_w1 + 6.5, RING_V - 6.25, RING_V + 6.25)
enc_metal = union(cyl_w(RING_U, RING_V, 3.5, plate_w1 - 7, plate_w1),
                  cyl_w(RING_U, RING_V, 4.9, plate_w0 - 2, plate_w0),
                  cyl_w(RING_U, RING_V, 3.0, plate_w1 - ENC_SHAFT_L, plate_w1 - 7))
enc_pcb = cyl_w(RING_U, RING_V, 13.0, plate_w1 + 6.5, plate_w1 + 8.1)
enc_pins = union(*[box(RING_U - 5.08 + 2.54 * i - 0.32, RING_U - 5.08 + 2.54 * i + 0.32, plate_w1 + 8.1,
                       plate_w1 + 16.6, RING_V + 10.86, RING_V + 11.5) for i in range(5)])
knob_w0 = -KNOB_LIFT
knob = cyl_w(RING_U, RING_V, KNOB_D / 2, knob_w0 - KNOB_H, knob_w0).cut(cyl_w(RING_U, RING_V, 3.0, knob_w0 - KNOB_H + 2, knob_w0 + 1))
knob_cap = cyl_w(RING_U, RING_V, KNOB_D / 2 - 1.5, knob_w0 - KNOB_H - 0.6, knob_w0 - KNOB_H)

# vibration module (board parallel to the deck, under the shroud, motor underneath)
vib_w0 = plate_w1 + 3
vib_pcb = box(RING_U - VIB_W / 2, RING_U + VIB_W / 2, vib_w0, vib_w0 + VIB_T, vib_cv - VIB_H / 2, vib_cv + VIB_H / 2)
for sx in (-1, 1):
    vib_pcb = vib_pcb.cut(cyl_w(RING_U + sx * VIB_HOLE_DX, vib_cv - VIB_HOLE_DY, 1.5, vib_w0 - 1, vib_w0 + 3))
vib_motor = cyl_w(RING_U, vib_cv + 3, VIB_MOTOR_D / 2, vib_w0 + VIB_T, vib_w0 + VIB_T + VIB_MOTOR_T)

# e-paper: glass panel + e-ink active area, and WeAct's board model
panel = box(panel_u0, panel_u0 + PANEL_L, pcb_w0 - PANEL_T, pcb_w0,
            EPD_V0 + (EPD_PCB_W - PANEL_W) / 2, EPD_V0 + (EPD_PCB_W + PANEL_W) / 2)
active = box(act_u0, act_u0 + ACTIVE_L, pcb_w0 - PANEL_T - 0.05, pcb_w0 - PANEL_T, act_v0, act_v0 + ACTIVE_W)

weact_path = os.path.join(HERE, "vendor", "WeAct-EpaperModule-3.7 Board 3D.step")
if not os.path.exists(weact_path):
    os.makedirs(os.path.dirname(weact_path), exist_ok=True)
    url = ("https://raw.githubusercontent.com/WeActStudio/WeActStudio.EpaperModule/master/Hardware/"
           "WeAct-EpaperModule-3.7%20Board%203D.step")
    print("downloading WeAct's e-paper model...")
    urllib.request.urlretrieve(url, weact_path)
weact = cq.importers.importStep(weact_path).val()
# their model: board in XY, components on +Z, header at +X. Ours (landscape): components face
# into the box (+w), header on the right (+u).
weact = weact.rotate(V(0, 0, 0), V(1, 0, 0), -90)
pcb_solid = max(weact.Solids(), key=lambda s: s.BoundingBox().xlen * s.BoundingBox().zlen)
pb = pcb_solid.BoundingBox()
weact = weact.translate(V(EPD_U0 - pb.xmin, pcb_w0 - pb.ymin, EPD_V0 - pb.zmin))
epd_pcb = [s for s in weact.Solids() if s.BoundingBox().xlen > 50]
epd_parts = [s for s in weact.Solids() if s.BoundingBox().xlen <= 50]

# ESP32-C3 SuperMini
esp_pcb = box(ex0, ex1, esp_y0, esp_y1, ESP_PCB_Z, ESP_PCB_Z + ESP_T)
esp_usb = box(ESP_X - USB_W / 2, ESP_X + USB_W / 2, esp_y1 + USB_OVERHANG - USB_L, esp_y1 + USB_OVERHANG,
              ESP_PCB_Z + ESP_T, ESP_PCB_Z + ESP_T + USB_H)
esp_parts = union(box(ESP_X - 2.5, ESP_X + 2.5, esp_y0 + 9, esp_y0 + 14, ESP_PCB_Z + ESP_T, ESP_PCB_Z + ESP_T + 0.9),
                  box(ESP_X - 4, ESP_X + 1, esp_y0 + 1, esp_y0 + 3, ESP_PCB_Z + ESP_T, ESP_PCB_Z + ESP_T + 1.2))

# ============================================================== the assembly

asm = cq.Assembly(name="MessageBox")
asm.add(body, name="body", color=GREEN)
asm.add(base, name="base", color=GREEN)
asm.add(D(shroud), name="light_shroud", color=BLACK)
asm.add(D(diffuser), name="diffuser", color=WHITE)
asm.add(D(knob), name="knob", color=BLACK)
asm.add(D(knob_cap), name="knob_cap", color=KNOB_BLUE)
asm.add(D(led_ring_pcb), name="led_ring_pcb", color=PCB_BLACK)
asm.add(D(leds), name="led_ring_leds", color=cq.Color(0.95, 0.95, 0.92))
asm.add(D(enc_body), name="encoder_body", color=METAL)
asm.add(D(enc_metal), name="encoder_shaft_nut", color=METAL)
asm.add(D(enc_pcb), name="encoder_pcb", color=PCB_BLUE)
asm.add(D(enc_pins), name="encoder_pins", color=GOLD)
asm.add(D(vib_pcb), name="vibration_pcb", color=PCB_BLACK)
asm.add(D(vib_motor), name="vibration_motor", color=METAL)
asm.add(F(panel), name="epaper_glass", color=GLASS)
asm.add(F(active), name="epaper_active_area", color=EINK)
asm.add(F(cq.Compound.makeCompound(epd_pcb)), name="epaper_module_pcb", color=PCB_BLACK)
asm.add(F(cq.Compound.makeCompound(epd_parts)), name="epaper_module_parts", color=METAL)
asm.add(esp_pcb, name="esp32c3_pcb", color=PCB_BLACK)
asm.add(esp_usb, name="esp32c3_usb_c", color=METAL)
asm.add(esp_parts, name="esp32c3_parts", color=cq.Color(0.3, 0.3, 0.3))

out = os.path.join(HERE, "MessageBox-enclosure.step")
asm.export(out)
print("wrote", out, "(%.0f x %.0f x %.0f mm)" % (W, DEPTH, TOTAL_H))

os.makedirs(os.path.join(HERE, "print"), exist_ok=True)
# printed parts, each in a good printing orientation
stl = lambda shape, name, tol=0.05: cq.exporters.export(cq.Workplane().add(shape), os.path.join(HERE, "print", name),
                                                         tolerance=tol, angularTolerance=0.1)
body_side = body.rotate(V(0, 0, 0), V(0, 1, 0), 90)   # on its side: the side profile flat on the bed
bb = body_side.BoundingBox()
stl(body_side.translate(V(-bb.xmin, -bb.ymin, -bb.zmin)), "body.stl")
stl(base, "base.stl")
stl(shroud.rotate(V(0, 0, 0), V(1, 0, 0), 90), "light_shroud.stl", 0.02)  # top lip on the bed
stl(diffuser.rotate(V(0, 0, 0), V(1, 0, 0), 90), "diffuser.stl", 0.02)
print("wrote print/*.stl")

# for previews and checks
if os.environ.get("MB_GLTF"):
    asm.export(os.environ["MB_GLTF"])
    print("wrote", os.environ["MB_GLTF"])
