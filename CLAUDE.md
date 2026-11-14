# kanji-trainer

Kanji stroke-order trainer for the Anbernic RG DS Plus running BananaOS.
Top screen: kanji, readings, example. Bottom screen (touch/stylus): trace strokes.

## Git
- Commit straight to `main`.
- NEVER create tags or GitHub releases by hand. Releases come from
  `.github/workflows/release.yml`, triggered by a change to `VERSION` on main
  (`0.0.0` = placeholder, no release). To release: bump `VERSION`, push to main.

## Packaging (BananaOS Ports layout)
- `scripts/package.sh <version>` -> `dist/KanjiTrainer-<version>-aarch64.zip`
- Zip holds `Kanji Trainer.sh` + `KanjiTrainer/`; user unzips into `roms/ports` on the SD card.
- Source: `launcher/Kanji Trainer.sh`, `app/` (not created yet; app/ holds the aarch64 binary + assets).

## Device facts (from tonywei92/banana-player DEVELOPMENT.md)
- Apps run via BananaOS `run-store-app`; HOME=/data/home, SDL_KMSDRM_FIRST = top connector.
- Top panel = DSI-2 connector; bottom panel is a separate SDL window. Bottom touch is
  one-finger evdev, identified by sysfs path `fe5e0000.i2c` (SDL can't tell the two touchscreens apart).
- Buttons via SDL GameController, Nintendo layout (A = east).
- Only the bottom screen is touch. Audio: ALSA default.

## Toolkit
SDL2 + C++ (CMake), two SDL windows (top = DSI-2 connector, bottom = touch), like Banana Player.
Decided over Love2D because Love2D owns a single window. Develop on PC with the two
windows side by side; cross-compile to aarch64 for the release zip.

## Data
Stroke data: KanjiVG (CC BY-SA 3.0) - keep attribution in the app and README.
