#!/usr/bin/env python3
"""Draws ../enclosure/ring-section.png: a cut through the middle of the knob, front to back,
showing the diffuser, the air gap and the LED ring. Run after make_enclosure.py's settings."""
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.path import Path
from matplotlib.patches import PathPatch

src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_enclosure.py")).read()
src = src[:src.index("out = os.path.join(HERE")]
exec(src)

parts = {}
for child in asm.children:
    parts[child.name] = (child.obj.val() if hasattr(child.obj, "val") else child.obj, child.color)


def section(shape, x):
    """Faces of shape cut by the plane X = x, as matplotlib paths in (Y, Z)."""
    cut = cq.Workplane("YZ").add(shape).section(x)
    paths = []
    for f in cut.faces().vals():
        verts, codes = [], []
        for wire in [f.outerWire()] + f.innerWires():
            n = max(8, int(wire.Length() / 0.15))
            pts = [(p.y, p.z) for p in wire.positions(list(np.linspace(0, 1, n, endpoint=False)))]
            verts += pts + [pts[0]]
            codes += [Path.MOVETO] + [Path.LINETO] * (len(pts) - 1) + [Path.CLOSEPOLY]
        paths.append(Path(verts, codes))
    return paths


fig, ax = plt.subplots(figsize=(11, 6.2), dpi=200)
show = ["body", "base", "light_shroud", "diffuser", "led_ring_pcb", "led_ring_leds", "knob",
        "encoder_body", "encoder_shaft_nut", "encoder_pcb", "vibration_pcb", "vibration_motor"]
for name in show:
    shape, col = parts[name]
    r, g, b, _ = col.toTuple()
    for p in section(shape, RING_U + 0.01 if name != "led_ring_leds" else RING_U + 0.01):
        ax.add_patch(PathPatch(p, facecolor=(r, g, b), edgecolor=(0.15, 0.15, 0.15), lw=0.4))
# the light: two cones from the LEDs to the diffuser
z_led = DECK_H - w_led_front
for sgn in (-1, 1):   # the light fills the black channel up to the diffuser
    y0 = RING_V + sgn * r_led
    inner, outer = RING_V + sgn * HUB_R, RING_V + sgn * CHAN_R
    ax.fill([y0 - 2.5, y0 + 2.5, max(inner, outer), min(inner, outer)],
            [z_led, z_led, DECK_H - DIFF_T, DECK_H - DIFF_T], color=(1.0, 0.2, 0.45), alpha=0.22, lw=0)
ax.set_xlim(RING_V - 45, RING_V + 45)
ax.set_ylim(DECK_H - 32, DECK_H + 20)
ax.set_aspect("equal")
ax.axis("off")
kw = dict(fontsize=8.5, color="#222")
y_r = RING_V + r_led
ax.annotate("", xy=(y_r + 4.5, DECK_H - DIFF_T), xytext=(y_r + 4.5, z_led),
            arrowprops=dict(arrowstyle="<->", color="#c2255c", lw=1))
ax.text(y_r + 15, (DECK_H + z_led) / 2 + 9, f"{DIFF_GAP:.0f} mm air gap\n(LED tops to diffuser)", va="center",
        bbox=dict(facecolor="white", edgecolor="none", pad=1), **kw)
ax.annotate(f"white diffuser, {DIFF_T} mm", xy=(y_r, DECK_H - DIFF_T / 2), xytext=(y_r + 4, DECK_H + 7),
            arrowprops=dict(arrowstyle="-", color="#555", lw=0.7), **kw)
ax.annotate("black shroud: its walls stop the light\nreaching the green body", xy=(RING_V - SLEEVE_R_OUT + 1, DECK_H - 6),
            xytext=(RING_V - 44, DECK_H + 12), arrowprops=dict(arrowstyle="-", color="#555", lw=0.7), **kw)
ax.annotate(f"WS2812B ring (12 LEDs, {2 * math.pi * r_led / 12:.1f} mm apart)", xy=(RING_V + r_led, z_led - 1), xytext=(RING_V + 8, DECK_H + 17),
            arrowprops=dict(arrowstyle="-", color="#555", lw=0.7), **kw)
ax.annotate("encoder", xy=(RING_V + 5, DECK_H - plate_w1 - 4), xytext=(RING_V + 14, DECK_H - 24),
            arrowprops=dict(arrowstyle="-", color="#555", lw=0.7), **kw)
ax.annotate("encoder board", xy=(vib_cv + 3, DECK_H - vib_w0 - 3), xytext=(RING_V - 37.5, DECK_H - 26.5),
            arrowprops=dict(arrowstyle="-", color="#555", lw=0.7), **kw)
ax.set_title("Cut through the knob, front (left) to back (right)", fontsize=10, color="#222")
out = os.path.join(HERE, "ring-section.png")
fig.savefig(out, bbox_inches="tight", facecolor="white")
print("wrote", out)
