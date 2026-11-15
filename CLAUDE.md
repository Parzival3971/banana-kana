# banana-kana

Banana Kana: hiragana/katakana drills (reading, word meanings, typing) and stylus tracing for the Anbernic RG DS Plus
running BananaOS. Top screen: prompt and feedback. Bottom screen (touch/stylus): answer tiles, keyboard, tracing canvas.
(Renamed from kanji-trainer; GitHub repo is Parzival3971/banana-kana.)

## Git
- Commit straight to `main`.
- NEVER create tags or GitHub releases by hand. Releases come from
  `.github/workflows/release.yml`, triggered by a change to `VERSION` on main
  (`0.0.0` = placeholder, no release). To release: bump `VERSION`, push to main.

## Packaging (BananaOS Ports layout)
- `scripts/package.sh <version>` -> `dist/BananaKana-<version>-aarch64.zip`
- Zip holds `Banana Kana.sh` + `BananaKana/` + `media/box/Banana Kana.png`; user unzips into `roms/ports` on the SD card.
- Source: `launcher/Banana Kana.sh`, `app/data/` (generated assets), aarch64 binary from `scripts/docker-build-aarch64.sh`.
- Device build: arm64 Debian bookworm container (QEMU in CI). BananaOS ships SDL2, SDL2_ttf, FreeType and ALSA, so we link
  them dynamically and bundle nothing but libstdc++/libgcc (static). `dist/aarch64/REQUIREMENTS.txt` records the glibc/SDL
  versions the binary needs - compare with the device before releasing.
- `.github/workflows/build.yml` builds, tests, self-tests and package-dry-runs on every push (no release).
- Launcher writes `~/.config/banana-kana/launch.log` (env + display info) - ask for it when debugging on device.

## Device facts (from tonywei92/banana-player DEVELOPMENT.md)
- Apps run via BananaOS `run-store-app`; HOME=/data/home, SDL_KMSDRM_FIRST = top connector.
- Top panel = DSI-2 connector; bottom panel is a separate SDL window. Bottom touch is
  one-finger evdev, identified by sysfs path `fe5e0000.i2c` (SDL can't tell the two touchscreens apart).
- Buttons via SDL GameController, which reports by POSITION (SDL A = south). Device labels are Nintendo (A = east), so
  main.cpp swaps A<->B and X<->Y before handling (confirmed on device: unswapped A/B were backwards). KT_POSITIONAL=1 disables.
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
  `SDL_VIDEODRIVER=dummy KT_SOFTWARE=1 build/banana_kana --selftest --shots /tmp/shots --state-dir /tmp/kt`
- Regenerate assets: `python3 tools/gen_data.py` (kana.tsv, words.tsv, KanjiVG strokes.txt, subset font).

## Code map
- `src/core.{h,cpp}`: data loading, progress/weak-spot weighting, question generation, stroke scoring (no SDL).
- `src/main.cpp`: two SDL windows, screens (Home/Settings/Drill/Result), input (mouse=touch, gamepad, keyboard), `--selftest`.
- Drill types: ReadKana, RecallKana, Meaning, Write (stylus trace vs KanjiVG), Type (on-screen romaji keys).
- No audio / Listening drill: removed on purpose (no clips available). Re-add only if real recordings exist.

## Known gaps / to verify on device
- Device test 2026-11-15 passed: touch + tracing land correctly, A/B correct, F + START quits, bottom screen survives sleep.
- Yoon kana (きゃ etc.) have no Write drill (two glyphs). Typing has no Japanese-keyboard mode (romaji only).
- Kana count is 104 per script (Maru shows 105).

## Device findings (first on-device test, 2026-11-14)
- BananaOS: Linux 7.2.7-banana aarch64, Buildroot glibc 2.41, SDL 2.32.10 with KMSDRM, SDL_KMSDRM_FIRST=DSI-2.
- SDL reports 2 displays, both 1024x768 (display 1 at x=1024). Top window + buttons worked; bottom panel was blank on the first build.
- Suspects for blank bottom: bottom backlight off (`/run/banana/backlight-bottom` names the device, `bl_power` 0=on/4=off, `/run/banana/panel-off`
  keeps it dark) and non-fullscreen window (KMSDRM needs fullscreen). Launcher now logs panel state and sets bl_power=0.
- Bottom panel fixed by the launcher turning the bottom backlight on (bl_power=0) + fullscreen windows.
- Touch: SDL touch did nothing on device. Now read via evdev (device whose sysfs path has fe5e0000.i2c), SDL finger events
  as fallback; touch-synthesised mouse events are ignored. Env: KT_TOUCH=sdl, KT_TOUCH_DEV, KT_TOUCH_SWAPXY/FLIPX/FLIPY.
  Launch log prints input devices, the chosen device, axis ranges and the first raw->logical touches.
- Env switches: `KT_NOFS=1` (borderless instead of fullscreen), `KT_VSYNC=1` (vsync per window), `KT_SOFTWARE=1`.

## Banana Player lessons applied (pre-emptive, 2026-11-14)
- Quit anywhere: F + START (confirmed working on device; SELECT + START also quits).
- Pause menu: F (or SELECT) pressed and released alone during practice -> Resume / Back to menu / Quit. Tab on PC.
- Touch buttons activate on release, slide off to cancel (stylus misfires).
- Progress saved after every answer; SIGTERM/SIGINT/SIGHUP exit cleanly with a save (lid/power-off safety).
- App re-enables the bottom backlight every 2 s if bl_power != 0 (BananaOS panel-off can re-darken it after sleep).
- Ports shelf box art: `media/box/Banana Kana.png` (512x512, size unverified) shipped in the zip.
- README install notes include the Mac "zip opens into its own folder" gotcha.
