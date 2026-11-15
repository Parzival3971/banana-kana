#!/usr/bin/env python3
"""Turns self-test screenshots (top/bottom BMPs) into README images in docs/img.

Each image shows the two screens stacked like the RG DS Plus clamshell. Run via scripts/screenshots.sh.
Usage: make_screenshots.py <shot-dir> <out-dir>   (needs Pillow)
"""
import os, sys
from PIL import Image, ImageDraw

SHOTS = {  # output name -> self-test shot
    "home": "home_reviews", "lesson": "lesson_traced", "read": "q_read_checked", "write": "q_write_checked",
    "write-combo": "q_write_combo_part2", "type-kana": "q_type_kana_typed", "meaning": "q_meaning_word",
    "mixup": "q_mixup_checked", "timed": "timed", "result": "result", "review": "q_review",
    "stats": "stats", "chart": "chart", "stages": "stages", "unlocked": "unlocked", "settings": "settings_practice",
    "light-home": "light_home", "light-stats": "light_stats", "welcome": "welcome", "practice-set": "chart_set",
    "free-write": "free_write", "skip": "settings_skip", "guided": "q_write", "credits": "credits",
    "draw-any": "draw_any_checked", "extras": "lesson_extras_fa", "match": "q_match", "packs": "packs", "words": "q_pack_recall", "handwriting": "lesson_handwriting_grid",
}
WIDTH = 480  # README width of each device image
BODY, HINGE = (27, 26, 23), (16, 15, 13)

def rounded(im, r):
    mask = Image.new("L", im.size, 0); ImageDraw.Draw(mask).rounded_rectangle([0, 0, im.width - 1, im.height - 1], r, fill=255)
    out = Image.new("RGBA", im.size); out.paste(im, (0, 0), mask); return out

def device(top, bot):
    pad, gap = 36, 44
    w = top.width + 2 * pad; h = top.height + bot.height + 2 * pad + gap
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w - 1, top.height + pad + gap // 2], 48, fill=BODY)
    d.rounded_rectangle([0, top.height + pad + gap // 2, w - 1, h - 1], 48, fill=BODY)
    d.rectangle([40, top.height + pad + gap // 2 - 4, w - 40, top.height + pad + gap // 2 + 4], fill=HINGE)
    im.alpha_composite(rounded(top.convert("RGB"), 14), (pad, pad))
    im.alpha_composite(rounded(bot.convert("RGB"), 14), (pad, pad + top.height + gap))
    return im

def main():
    src, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
    for name, shot in SHOTS.items():
        t, b = os.path.join(src, shot + "_top.bmp"), os.path.join(src, shot + "_bot.bmp")
        if not (os.path.exists(t) and os.path.exists(b)): print("missing", shot); continue
        im = device(Image.open(t), Image.open(b))
        im = im.resize((WIDTH, round(im.height * WIDTH / im.width)), Image.LANCZOS)
        im.save(os.path.join(out, name + ".png"), optimize=True)
    icon = Image.open(os.path.join(os.path.dirname(__file__), "..", "launcher", "media", "box", "Banana Kana.png"))
    icon.thumbnail((280, 280), Image.LANCZOS); icon.save(os.path.join(out, "icon.png"), optimize=True)  # README icon: no space in its path
    print("wrote", len(SHOTS), "images and icon.png to", out)

if __name__ == "__main__": main()
