#!/usr/bin/env python3
"""Build every Banana Kana icon variant from the checked-in master artwork.

Outputs the 512px BananaOS box art, the 280px README icon and the 192px BMP used by SDL on Welcome,
unlock and Credits screens. Needs Pillow. Usage: python3 tools/make_icon.py
"""
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
MASTER = ROOT / "assets" / "banana-kana-icon-master.png"


def main():
    source = Image.open(MASTER).convert("RGBA")
    outputs = {
        ROOT / "launcher" / "media" / "box" / "Banana Kana.png": 512,
        ROOT / "docs" / "img" / "icon.png": 280,
    }
    for path, size in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        source.resize((size, size), Image.Resampling.LANCZOS).save(path, optimize=True)
        print("wrote", path)

    bmp = ROOT / "app" / "data" / "brand-icon.bmp"
    source.resize((192, 192), Image.Resampling.LANCZOS).save(bmp)
    print("wrote", bmp)


if __name__ == "__main__":
    main()
