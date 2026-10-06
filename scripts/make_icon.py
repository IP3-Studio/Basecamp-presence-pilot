#!/usr/bin/env python3
"""Generate icons/presence.png: a 256x256 badge with three rotating presence
rings and a dot, drawn with Pillow (pip install pillow)."""

from pathlib import Path

from PIL import Image, ImageDraw

N = 256
S = 4  # supersampling
W = N * S

BADGE = (26, 30, 40, 255)
RING = (201, 169, 106, 255)
RING_FAINT = (201, 169, 106, 110)
DOT = (95, 169, 126, 255)

img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
r = 40 * S
d.rounded_rectangle([0, 0, W - 1, W - 1], radius=r, fill=BADGE)

cx = cy = W // 2
for i, (rad, col, width) in enumerate([(92, RING, 14), (62, RING_FAINT, 12), (34, RING, 12)]):
    rr = rad * S
    # broken rings: three arcs with gaps, rotated per ring so they read as "rotating"
    start = 20 + i * 37
    for k in range(3):
        a0 = start + k * 120
        d.arc([cx - rr, cy - rr, cx + rr, cy + rr], a0, a0 + 88, fill=col, width=width * S)
dr = 13 * S
d.ellipse([cx - dr, cy - dr, cx + dr, cy + dr], fill=DOT)

out = img.resize((N, N), Image.LANCZOS)
dest = Path(__file__).resolve().parent.parent / "icons" / "presence.png"
dest.parent.mkdir(parents=True, exist_ok=True)
out.save(dest)
print(f"wrote {dest} {out.size}")
