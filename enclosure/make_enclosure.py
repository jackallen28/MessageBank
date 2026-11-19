#!/usr/bin/env python3
"""
MessageBox enclosure: builds a STEP assembly of the housing with every component in place.

    pip install cadquery          (tested with CadQuery 2.8)
    python3 make_enclosure.py     -> MessageBox-enclosure.step  (the assembly, with colours)
                                     print/*.stl                (the printed parts)

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

import numpy as np
import os
import urllib.request

import cadquery as cq
from cadquery import Vector as V

HERE = os.path.dirname(os.path.abspath(__file__))

# ============================================================== PARAMETERS

W = 140.0            # width
DECK_H = 48.5        # height of the flat front deck: 35 mm under the encoder's mounting face for its wires
DECK_L = 84.0        # deck length, front edge to the bottom of the screen face
SCREEN_TILT = 30.0   # screen face leans back from vertical (degrees)
FACE_L = 72.0        # length of the screen face, along the slope
TOP_L = 25.0         # flat top behind the screen
WALL = 2.5           # wall thickness
BASE_T = 3.0         # base plate thickness
EDGE_R = 4.0         # outside corner rounding
# Ribbed texture on the walls: horizontal grooves, the same way as the layer lines (the body prints
# upright, open side down), so 0.2 mm layer lines vanish into deliberate 2 mm ribs. The deck top and
# the top behind the screen print as smooth top surfaces and stay plain.
TEX_PITCH, TEX_GROOVE, TEX_DEPTH = 2.0, 1.0, 0.6
TEX_MARGIN = 3.0     # smooth band round the screen and the USB-C socket
# WeAct 3.7" e-paper module (from WeAct's outline drawing), mounted landscape
EPD_PCB_L, EPD_PCB_W, EPD_PCB_T = 105.79, 53.80, 1.69
EPD_HOLE_INSET = 2.80          # M3 holes (3.2 mm), 2.8 mm in from each edge
PANEL_L, PANEL_W, PANEL_T = 92.99, 53.00, 1.20   # glass panel on the front of the board
PANEL_END_GAP = 6.40           # board edge to panel edge (both ends)
ACTIVE_L, ACTIVE_W = 81.54, 47.04
ACTIVE_FPC_BORDER = 7.00       # glass border on the ribbon-cable end (the header end): check yours
WINDOW_MARGIN = 0.60           # window is this much bigger than the active area on each side
EPD_TAPE = 0.4                 # foam tape between glass and the wall
SCREW_PILOT = 2.5              # every screw is the same small self-tapper (about 3 mm), in 2.5 mm pilot holes
SCREW_CLEAR = 3.3              # ...and through 3.3 mm clearance holes
EPD_PILOT = SCREW_PILOT        # the screen's 4 screws go through the board's own 3.2 mm holes
EPD_PILOT_DEPTH = 3.2          # (no longer used: the screen screws into the carrier, see below)

# LED ring, diffuser, shroud, knob
RING_OD, RING_ID, RING_T = 50.0, 36.0, 1.6
LED = 5.0                      # WS2812B 5050
LED_H = 1.6
DIFF_T = 0.4                   # diffuser thickness (two 0.2 mm layers of white PLA/PETG)
DIFF_GAP = 10.0                # LED tops to the diffuser. 12 LEDs on a 43 mm circle are 11.3 mm
                               # apart; at 10 mm the light between two LEDs is within ~9 % of the
                               # light over one, so the ring reads as one smooth band (see README)
HUB_R = 15.0                   # black centre disc around the knob (the diffuser rests on a step at r+1)
ENC_PLATE_W = 8.2              # depth of the plate the encoder screws to (inside the hub)
SLEEVE_R_IN, SLEEVE_R_OUT = 27.0, 29.0   # black outer wall
LEDGE_R = 26.0                 # the diffuser rests on this step
PLATE_T = 2.0                  # encoder plate and LED-channel floor thickness
FLANGE_R = 37.0
SCREW_R = 33.0                 # shroud screws (M2.5 x 8, self-tapping)
KNOB_D, KNOB_H = 20.0, 16.0   # printed knob: plain top, thimble dimples round the side
KNOB_BORE = 5.9                # tight on the KY-040's knurled split 6 mm shaft (6.1 for a D shaft + glue)
KNOB_BORE_DEPTH = 11.0
KNOB_LIFT = 0.5                # knob skirt above the deck
ENC_SHAFT_L = 20.0             # EC11 shaft length from the mounting face

# ESP32-C3 SuperMini
ESP_L, ESP_W, ESP_T = 22.52, 18.0, 1.0
ESP_X = W - 35.0               # centre, across the width (near the screen's header end)
RIDGE_H = 2.0                  # the board lies flat on a ridge this tall (keeps the soldered wire ends off the base)
ESP_PCB_Z = BASE_T + RIDGE_H   # underside of the board
PIN_ROW_DX = 7.62              # the two pin rows are 15.24 mm apart
USB_W, USB_L, USB_H, USB_OVERHANG = 8.94, 7.35, 3.2, 1.0
USB_CUT_W, USB_CUT_H, USB_CUT_R = 10.0, 4.6, 1.5   # through the rear wall: the plug's metal end
USB_POCKET_W, USB_POCKET_H, USB_POCKET_R, USB_POCKET_D = 14.5, 9.0, 3.0, 1.5  # outside recess for the plug's
                               # body, so the wall at the socket is 1 mm and any cable's plug reaches
# a 2 mm zip tie goes round the board over the GPIO3 and GPIO9 pads, which are left free
TIE_W, TIE_T = 2.0, 1.0

# Vibration module (measure yours)
VIB_W, VIB_H, VIB_T = 25.0, 21.0, 1.6
VIB_HOLE_DX, VIB_HOLE_DY = 9.0, 7.5   # holes from the board centre (two, along the wire edge)
VIB_OFF = 27.0                 # board centre this far to the right of the knob; its wire edge faces right
VIB_WIRES = 20.0               # the wires leave that edge and need this much room
RING_WIRES = 15.0              # the LED ring's wired pins hang this far below the ring board
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
outer = cq.Workplane("YZ").polyline(profile).close().extrude(W).edges("not(<Z)").fillet(EDGE_R)
body = outer.faces("<Z").shell(-WALL).val().Solids()[0]   # (booleans on the shell's compound wrapper lose the solid)
outer = outer.val().Solids()[0]

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

# -- the screen. A straight screwdriver can't reach screws in the sloped face from the open bottom
# (the rear wall is in the way), so the e-paper is screwed to a printed CARRIER on the bench (4
# screws from the front, through its corner holes, heads in shallow pockets in the face). The
# carrier slides up the slope 8 mm into two short rails inside the face, against a stop, and one
# screw from underneath, through the carrier's arm into a boss under the deck roof, locks it.
# Tightening that screw lifts the carrier, which presses the screen against the window and up
# into the stop.
pcb_w0 = WALL + EPD_TAPE + PANEL_T              # front of the e-paper board
pcb_w1 = pcb_w0 + EPD_PCB_T
CAR_W0 = pcb_w1 + 4.5                           # carrier plate front: clears the board's parts (3.6 mm)
CAR_W1 = CAR_W0 + 2.0
car_u0, car_u1 = EPD_U0 - 4, EPD_U0 + EPD_PCB_L + 4
car_v0, car_v1 = EPD_V0 - 1, EPD_V0 + EPD_PCB_W + 1
CAR_SLIDE = 8.0                                 # it goes in this much lower, then slides up
RAIL_L = CAR_SLIDE - 1.0                        # rails grip the top 7 mm
LOCK_X = 28.0                                   # the locking screw, across the width
LOCK_Y = 82.0                                   # ...and front to back (under the deck roof)
LOCK_Z = 41.0                                   # top of the carrier's arm
epd_holes = [(EPD_U0 + du, EPD_V0 + dv) for du in (EPD_HOLE_INSET, EPD_PCB_L - EPD_HOLE_INSET)
             for dv in (EPD_HOLE_INSET, EPD_PCB_W - EPD_HOLE_INSET)]
# pockets in the face for the 4 screw heads (the screws come from the front of the board)
body = body.cut(F(union(*[cyl_w(u, v, 3.6, WALL - 1.0, WALL + 0.1) for u, v in epd_holes])))
rails = []
for side in (-1, 1):
    e = car_u0 if side < 0 else car_u1
    post = (e - 2.5, e - 0.2) if side < 0 else (e + 0.2, e + 2.5)
    lip = (e - 2.5, e + 3.0) if side < 0 else (e - 3.0, e + 2.5)
    rails.append(box(post[0], post[1], WALL - 0.5, CAR_W1 + 2.1, car_v1 - RAIL_L, car_v1 + 2.2))  # side wall
    rails.append(box(lip[0], lip[1], CAR_W1 + 0.15, CAR_W1 + 2.1, car_v1 - RAIL_L, car_v1 + 2.2))  # lip behind
    rails.append(box(lip[0], lip[1], WALL - 0.5, CAR_W1 + 2.1, car_v1 + 0.2, car_v1 + 2.2))       # top stop
body = body.fuse(F(union(*rails)))
lock_boss = cyl_z(LOCK_X, LOCK_Y, 4.5, LOCK_Z + 0.3, DECK_H - 0.5).intersect(outer)
body = body.fuse(lock_boss).cut(cyl_z(LOCK_X, LOCK_Y, SCREW_PILOT / 2, LOCK_Z + 0.2, DECK_H - 1.0))

# the carrier itself (printed): a 2 mm plate behind the board, open in the middle and round the
# header, with 4 bosses for the board's screws, and an arm down to the locking screw
car = box(car_u0, car_u1, CAR_W0, CAR_W1, car_v0, car_v1)
car = car.cut(box(EPD_U0 + 12, EPD_U0 + EPD_PCB_L - 22, CAR_W0 - 1, CAR_W1 + 1, EPD_V0 + 9, EPD_V0 + EPD_PCB_W - 9))
car = car.cut(box(EPD_U0 + 97, car_u1 + 1, CAR_W0 - 1, CAR_W1 + 1, EPD_V0 + 5, EPD_V0 + 19.5))   # header
for u, v in epd_holes:
    car = car.fuse(cyl_w(u, v, 3.0, pcb_w1 + 0.05, CAR_W0 + 0.01)).cut(cyl_w(u, v, SCREW_PILOT / 2, pcb_w1 - 0.1, CAR_W1 + 0.1))
car = car.fuse(box(LOCK_X - 6, LOCK_X + 6, CAR_W0 - 2, CAR_W1, -4, car_v0 + 0.01))   # leg down the slope
carrier = F(car).fuse(box(LOCK_X - 6, LOCK_X + 6, LOCK_Y - 5, LOCK_Y + 10, LOCK_Z - 4, LOCK_Z))   # arm
carrier = carrier.intersect(F(box(-50, W + 50, -60, CAR_W1, -80, FACE_L + 20)))   # flat back: prints on it
carrier = carrier.cut(cyl_z(LOCK_X, LOCK_Y, SCREW_CLEAR / 2, LOCK_Z - 5, LOCK_Z + 1)).clean()
carrier = carrier.Solids()[0] if len(carrier.Solids()) == 1 else carrier
w_led_front = DIFF_T + DIFF_GAP                # tops of the LEDs
w_ring_front = w_led_front + LED_H
floor_w0 = w_ring_front + RING_T               # the LED ring sits on the shroud's channel floor
floor_w1 = floor_w0 + PLATE_T
plate_w0 = ENC_PLATE_W                         # the encoder's plate, inside the hub
plate_w1 = plate_w0 + PLATE_T                  # the encoder's mounting face
SCREW_ANGLES = (90, 210, 330)
bosses, pilots = [], []
for a in SCREW_ANGLES:
    du, dv = polar(SCREW_R, a)
    bosses.append(cyl_w(RING_U + du, RING_V + dv, 3.5, WALL - 0.5, floor_w0))
    pilots.append(cyl_w(RING_U + du, RING_V + dv, SCREW_PILOT / 2, floor_w0 - 6, floor_w0 + 0.1))  # 8 mm screw
body = body.fuse(D(union(*bosses))).cut(D(union(*pilots)))

# -- base screw posts (vertical, in the corners, merged into the walls) and the USB-C hole
cx0, cy0, cy1 = WALL + 3.5, WALL + 3.5, DEPTH - WALL - 3.5
corner = [(cx0, cy0), (W - cx0, cy0), (cx0, cy1), (W - cx0, cy1)]
for x, y in corner:
    body = body.fuse(cyl_z(x, y, 4.0, BASE_T, BASE_T + 10)).cut(cyl_z(x, y, 1.25, BASE_T - 0.1, BASE_T + 9))
usb_z = ESP_PCB_Z + ESP_T + USB_H / 2
usb_cut = (cq.Workplane("XZ", origin=(0, DEPTH + 1, 0)).center(ESP_X, usb_z)
           .rect(USB_CUT_W, USB_CUT_H).extrude(WALL + 2).edges("|Y").fillet(USB_CUT_R)).val()
usb_cut = usb_cut.fuse((cq.Workplane("XZ", origin=(0, DEPTH + 1, 0)).center(ESP_X, usb_z)
                        .rect(USB_POCKET_W, USB_POCKET_H).extrude(1 + USB_POCKET_D).edges("|Y").fillet(USB_POCKET_R)).val())
body = body.cut(usb_cut).clean()

# -- fluted texture: a 0.6 mm skin under the outside surface, sliced into 1 mm grooves every 2 mm
# across the width, except a smooth frame round the screen and the ring, round the USB-C hole,
# along the bottom edge and on the rounded side edges
from OCP.BRepOffsetAPI import BRepOffsetAPI_MakeOffsetShape
from OCP.BRepOffset import BRepOffset_Skin
from OCP.GeomAbs import GeomAbs_Arc
_off = BRepOffsetAPI_MakeOffsetShape()
_off.PerformByJoin(outer.wrapped, -TEX_DEPTH, 1e-4, BRepOffset_Skin, False, False, GeomAbs_Arc)
skin = outer.cut(cq.Shape.cast(_off.Shape()))
body_plain = body       # the outer 0.6 mm of the body
zs = [z for z in np.arange(2.0, TOTAL_H - EDGE_R - 1.5, TEX_PITCH)
      if z + TEX_GROOVE < DECK_H - EDGE_R - 0.5 or z > DECK_H + 1.0]   # keep the deck's top edge plain
slabs = cq.Compound.makeCompound([box(-1, W + 1, -1, DEPTH + 1, z, z + TEX_GROOVE) for z in zs])
keep = union(
    F(box(EPD_U0 - TEX_MARGIN, EPD_U0 + EPD_PCB_L + TEX_MARGIN, -2, WALL + 2,
          EPD_V0 - TEX_MARGIN, EPD_V0 + EPD_PCB_W + TEX_MARGIN)),                 # screen frame
    box(ESP_X - USB_POCKET_W / 2 - TEX_MARGIN, ESP_X + USB_POCKET_W / 2 + TEX_MARGIN,
        DEPTH - WALL - 1, DEPTH + 1, 0, usb_z + USB_POCKET_H / 2 + TEX_MARGIN))    # USB-C surround
grooves = skin.intersect(slabs).cut(keep)
body = body.cut(grooves).clean()

# ============================================================== base plate (green)

bx0, bx1 = WALL + 0.25, W - WALL - 0.25
by0, by1 = WALL + 0.25, DEPTH - WALL - 0.25
base = cq.Workplane("XY").box(bx1 - bx0, by1 - by0, BASE_T, centered=False).translate((bx0, by0, 0)) \
    .edges("|Z").fillet(1.5).val()
for x, y in corner:   # countersunk M3 holes from underneath
    base = base.cut(cyl_z(x, y, 1.7, -1, BASE_T + 1))
    base = base.cut(cq.Solid.makeCone(3.3, 1.7, 1.6, V(x, y, -0.001), V(0, 0, 1)))
for x, y in [(20, 20), (W - 20, 20), (20, DEPTH - 35), (W - 20, DEPTH - 35)]:
    base = base.cut(cyl_z(x, y, 6.2, -1, 1.0))   # 12 mm stick-on rubber feet sit in these
# -- ESP32-C3 cradle: a slot holds the far end, a post under the USB end
esp_y1 = DEPTH - WALL - 0.3 - USB_OVERHANG      # board's USB edge
esp_y0 = esp_y1 - ESP_L
ex0, ex1 = ESP_X - ESP_W / 2, ESP_X + ESP_W / 2
# the board lies flat on a ridge between its two pin rows; its far end butts against a stop
ridge = box(ESP_X - PIN_ROW_DX + 1.6, ESP_X + PIN_ROW_DX - 1.6, esp_y0 + 1, esp_y1, BASE_T - 0.01, ESP_PCB_Z)
stop = box(ESP_X - 5, ESP_X + 5, esp_y0 - 2.2, esp_y0 - 0.2, BASE_T - 0.01, ESP_PCB_Z + ESP_T + 1.5)
base = union(base, ridge, stop)
# -- zip tie round the middle of the board: a slot beside each edge, and a groove underneath so
# the base still sits flat
# over the 5th pin from the USB end on each side: GPIO3 and GPIO9, which firmware v9 leaves free
# (its neighbours GPIO4/GPIO2 and GPIO8/GPIO10 carry wires, 2.54 mm either side)
TIE_PIN = 4                                     # 0 = the pin nearest the USB-C socket
esp_mid = esp_y1 - 2.37 - 2.54 * TIE_PIN
tie_x = (ex0 - 0.9, ex1 + 0.9)
for tx in tie_x:
    base = base.cut(box(tx - 0.8, tx + 0.8, esp_mid - 1.3, esp_mid + 1.3, -1, BASE_T + 1))
base = base.cut(box(tie_x[0] - 0.8, tie_x[1] + 0.8, esp_mid - 1.3, esp_mid + 1.3, -1, 1.2)).clean()
# the zip tie itself (for the picture): over the board, down both edges, under the base
tie_top = ESP_PCB_Z + ESP_T + 0.95              # it lies over the C3 chip
tie = union(box(tie_x[0] - 0.5, tie_x[1] + 0.5, esp_mid - TIE_W / 2, esp_mid + TIE_W / 2, tie_top, tie_top + TIE_T),
            box(tie_x[0] - 0.5, tie_x[0] + 0.5, esp_mid - TIE_W / 2, esp_mid + TIE_W / 2, 0.1, tie_top + TIE_T),
            box(tie_x[1] - 0.5, tie_x[1] + 0.5, esp_mid - TIE_W / 2, esp_mid + TIE_W / 2, 0.1, tie_top + TIE_T),
            box(tie_x[0] - 0.5, tie_x[1] + 0.5, esp_mid - TIE_W / 2, esp_mid + TIE_W / 2, 0.1, 1.1),
            box(tie_x[0] - 4.5, tie_x[0] - 0.5, esp_mid - 1.8, esp_mid + 1.8, tie_top - 2, tie_top + 2))   # the head

# ============================================================== light shroud (black)

HUB_IN = 10.0   # inside the hub below the encoder plate (the encoder body is 12 x 12.5 mm)
NUT_POCKET_R = 7.75  # the knob (15 mm) covers it
sh = union(
    ring_w(RING_U, RING_V, SLEEVE_R_IN, SLEEVE_R_OUT, 0, DIFF_T),          # top lip (round the diffuser)
    ring_w(RING_U, RING_V, LEDGE_R, SLEEVE_R_OUT, DIFF_T, floor_w0),       # outer wall, with the diffuser's ledge
    cyl_w(RING_U, RING_V, HUB_R, 0, DIFF_T),                               # black disc round the knob
    cyl_w(RING_U, RING_V, HUB_R + 1, DIFF_T, plate_w1),                    # hub (the diffuser rests on its step)
    ring_w(RING_U, RING_V, HUB_IN, HUB_R + 1, plate_w1, floor_w0),         # hub wall down to the floor
    ring_w(RING_U, RING_V, HUB_IN, FLANGE_R, floor_w0, floor_w1),          # LED channel floor + flange
)
sh = sh.cut(cyl_w(RING_U, RING_V, NUT_POCKET_R, -1, plate_w0))             # pocket: room for a 10 mm socket on the nut
sh = sh.cut(ring_w(RING_U, RING_V, 5.6, 8.9, plate_w1 - 1.0, plate_w1 + 0.1))  # groove for the encoder's anti-rotation tab
sh = sh.cut(cyl_w(RING_U, RING_V, 3.6, plate_w0 - 1, plate_w1 + 1))        # 7 mm bushing hole
for a in SCREW_ANGLES:
    du, dv = polar(SCREW_R, a)
    sh = sh.cut(cyl_w(RING_U + du, RING_V + dv, SCREW_CLEAR / 2, floor_w0 - 1, floor_w1 + 1))
# a 50 degree arc slot in the floor (behind the knob) for the ring's wired pins to drop through:
# turn the ring so its pads sit over it
slot = (cq.Workplane("XZ", origin=(0, floor_w1 + 1, 0))
        .polyline([(RING_U, RING_V)] + [(RING_U + 40 * math.cos(math.radians(a)), RING_V + 40 * math.sin(math.radians(a)))
                                        for a in (65, 90, 115)]).close().extrude(PLATE_T + 2).val())
sh = sh.cut(slot.intersect(ring_w(RING_U, RING_V, RING_ID / 2 + 0.3, RING_OD / 2 - 0.3, floor_w0 - 1, floor_w1 + 2)))
# the vibration board screws flat to the underside of the flange (rigid, so the ticks reach the
# knob): two M2 screws from below into bosses on top of the flange
vib_cu, vib_cv = RING_U + VIB_OFF, RING_V
vib_holes = [(vib_cu + VIB_HOLE_DY, vib_cv + sy * VIB_HOLE_DX) for sy in (-1, 1)]
for hu, hv in vib_holes:
    sh = sh.fuse(cyl_w(hu, hv, 3.0, floor_w0 - 4, floor_w0 + 0.01)).cut(cyl_w(hu, hv, SCREW_PILOT / 2, floor_w0 - 4.1, floor_w1 + 0.1))
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
                  cyl_w(RING_U, RING_V, KNOB_BORE / 2, plate_w1 - ENC_SHAFT_L, plate_w1 - 7))  # (as pressed into the knob)
enc_pcb = cyl_w(RING_U, RING_V, 13.0, plate_w1 + 6.5, plate_w1 + 8.1)
enc_pins = union(*[box(RING_U - 5.08 + 2.54 * i - 0.32, RING_U - 5.08 + 2.54 * i + 0.32, plate_w1 + 8.1,
                       plate_w1 + 16.6, RING_V + 10.86, RING_V + 11.5) for i in range(5)])
knob_w0 = -KNOB_LIFT


def make_knob():
    """The knob, standing on its base at the origin: a 20 mm cylinder with a rounded top edge,
    staggered thimble dimples round the side and a plain top."""
    R = KNOB_D / 2
    k = cq.Workplane("XY").circle(R).extrude(KNOB_H).faces(">Z").edges().fillet(2.0) \
        .faces("<Z").edges().chamfer(0.4).val()
    dimples = []
    rows = np.arange(2.6, KNOB_H - 2.4, 1.9)
    per_row = 28
    for j, z in enumerate(rows):
        for i in range(per_row):
            a = 2 * math.pi * (i + 0.5 * (j % 2)) / per_row
            dimples.append(cq.Solid.makeSphere(0.95, V((R + 0.35) * math.cos(a), (R + 0.35) * math.sin(a), z)))
    k = k.cut(cq.Compound.makeCompound(dimples))
    k = k.cut(cq.Solid.makeCylinder(KNOB_BORE / 2, KNOB_BORE_DEPTH, V(0, 0, -0.01)))
    k = k.cut(cq.Solid.makeCone(KNOB_BORE / 2 + 0.5, KNOB_BORE / 2, 0.5, V(0, 0, -0.01)))   # lead-in
    return k.clean()


knob_part = make_knob()
# into place: base 0.5 mm above the deck, top up (the local w axis points into the box)
knob = knob_part.rotate(V(0, 0, 0), V(1, 0, 0), 90).translate(V(RING_U, knob_w0, RING_V))

# vibration module (board parallel to the deck, under the shroud, motor underneath)
vib_w0 = floor_w1
vib_pcb = box(vib_cu - VIB_H / 2, vib_cu + VIB_H / 2, vib_w0, vib_w0 + VIB_T, vib_cv - VIB_W / 2, vib_cv + VIB_W / 2)
for hu, hv in vib_holes:
    vib_pcb = vib_pcb.cut(cyl_w(hu, hv, 1.5, vib_w0 - 1, vib_w0 + 3))
vib_motor = cyl_w(vib_cu - 3, vib_cv, VIB_MOTOR_D / 2, vib_w0 + VIB_T, vib_w0 + VIB_T + VIB_MOTOR_T)
vib_pins = union(*[box(vib_cu + VIB_H / 2 - 1, vib_cu + VIB_H / 2 + 6, vib_w0 + VIB_T, vib_w0 + VIB_T + 0.64,
                       vib_cv + k * 2.54 - 0.32, vib_cv + k * 2.54 + 0.32) for k in (-1, 0, 1)])

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
                  box(ESP_X - 4, ESP_X + 1, esp_y0 + 4, esp_y0 + 6, ESP_PCB_Z + ESP_T, ESP_PCB_Z + ESP_T + 1.2))

# ============================================================== the assembly

asm = cq.Assembly(name="MessageBox")
asm.add(body, name="body", color=GREEN)
asm.add(base, name="base", color=GREEN)
asm.add(D(shroud), name="light_shroud", color=BLACK)
asm.add(D(diffuser), name="diffuser", color=WHITE)
asm.add(D(knob), name="knob", color=BLACK)
asm.add(D(led_ring_pcb), name="led_ring_pcb", color=PCB_BLACK)
asm.add(D(leds), name="led_ring_leds", color=cq.Color(0.95, 0.95, 0.92))
asm.add(D(enc_body), name="encoder_body", color=METAL)
asm.add(D(enc_metal), name="encoder_shaft_nut", color=METAL)
asm.add(D(enc_pcb), name="encoder_pcb", color=PCB_BLUE)
asm.add(D(enc_pins), name="encoder_pins", color=GOLD)
asm.add(D(vib_pcb), name="vibration_pcb", color=PCB_BLACK)
asm.add(D(vib_motor), name="vibration_motor", color=METAL)
asm.add(D(vib_pins), name="vibration_pins", color=GOLD)
asm.add(F(panel), name="epaper_glass", color=GLASS)
asm.add(F(active), name="epaper_active_area", color=EINK)
asm.add(F(cq.Compound.makeCompound(epd_pcb)), name="epaper_module_pcb", color=PCB_BLACK)
asm.add(F(cq.Compound.makeCompound(epd_parts)), name="epaper_module_parts", color=METAL)
asm.add(carrier, name="screen_carrier", color=cq.Color(0.25, 0.25, 0.27))
asm.add(esp_pcb, name="esp32c3_pcb", color=PCB_BLACK)
asm.add(esp_usb, name="esp32c3_usb_c", color=METAL)
asm.add(esp_parts, name="esp32c3_parts", color=cq.Color(0.3, 0.3, 0.3))
asm.add(tie, name="zip_tie", color=cq.Color(0.92, 0.92, 0.88))

out = os.path.join(HERE, "MessageBox-enclosure.step")
asm.export(out)
print("wrote", out, "(%.0f x %.0f x %.0f mm)" % (W, DEPTH, TOTAL_H))

os.makedirs(os.path.join(HERE, "print"), exist_ok=True)
# printed parts, each in a good printing orientation
def stl(shape, name, tol=0.05, ang=0.1):
    """Exports a printed part. OpenCascade can leave hairline cracks where three rounded edges
    meet; if it does, the mesh is welded (points within 0.01, then 0.1 mm) so it's watertight."""
    path = os.path.join(HERE, "print", name)
    cq.exporters.export(cq.Workplane().add(shape), path, tolerance=tol, angularTolerance=ang)
    try:
        import trimesh
    except ImportError:
        return
    m = trimesh.load(path)
    for digits in (2, 1):
        if m.is_watertight:
            break
        m.merge_vertices(digits_vertex=digits)
        m.update_faces(m.nondegenerate_faces())
        m.update_faces(m.unique_faces())
        m.remove_unreferenced_vertices()
        trimesh.repair.fill_holes(m)
        trimesh.repair.fix_normals(m)
    if abs(m.volume - shape.Volume()) > 0.01 * shape.Volume():
        raise SystemExit(f"{name}: mesh repair changed the volume, check it")
    m.export(path)
stl(body, "body.stl")                                   # upright, open side down
stl(knob_part.rotate(V(0, 0, 0), V(1, 0, 0), 180).translate(V(0, 0, KNOB_H)), "knob.stl", 0.02, 0.2)  # top down
stl(base, "base.stl")
shroud_flat = shroud.rotate(V(0, 0, 0), V(1, 0, 0), -90)   # flange on the bed: no supports needed
bb = shroud_flat.BoundingBox()
stl(shroud_flat.translate(V(-bb.xmin, -bb.ymin, -bb.zmin)), "light_shroud.stl", 0.02)
stl(diffuser.rotate(V(0, 0, 0), V(1, 0, 0), 90), "diffuser.stl", 0.02)
# the carrier: back of the plate on the bed (bosses up)
_c = carrier.rotate(V(0, 0, 0), V(1, 0, 0), SCREEN_TILT).rotate(V(0, 0, 0), V(1, 0, 0), -90)
_b = _c.BoundingBox()
stl(_c.translate(V(-_b.xmin, -_b.ymin, -_b.zmin)), "screen_carrier.stl")
print("wrote print/*.stl")

# for previews and checks
if os.environ.get("MB_GLTF"):
    asm.export(os.environ["MB_GLTF"])
    print("wrote", os.environ["MB_GLTF"])
