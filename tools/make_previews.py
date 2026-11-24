#!/usr/bin/env python3
"""Builds ../screens.png from the .pgm files that preview_screens writes (landscape, as in the case)."""
import os, sys
from PIL import Image, ImageDraw

src = sys.argv[1] if len(sys.argv) > 1 else "."
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "screens.png")
SHOTS = [("a0_arrive", "1. A message arrives..."), ("a2_arrive", "2. ...the hummingbird flies in"),
         ("a3_arrive", "3. ...pecks the seal"), ("a4_arrive", "4. ...and it pops open"),
         ("1_envelope", "Waiting: press to read"), ("2_read_top", "Reading (turn to scroll)"),
         ("d_react", "Turn on past the end: quick replies"), ("e_sent", "Press: sent, in the same thread"),
         ("8_happy", "All read"), ("b_nomsg", "Press with nothing new"),
         ("c_carousel", "Turn: the saved messages"), ("f_menu", "Hold 20 s: settings"), ("g_sleep", "Shut down")]
W, H, pad, lab, cols = 416, 240, 18, 26, 3
rows = (len(SHOTS) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (W + pad) + pad, rows * (H + pad + lab) + pad), (255, 255, 255))
d = ImageDraw.Draw(sheet)
for i, (name, label) in enumerate(SHOTS):
    im = Image.open(os.path.join(src, name + "_landscape.pgm")).convert("RGB")
    x, y = pad + (i % cols) * (W + pad), pad + (i // cols) * (H + pad + lab)
    d.text((x, y + 6), label, fill=(40, 40, 40))
    sheet.paste(im, (x, y + lab))
    d.rectangle([x - 1, y + lab - 1, x + W, y + lab + H], outline=(150, 150, 150))
sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
sheet.save(out, optimize=True)
print("wrote", os.path.normpath(out))
