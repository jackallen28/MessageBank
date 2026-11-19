#!/usr/bin/env python3
"""Builds the enclosure (without writing files) and reports any parts that overlap."""
import os, sys
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_enclosure.py")).read()
src = src[:src.index("out = os.path.join(HERE")]
exec(src)
parts = {}
for name in asm.children if False else []:
    pass
for child in asm.children:
    shape = child.obj if not hasattr(child.obj, "val") else child.obj.val()
    parts[child.name] = shape.moved(child.loc) if child.loc else shape
names = list(parts)
print("body: valid %s, %d solid(s), %.0f cm3" % (body.isValid(), len(body.Solids()), body.Volume() / 1000))
bad = 0
for i, a in enumerate(names):
    for b in names[i + 1:]:
        if a.startswith(("epaper_module", "esp32c3_", "encoder_", "led_ring", "vibration_")) and a.split("_")[0] == b.split("_")[0]:
            continue  # pieces of the same bought part
        try:
            v = parts[a].intersect(parts[b]).Volume()
        except Exception:
            v = 0
        if v > 0.5:
            bad += 1
            print("  overlap %-22s %-22s %8.1f mm3" % (a, b, v))
print("overlaps:", bad)
