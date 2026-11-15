#!/usr/bin/env bash
# Regenerates the README screenshots in docs/img from the UI self-test, at the device's 1024x768 per screen.
# Needs a PC build (cmake -S . -B build && cmake --build build) and python3 with Pillow.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
shots="$(mktemp -d)"; state="$(mktemp -d)"; trap 'rm -rf "$shots" "$state"' EXIT
SDL_VIDEODRIVER=dummy KT_SOFTWARE=1 KT_WINSIZE=1024x768 \
  "$root/build/banana_kana" --selftest --shots "$shots" --state-dir "$state" --data "$root/app/data" | tail -1
python3 "$root/tools/make_screenshots.py" "$shots" "$root/docs/img"
