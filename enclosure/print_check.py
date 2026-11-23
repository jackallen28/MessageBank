#!/usr/bin/env python3
"""Print-readiness check for the MessageBox enclosure. Run after make_enclosure.py:

    python3 print_check.py

Checks: solids are valid; no parts overlap; wire clearances (35 mm under the encoder's mounting
face, 20 mm over the ESP32-C3, room behind the e-paper header); fits and tolerances; wall
thicknesses; STL files are watertight and fit the bed; and where each printed part, in its
print orientation, has overhangs that need supports.
"""
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "make_enclosure.py")).read()
src = src[:src.index("out = os.path.join(HERE")]
exec(src)

BED = (220, 220, 250)        # a common printer (Ender 3 / Prusa MK-size); change for yours
problems = []


def say(ok, text):
    print(("  ok    " if ok else "  FIX   ") + text)
    if not ok:
        problems.append(text)


parts = {c.name: (c.obj.val() if hasattr(c.obj, "val") else c.obj) for c in asm.children}

# ------------------------------------------------------------------ 1. solids and overlaps
print("1. Solids and overlaps")
for name in ("body", "base", "light_shroud", "diffuser"):
    sh = parts[name]
    say(sh.isValid() and len(sh.Solids()) == 1, f"{name}: valid, {len(sh.Solids())} solid, {sh.Volume() / 1000:.1f} cm3")
names = list(parts)
same = lambda a, b: a.split("_")[0] == b.split("_")[0] and a.startswith(("epaper", "esp32c3", "encoder", "led", "vibration", "knob"))
bad = []
for i, a in enumerate(names):
    for b in names[i + 1:]:
        if same(a, b):
            continue
        try:
            v = parts[a].intersect(parts[b]).Volume()
        except Exception:
            v = 0
        if v > 0.05:
            bad.append(f"{a} / {b} {v:.2f} mm3")
say(not bad, "no parts overlap" + ("" if not bad else ": " + "; ".join(bad)))

# ------------------------------------------------------------------ 2. wire clearances
print("2. Wire clearances")


def clear(volume, allowed, label):
    hits = []
    for n, sh in parts.items():
        if any(n.startswith(p) for p in allowed):
            continue
        try:
            v = sh.intersect(volume).Volume()
        except Exception:
            v = 0
        if v > 0.05:
            hits.append(f"{n} ({v:.1f} mm3)")
    say(not hits, label + ("" if not hits else " - blocked by " + ", ".join(hits)))


free_under = (DECK_H - BASE_T) - plate_w1
say(free_under >= 35.0, f"encoder: {free_under:.1f} mm from its mounting face down to the base (needs 35)")
clear(D(cyl_w(RING_U, RING_V, 13.0, plate_w1 + 8.1, plate_w1 + 35.0)), ("encoder",),
      "encoder: the 26 mm column under its board, to 35 mm below the mounting face, is empty")
esp_top = ESP_PCB_Z + ESP_T
clear(box(ex0, ex1, esp_y0, esp_y1, esp_top + 0.1, esp_top + 20), ("esp32c3", "zip_tie"),
      "ESP32-C3: 20 mm clear above the whole board")
hdr = max(epd_parts, key=lambda s: s.BoundingBox().ylen)       # the 2 x 4 header (tallest part)
hb = hdr.BoundingBox()
clear(F(box(hb.xmin - 2, hb.xmax + 2, hb.ymax, hb.ymax + 25, hb.zmin - 2, hb.zmax + 2)), ("epaper",),
      "e-paper header: 25 mm clear behind it for the plugs or wires")
wedge = (cq.Workplane("XZ", origin=(0, floor_w0 + RING_WIRES, 0))
         .polyline([(RING_U, RING_V)] + [(RING_U + 40 * math.cos(math.radians(a)), RING_V + 40 * math.sin(math.radians(a)))
                                         for a in (68, 90, 112)]).close().extrude(RING_WIRES - 0.05).val())
clear(D(wedge.intersect(ring_w(RING_U, RING_V, RING_SLOT_R0 + 0.2, RING_OD / 2 - 0.5, floor_w0, floor_w0 + RING_WIRES))),
      ("led",), f"LED ring: its pins drop through the 50 degree slot with {RING_WIRES:.0f} mm clear below the ring")
clear(D(box(vib_cu + VIB_H / 2 + 0.1, vib_cu + VIB_H / 2 + VIB_WIRES, vib_w0 - 1, vib_w0 + 6, vib_cv - 6, vib_cv + 6)),
      ("vibration",), f"vibration board: {VIB_WIRES:.0f} mm clear off its wire edge")

# ------------------------------------------------------------------ 3. fits and tolerances
print("3. Fits (gap per side)")
fits = [
    ("diffuser in the black bezel (outer)", FIT_SLIDE, 0.25, 0.5),
    ("diffuser rests on the channel wall's ledge", DIFF_R_OUT - CHAN_R, 0.7, 9),
    ("diffuser rests on the centre column", HUB_R - DIFF_R_IN, 0.7, 9),
    ("snap tab hook: interference as drawn (the tab bends this far)", 10.5 - 10.3, 0.15, 0.35),
    ("snap tab: room to bend inward in its pocket", 9.3 - 8.75, 0.4, 2),
    ("snap tab: side clearance in its pocket", math.radians((SNAP_POCKET_SPAN - SNAP_TAB_SPAN) / 2) * 9.3, 0.25, 0.6),
    ("snap hook in its undercut (radial / vertical play)", min(10.65 - 10.5, 5.9 - 5.8), 0.1, 0.3),
    ("shroud in the deck opening", FIT_SLIDE, 0.25, 0.5),
    ("screen carrier in its rails (sides, behind, top stop)", FIT_RAIL, 0.3, 0.5),
    ("LED ring (37 mm) inside the channel (outer)", CHAN_R - RING_OD / 2, 0.3, 3),
    ("LED ring (23 mm hole) over the centre column", RING_ID / 2 - HUB_R, 0.3, 3),
    ("encoder body (13 x 13) in its square pocket as printed", (ENC_POCKET - HOLE_COMP - ENC_SQ) / 2, 0.2, 0.6),
    ("base inside the body (plus a 0.5 mm chamfer underneath)", FIT_BASE, 0.3, 0.6),
    ("knob above the deck (room to press it)", KNOB_LIFT, 2, 4),
    ("knob hole: shaft stops 0.1 mm short of its end", KNOB_BORE_DEPTH - (ENC_SHAFT_L - plate_w1 - KNOB_LIFT), 0.05, 0.5),
    ("knob bore as printed on the 6 mm knurled shaft (press fit, negative = interference)", (KNOB_BORE - HOLE_COMP - 6.0) / 2, -0.1, 0.0),
    ("diffuser's inner edge hidden under the knob", KNOB_D / 2 - DIFF_R_IN, 0.5, 9),
    ("knob covers the nut pocket", KNOB_D / 2 - NUT_POCKET_R, 1, 9),
    ("encoder bushing (7 mm) in its hole as printed", 3.6 - 3.5, 0.05, 0.3),
    ("self-tapper pilot holes as printed", SCREW_PILOT - HOLE_COMP, 2.5, 2.5),
    ("10 mm socket (14 mm across) in the nut pocket", NUT_POCKET_R - 7.0, 0.3, 3),
    ("USB-C plug body (13 x 8 mm) in the rear pocket, width", (USB_POCKET_W - 13.0) / 2, 0.3, 2),
    ("USB-C plug body (13 x 8 mm) in the rear pocket, height", (USB_POCKET_H - 8.0) / 2, 0.3, 2),
    ("USB-C plug's metal end (8.4 x 2.6) in the through-hole, width", (USB_CUT_W - 8.4) / 2, 0.3, 2),
    ("USB-C plug's metal end (8.4 x 2.6) in the through-hole, height", (USB_CUT_H - 2.6) / 2, 0.3, 2),
    ("ESP32-C3 between its stop and the rear wall (play)", 0.3, 0.2, 1),
    ("zip tie (2 x 1 mm) in its slots, width", (2.8 - TIE_W) / 2, 0.3, 1),
    ("zip tie in its slots, thickness", (1.8 - TIE_T) / 2, 0.3, 1),
]
for label, gap, lo, hi in fits:
    say(lo <= gap <= hi, f"{label}: {gap:.2f} mm")
strain = 100 * 1.5 * 0.8 * 0.3 / (5.9 - DIFF_T) ** 2   # cantilever: 1.5 t d / L^2
say(strain <= 1.5, f"diffuser snap tabs bend at {strain:.1f} % strain (PLA/PETG are fine below 1.5 %)")
say((WALL - 1.0) >= 1.4, f"screen screw-head pockets leave {WALL - 1.0:.1f} mm of face wall")
say(CAR_W0 - pcb_w1 - 3.6 >= 0.5, f"screen carrier: {CAR_W0 - pcb_w1 - 3.6:.1f} mm clear of the board's tallest part (3.6 mm)")
recess = (WALL - USB_POCKET_D) + 0.3
say(recess <= 1.6, f"USB-C socket sits {recess:.1f} mm behind the bottom of the pocket (a plug reaches up to ~2)")

# ------------------------------------------------------------------ 4. wall thicknesses
print("4. Thinnest walls")
for label, t, lo in [
    ("body wall under the texture grooves", WALL - TEX_DEPTH, 1.6),
    ("body wall at the USB-C pocket", WALL - USB_POCKET_D, 0.8),
    ("base under the zip-tie groove", BASE_T - 1.2, 1.2),
    ("base under a foot recess", BASE_T - 1.0, 1.2),
    ("shroud channel wall / bezel", SLEEVE_R_OUT - CHAN_R, 1.6),
    ("centre column at the encoder pocket's corners", HUB_R - ENC_POCKET / 2 ** 0.5, 1.2),
    ("shroud channel floor / flange", PLATE_T, 1.6),
    ("encoder plate under the tab groove", PLATE_T - 1.0, 0.8),
    ("diffuser (two 0.2 mm layers)", DIFF_T, 0.4),
    ("centre column between a snap pocket and the channel", HUB_R - 10.65, 0.4),
    ("centre column between a snap pocket and the nut pocket", 8.75 - NUT_POCKET_R, 0.8),
]:
    say(t >= lo - 1e-6, f"{label}: {t:.1f} mm (min {lo})")

# ------------------------------------------------------------------ 5. STL files
print("4b. Tool access and fitting order")
# Each screw needs a straight path for the screwdriver (6 mm shaft, 10 mm handle beyond 40 mm)
# out through the open bottom, with only the parts fitted at that point in place.
fitted_before_base = [n for n in parts if not n.startswith(("base", "esp32c3", "zip_tie"))]


def tool_path(x, y, z_top, label, allowed, present):
    shaft = cyl_z(x, y, 3.0, -60, z_top - 0.05)
    hits = []
    for n in present:
        if any(n.startswith(p) for p in allowed):
            continue
        try:
            v = parts[n].intersect(shaft).Volume()
        except Exception:
            v = 0
        if v > 0.05:
            hits.append(n)
    say(not hits, label + ("" if not hits else " - blocked by " + ", ".join(hits)))


for a in SCREW_ANGLES:
    du, dv = polar(SCREW_R, a)
    gx, gy = RING_U + du, RING_V + dv                           # deck frame: v is y from the front
    tool_path(gx, gy, DECK_H - floor_w1, f"shroud screw at {a} deg: straight up from the open bottom", (), [n for n in fitted_before_base if n != "knob"])
tool_path(LOCK_X, LOCK_Y, LOCK_Z - 4, "screen carrier's locking screw: straight up from the open bottom", ("screen_carrier",), fitted_before_base)
say(True, "screen's 4 screws and the vibration board's 2: fitted on the bench, before those parts go in")
say(True, "base screws: from underneath, countersunk")
nut = D(cyl_w(RING_U, RING_V, 7.0, -80, plate_w0 - 2))          # a 10 mm socket from above, knob off
hits = [n for n in parts if n not in ("knob", "encoder_shaft_nut") and parts[n].intersect(nut).Volume() > 0.05]
say(not hits, "encoder nut: a 10 mm socket reaches it from above (knob off)" + ("" if not hits else " - blocked by " + ", ".join(hits)))
# sliding the carrier in: at every point on its 8 mm slide up the slope, it must not hit anything
others = [n for n in fitted_before_base if not n.startswith(("screen_carrier", "epaper"))]
slide_dir = V(0, S, C)                                           # up the slope
blocked = []
for d in (1, 2, 3, 4, 5, 6, 7, 8, 9):
    moved = [parts[n].translate(slide_dir * -d) for n in ("screen_carrier", "epaper_module_pcb", "epaper_module_parts", "epaper_glass")]
    for m in moved:
        for n in others:
            try:
                if m.intersect(parts[n]).Volume() > 0.05:
                    blocked.append(f"{d} mm: {n}")
            except Exception:
                pass
say(not blocked, "screen + carrier slide in: clear at every millimetre of the 8 mm slide" + ("" if not blocked else " - " + ", ".join(sorted(set(blocked)))))

print("5. STL files")


def load_stl(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[80:84])[0]
    if 84 + n * 50 == len(d):
        a = np.frombuffer(d[84:84 + n * 50], dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))
        return a["v"].reshape(-1, 3, 3).astype(float)
    v = [list(map(float, l.split()[1:4])) for l in d.decode().splitlines() if l.strip().startswith("vertex")]
    return np.array(v).reshape(-1, 3, 3)


meshes = {}
for f in sorted(os.listdir(os.path.join(HERE, "print"))):
    if not f.endswith(".stl"):
        continue
    tri = load_stl(os.path.join(HERE, "print", f))
    meshes[f] = tri
    q = np.round(tri.reshape(-1, 3) / 1e-4).astype(np.int64).reshape(-1, 3, 3)
    edges = {}
    for t in q:
        for i in range(3):
            a, b = tuple(t[i]), tuple(t[(i + 1) % 3])
            k = (a, b) if a < b else (b, a)
            edges[k] = edges.get(k, 0) + 1
    open_edges = sum(1 for c in edges.values() if c != 2)
    size = tri.reshape(-1, 3).max(0) - tri.reshape(-1, 3).min(0)
    fits_bed = all(s <= b for s, b in zip(size, BED))
    say(open_edges == 0, f"{f}: watertight ({len(tri)} triangles)" + ("" if open_edges == 0 else f", {open_edges} open edges"))
    say(fits_bed and tri.reshape(-1, 3)[:, 2].min() > -0.01,
        f"{f}: {size[0]:.0f} x {size[1]:.0f} x {size[2]:.0f} mm, flat on the bed, fits {BED[0]} x {BED[1]} x {BED[2]}")

# ------------------------------------------------------------------ 6. overhangs in print orientation
print("6. Overhangs steeper than 45 degrees, as each part is exported (bridges count here too)")
# the body is checked without its texture: the grooves are 0.6 mm steps that print without support
tmp = os.path.join(HERE, "print", "_plain_body.stl")
cq.exporters.export(cq.Workplane().add(body_plain), tmp, tolerance=0.05, angularTolerance=0.1)   # upright, as printed
meshes["body.stl (untextured)"] = load_stl(tmp)
os.remove(tmp)
del meshes["body.stl"]
for f, tri in meshes.items():
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    area = np.linalg.norm(n, axis=1) / 2
    nz = n[:, 2] / np.maximum(np.linalg.norm(n, axis=1), 1e-12)
    zmin = tri[:, :, 2].min(1)
    down = (nz < -0.7072) & (zmin > 0.25)          # faces pointing down more than 45 deg, off the bed
    a = area[down].sum()
    if a < 1:
        say(True, f"{f}: no supports needed")
        continue
    # group the overhanging faces into regions by their centres (2 mm grid), report the biggest
    c = tri[down].mean(1)
    cells = {}
    for p, ar in zip(c, area[down]):
        k = tuple(np.floor(p / 6).astype(int))
        cells[k] = cells.get(k, 0) + ar
    print(f"  note  {f}: {a:.0f} mm2 facing down. The biggest areas, at height z above the bed:")
    zs = {}
    for p, ar in zip(c, area[down]):
        k = round(p[2] / 3) * 3
        zs[k] = zs.get(k, 0) + ar
    for z, ar in sorted(zs.items(), key=lambda t: -t[1])[:6]:
        sel = np.abs(c[:, 2] - z) <= 1.5
        lo, hi = c[sel].min(0), c[sel].max(0)
        print(f"          z {z:5.0f}: {ar:6.0f} mm2 over x {lo[0]:.0f}-{hi[0]:.0f}, y {lo[1]:.0f}-{hi[1]:.0f}")

print()
print("All checks passed." if not problems else f"{len(problems)} thing(s) to fix:\n  - " + "\n  - ".join(problems))
