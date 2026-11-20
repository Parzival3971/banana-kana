#!/usr/bin/env python3
"""Renders the Banana Kana icon to launcher/media/box/Banana Kana.png (512x512) and docs/img/icon.png (280x280).

The icon is drawn by the app itself (App::appTile: Kitsu with her suzu bell and big tail on a cream tile, with an
orange あ badge), so the Ports shelf icon always matches the tile on the Welcome and Credits screens.
`banana_kana --icon DIR` (and the self-test) render it twice, on black and on white, at 4x; the pair gives each
pixel's exact alpha and colour, so the rounded corners and the fox's edges come out clean on any background.

Needs Pillow and a built app (build/banana_kana).
Usage: python3 tools/make_icon.py [shot_dir]   (with a shot_dir: use icon_black.bmp / icon_white.bmp already there)
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "launcher", "media", "box", "Banana Kana.png")
README_ICON = os.path.join(ROOT, "docs", "img", "icon.png")
TILE = (90, 10, 460)  # x, y, size of the tile in the 640x480 logical top screen (App::iconShots)


def render(shots):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", KT_SOFTWARE="1", KT_WINSIZE="2560x1920")  # 4x: smooth after scaling down
    state = tempfile.mkdtemp(prefix="kt-icon-state-")
    subprocess.run([os.path.join(ROOT, "build", "banana_kana"), "--icon", shots, "--state-dir", state],
                   check=True, env=env, stdout=subprocess.DEVNULL)


def main():
    shots = sys.argv[1] if len(sys.argv) > 1 else None
    if not shots:
        shots = tempfile.mkdtemp(prefix="kt-icon-")
        render(shots)
    black = Image.open(os.path.join(shots, "icon_black.bmp")).convert("RGB")
    white = Image.open(os.path.join(shots, "icon_white.bmp")).convert("RGB")
    s = black.width / 640
    x, y, size = TILE
    box = (round(x * s), round(y * s), round((x + size) * s), round((y + size) * s))
    black, white = black.crop(box), white.crop(box)
    out = Image.new("RGBA", black.size)
    bp, wp, op = black.load(), white.load(), out.load()
    for j in range(black.height):
        for i in range(black.width):
            b, w = bp[i, j], wp[i, j]
            a = 255 - max(0, min(255, round(sum(w[k] - b[k] for k in range(3)) / 3)))  # on black: c*a; on white: c*a + 255*(1-a)
            op[i, j] = (0, 0, 0, 0) if a == 0 else tuple(min(255, round(b[k] * 255 / a)) for k in range(3)) + (a,)
    icon = out.resize((512, 512), Image.LANCZOS)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    icon.save(OUT, optimize=True)
    small = icon.copy(); small.thumbnail((280, 280), Image.LANCZOS); small.save(README_ICON, optimize=True)
    print("wrote", OUT, "and", README_ICON)


if __name__ == "__main__":
    main()
