#!/usr/bin/env python3
"""Renders the Banana Kana icon ("Kana tile", Handheld theme) to launcher/media/box/Banana Kana.png (512x512).

A glossy orange rounded tile with a white あ and a small white ア badge, drawn at 4x and scaled down for smooth edges.
Needs Pillow and Zen Maru Gothic Bold (fetched by tools/gen_data.py into /tmp/gf, or pass a path).
Usage: python3 tools/make_icon.py [path/to/ZenMaruGothic-Bold.ttf]
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "launcher", "media", "box", "Banana Kana.png")
FONT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/gf/ZenMaruGothic-Bold.ttf"
S = 4            # supersampling
N = 512 * S


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def rounded_mask(size, box, radius):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius=radius, fill=255)
    return m


def gradient(size, stops):
    """Vertical gradient image from (position, colour) stops."""
    w, h = size
    img = Image.new("RGB", size)
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                col = lerp(c0, c1, (t - p0) / max(1e-6, p1 - p0))
                break
        for x in range(w):
            px[x, y] = col
    return img


def centred_text(draw, cx, cy, text, font, fill):
    l, t, r, b = draw.textbbox((0, 0), text, font=font)
    draw.text((cx - (l + r) / 2, cy - (t + b) / 2), text, font=font, fill=fill)


def main():
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    m = 26 * S                      # margin
    tile = (m, m, N - m, N - m - 18 * S)
    rad = 110 * S
    # hard drop shadow under the tile (like the app's buttons)
    sh = rounded_mask((N, N), (tile[0], tile[1] + 20 * S, tile[2], tile[3] + 20 * S), rad)
    img.paste(Image.new("RGBA", (N, N), rgb("#b8600f") + (255,)), (0, 0), sh)
    # outline, then the glossy face
    img.paste(Image.new("RGBA", (N, N), rgb("#d9741c") + (255,)), (0, 0), rounded_mask((N, N), tile, rad))
    inset = 8 * S
    face_box = (tile[0] + inset, tile[1] + inset, tile[2] - inset, tile[3] - inset)
    face = gradient((N, N), [(0.0, rgb("#ffb15e")), (0.58, rgb("#f28c28")), (1.0, rgb("#e57f1f"))]).convert("RGBA")
    img.paste(face, (0, 0), rounded_mask((N, N), face_box, rad - inset))
    # soft gloss band across the top
    gloss = rounded_mask((N, N), (face_box[0] + 30 * S, face_box[1] + 18 * S, face_box[2] - 30 * S, face_box[1] + 150 * S), 70 * S)
    img.paste(Image.new("RGBA", (N, N), (255, 255, 255, 255)), (0, 0), gloss.point(lambda v: v * 50 // 255))

    d = ImageDraw.Draw(img)
    big = ImageFont.truetype(FONT, 268 * S)
    cx, cy = (tile[0] + tile[2]) // 2 - 26 * S, (tile[1] + tile[3]) // 2 - 22 * S
    # the あ, with a darker shadow below it
    shadow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    centred_text(ImageDraw.Draw(shadow), cx, cy + 12 * S, "あ", big, rgb("#a8520b") + (150,))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(2 * S)))
    centred_text(d, cx, cy, "あ", big, (255, 255, 255, 255))
    # the ア badge, bottom right
    bw = 122 * S
    bx, by = tile[2] - bw - 30 * S, tile[3] - bw - 30 * S
    d.rounded_rectangle((bx, by + 9 * S, bx + bw, by + bw + 9 * S), radius=38 * S, fill=rgb("#c96a14"))
    d.rounded_rectangle((bx, by, bx + bw, by + bw), radius=38 * S, fill=(255, 255, 255, 255))
    centred_text(d, bx + bw // 2, by + bw // 2, "ア", ImageFont.truetype(FONT, 100 * S), rgb("#e8590c"))

    img = img.resize((512, 512), Image.LANCZOS)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT, optimize=True)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
