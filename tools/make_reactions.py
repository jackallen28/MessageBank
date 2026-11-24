#!/usr/bin/env python3
"""Makes ../MessageBox/reactions.h: the quick-reply reactions.

The pictures are the hand-drawn symbols from the Jack-in-a-Box project (app/icons.py, drawn with
its own sketch-pen renderer), so they look the same on the box as on Jack-in-a-Box printouts.
Each reaction gets a 1-bit bitmap for the e-paper and a small PNG that goes in the reply email.

    python3 tools/make_reactions.py /path/to/jack_in_a_box
"""
import io, os, sys
from PIL import Image

JIAB = sys.argv[1] if len(sys.argv) > 1 else "../jack_in_a_box"
sys.path.insert(0, JIAB)
from app.icons import draw_icon  # noqa: E402

# (Jack-in-a-Box symbol, label on the box, words in the email)
REACTIONS = [
    ("heart", "Love it", "Love it!"),
    ("fun", "Haha!", "Haha!"),
    ("star", "Amazing", "Amazing!"),
    ("exclamation", "Wow!", "Wow!"),
    ("tick", "Got it", "Got it."),
    ("question", "Tell me more", "Tell me more?"),
]
SCREEN_PX, EMAIL_PX = 56, 128

out = ["// Made by tools/make_reactions.py from the Jack-in-a-Box hand-drawn symbols. Don't edit.",
       "#pragma once", "#include <Arduino.h>", "",
       f"const int REACTION_COUNT = {len(REACTIONS)};", f"const int REACTION_PX = {SCREEN_PX};", ""]
names = []
for i, (icon, label, words) in enumerate(REACTIONS):
    big = draw_icon(icon, 320)                      # draw large, then shrink: smoother strokes
    small = big.resize((SCREEN_PX, SCREEN_PX), Image.LANCZOS).point(lambda p: 0 if p < 150 else 255)
    bits = []
    for y in range(SCREEN_PX):
        for xb in range(0, SCREEN_PX, 8):
            b = 0
            for k in range(8):
                if small.getpixel((xb + k, y)) == 0:
                    b |= 0x80 >> k
            bits.append(b)
    out.append(f"static const uint8_t REACT_BMP_{i}[] PROGMEM = {{" + ",".join(f"0x{b:02x}" for b in bits) + "};")
    png = big.resize((EMAIL_PX, EMAIL_PX), Image.LANCZOS).point(lambda p: 0 if p < 150 else 255).convert("1")
    buf = io.BytesIO()
    png.save(buf, "PNG", optimize=True)
    data = buf.getvalue()
    out.append(f"static const uint8_t REACT_PNG_{i}[] PROGMEM = {{" + ",".join(str(b) for b in data) + "};")
    names.append((icon, label, words, len(data)))
out.append("")
out.append("struct Reaction { const char *icon, *label, *words; const uint8_t *bitmap, *png; uint16_t pngLen; };")
out.append("static const Reaction REACTIONS[REACTION_COUNT] = {")
for i, (icon, label, words, n) in enumerate(names):
    out.append(f'  {{ "{icon}", "{label}", "{words}", REACT_BMP_{i}, REACT_PNG_{i}, {n} }},')
out.append("};")
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "MessageBox", "reactions.h")
open(dst, "w").write("\n".join(out) + "\n")
print("wrote", os.path.normpath(dst), [n[3] for n in names], "PNG bytes")
