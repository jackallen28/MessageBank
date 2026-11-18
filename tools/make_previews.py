#!/usr/bin/env python3
"""Builds ../screens.png from the .pgm files that preview_screens writes."""
import glob, os, sys
from PIL import Image, ImageDraw

src = sys.argv[1] if len(sys.argv) > 1 else "."
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "screens.png")
LABELS = {"1_envelope": "New messages", "2_read_top": "Reading", "3_read_end": "Scrolled to the end",
          "4_read_short": "A short note", "5_decide": "Press: keep or delete?", "6_decide_delete": "Turned anticlockwise",
          "7_decide_save": "Turned clockwise", "8_happy": "All read"}
portrait = sorted(f for f in glob.glob(os.path.join(src, "*.pgm")) if "landscape" not in f)
pad, lab = 18, 26
W = len(portrait) * (240 + pad) + pad
sheet = Image.new("RGB", (W, 416 + 2 * pad + lab), (255, 255, 255))
d = ImageDraw.Draw(sheet)
for i, f in enumerate(portrait):
    im = Image.open(f).convert("RGB")
    x = pad + i * (240 + pad)
    sheet.paste(im, (x, pad + lab))
    d.rectangle([x - 1, pad + lab - 1, x + 240, pad + lab + 416], outline=(150, 150, 150))
    d.text((x, pad + 4), LABELS.get(os.path.basename(f)[:-4], ""), fill=(40, 40, 40))
sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
sheet.save(out)
print("wrote", os.path.normpath(out))
