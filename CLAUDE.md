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
- Source: `launcher/Kanji Trainer.sh`, `app/data/` (generated assets), aarch64 binary from `scripts/docker-build-aarch64.sh`.
- Device build: arm64 Debian bookworm container (QEMU in CI). BananaOS ships SDL2, SDL2_ttf, FreeType and ALSA, so we link
  them dynamically and bundle nothing but libstdc++/libgcc (static). `dist/aarch64/REQUIREMENTS.txt` records the glibc/SDL
  versions the binary needs - compare with the device before releasing.
- `.github/workflows/build.yml` builds, tests, self-tests and package-dry-runs on every push (no release).
- Launcher writes `~/.config/kanji-trainer/launch.log` (env + display info) - ask for it when debugging on device.

## Device facts (from tonywei92/banana-player DEVELOPMENT.md)
- Apps run via BananaOS `run-store-app`; HOME=/data/home, SDL_KMSDRM_FIRST = top connector.
- Top panel = DSI-2 connector; bottom panel is a separate SDL window. Bottom touch is
  one-finger evdev, identified by sysfs path `fe5e0000.i2c` (SDL can't tell the two touchscreens apart).
- Buttons via SDL GameController, Nintendo layout (A = east).
- Only the bottom screen is touch. Audio unused for now.

## Toolkit
SDL2 + C++ (CMake), two SDL windows (top = DSI-2 connector, bottom = touch), like Banana Player.
Decided over Love2D because Love2D owns a single window. Develop on PC with the two
windows side by side; cross-compile to aarch64 for the release zip.

## Data
Stroke data: KanjiVG (CC BY-SA 3.0) - keep attribution in the app and README.

## Build / test (PC)
- Deps: `libsdl2-dev libsdl2-ttf-dev cmake g++`; asset generation needs `python3 fonttools`.
- `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure` (core logic tests)
- UI self-test (scripted clicks through every drill type, dummy displays):
  `SDL_VIDEODRIVER=dummy KT_SOFTWARE=1 build/kanji_trainer --selftest --shots /tmp/shots --state-dir /tmp/kt`
- Regenerate assets: `python3 tools/gen_data.py` (kana.tsv, words.tsv, KanjiVG strokes.txt, subset font).

## Code map
- `src/core.{h,cpp}`: data loading, progress/weak-spot weighting, question generation, stroke scoring (no SDL).
- `src/main.cpp`: two SDL windows, screens (Home/Settings/Drill/Result), input (mouse=touch, gamepad, keyboard), `--selftest`.
- Drill types: ReadKana, RecallKana, Meaning, Write (stylus trace vs KanjiVG), Type (on-screen romaji keys).
- No audio / Listening drill: removed on purpose (no clips available). Re-add only if real recordings exist.

## Known gaps / to verify on device
- Dual-screen window placement (uses 2 displays if SDL reports them) and bottom touch routing (evdev `fe5e0000.i2c`) untested.
- Yoon kana (きゃ etc.) have no Write drill (two glyphs). Typing has no Japanese-keyboard mode (romaji only).
- Kana count is 104 per script (Maru shows 105).
