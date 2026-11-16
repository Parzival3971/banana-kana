# banana-kana

Banana Kana: hiragana/katakana drills (reading, word meanings, typing) and stylus tracing for the Anbernic RG DS Plus
running BananaOS. Top screen: prompt and feedback. Bottom screen (touch/stylus): answer tiles, keyboard, tracing canvas.
(Renamed from kanji-trainer; GitHub repo is Parzival3971/banana-kana.)

## Git
- Small fixes: commit straight to `main`. Big changes (e.g. the 0.5.0 Handheld redesign, user agreed 2026-11-15): a branch +
  PR into main; the PR's CI builds the aarch64 zip (build.yml artifact) for a device test before merging.
- NEVER create tags or GitHub releases by hand. Releases come from
  `.github/workflows/release.yml`, triggered by a change to `VERSION` on main
  (`0.0.0` = placeholder, no release). To release: add a `## <version>` section to CHANGELOG.md (release notes come
  from it via scripts/release-notes.sh; the workflow fails without it), bump `VERSION`, push to main.

## Packaging (BananaOS Ports layout)
- `scripts/package.sh <version>` -> `dist/BananaKana-<version>-aarch64.zip`
- Zip holds `Banana Kana.sh` + `BananaKana/` (binary, data, VERSION, LICENSE, CHANGELOG) + `media/box/Banana Kana.png`; user unzips into `roms/ports`.
- Source: `launcher/Banana Kana.sh`, `app/data/` (generated assets), aarch64 binary from `scripts/docker-build-aarch64.sh`.
- Device build: arm64 Debian bookworm container (QEMU in CI). BananaOS ships SDL2, SDL2_ttf, FreeType and ALSA, so we link
  them dynamically and bundle nothing but libstdc++/libgcc (static). `dist/aarch64/REQUIREMENTS.txt` records the glibc/SDL
  versions the binary needs - compare with the device before releasing.
- `.github/workflows/build.yml` on every push: `pc` job (native build, ctest, self-test) + `aarch64` job (QEMU build, tests, package dry run). No release.
- Launcher writes `~/.config/banana-kana/launch.log` (env + display info) - ask for it when debugging on device.

## Device facts (from tonywei92/banana-player DEVELOPMENT.md)
- Apps run via BananaOS `run-store-app`; HOME=/data/home, SDL_KMSDRM_FIRST = top connector.
- Top panel = DSI-2 connector; bottom panel is a separate SDL window. Bottom touch is
  one-finger evdev, identified by sysfs path `fe5e0000.i2c` (SDL can't tell the two touchscreens apart).
- Buttons via SDL GameController. Device labels are Nintendo (A east, B south, X north, Y west). SDL mapping a:b0(SOUTH)
  b:b1(EAST) x:b2(NORTH) y:b3(WEST): main.cpp swaps A<->B only (confirmed on device). X/Y are NOT swapped: the old X<->Y
  swap made labelled X act as Y (user: "X doesn't check when typing", 2026-11-15). KT_POSITIONAL=1 / KT_SWAP_XY=1.
- Only the bottom screen is touch. Audio: synthesised sound effects only (no voice clips).

## Toolkit
SDL2 + C++ (CMake), two SDL windows (top = DSI-2 connector, bottom = touch), like Banana Player.
Decided over Love2D because Love2D owns a single window. Develop on PC with the two
windows side by side; cross-compile to aarch64 for the release zip.

## Data
Stroke data: KanjiVG (CC BY-SA 3.0) - keep attribution in the app and README.
README has an AI disclosure section (user asked 2026-11-15): keep it accurate when the process changes.
README uses emojis on headings, feature bullets and table rows (user asked 2026-11-15): keep the style when editing.

## Build / test (PC)
- Deps: `libsdl2-dev libsdl2-ttf-dev cmake g++`; asset generation needs `python3 fonttools`.
- `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure` (core logic tests)
- UI self-test (scripted clicks through every drill type, dummy displays):
  `SDL_VIDEODRIVER=dummy KT_SOFTWARE=1 build/banana_kana --selftest --shots /tmp/shots --state-dir /tmp/kt`
- Regenerate assets: `python3 tools/gen_data.py` (kana.tsv, words.tsv, KanjiVG strokes.txt, kana/ui/fallback fonts, licences).
  kana.ttf covers the whole hiragana/katakana blocks (kana keyboard can type small vowels, ゔ etc.).
- README screenshots: `scripts/screenshots.sh` (self-test at KT_WINSIZE=1024x768 -> tools/make_screenshots.py -> docs/img).
- Repo docs: README.md (user + dev), LICENSE (MIT code; data notes), CHANGELOG.md, .github/ISSUE_TEMPLATE/bug_report.md.

## Code map
- `src/core.{h,cpp}`: data loading, stages (Data::stages, stageOfKana), progress v2 (steps, session cap, unlocked/lessons per
  script, checkUnlocks, v1 migration), question generation (unlocked kana only, current stage weighted, review setting,
  words only when every char is unlocked and >= 6 such words; 4 options when the pool is small), stroke scoring.
  Also (2026-11-15): spaced review (Stat.srs/due, SRS_DAYS 1/3/7/14/30/90, isDue/dueKana, makeReviewQuestion; known kana
  without srs get scheduled 1-3 days out on load), mix-ups (MIX_DEFS look-alike sets + Progress::confusions "h:a|h:b",
  mixupSets/makeMixupQuestion), daily goal/streak (Settings.dailyGoal, today/todayCount/streak/bestStreak/lastGoalDay,
  goalJustMet, days history 90 days), timedBest[2] + makeTimedQuestion, kana keyboard (Settings.kanaKeys, Question.kanaInput,
  kanaTypedMatches accepts じ/ぢ for "ji"), Data::writeParts (combos traced as 2 glyphs, small kana use the full-size glyph).
  Progress.clock = fake time for tests (0 = real). Progress file: `stat ... step gainSession gain srs due wlv wlvSession`, `day`, `mix`.
  Staging (user asked 2026-11-15, "writing straight away is hard"): typeAllowed(): step 0 choice only, romaji typing from
  TYPE_STEP 1, kana keyboard + writing from PRODUCE_STEP 2. Writing level Stat.wlv 0 full guide / 1 faint outline /
  2 memory, +1 per session on a right guided write; guided writes (recordWrite level<2) never move steps; a memory miss
  drops wlv to 1. skipTo(script, stage|SKIP_ALL): earlier stages known with srs 1 due in 1-2 days (user chose "known,
  early review"); only moves forwards.
  Kana Draw comparison (2026-11-15, user said "do all of them"): STAGE_COUNT 15 = Extras (hiragana: っ; katakana:
  ッ ー + 13 foreign-sound kana from EXTRA_K). Kana.special (っ ッ ー): ReadKana becomes a word question with near-miss
  romaji (mark ignored / misplaced), never typed, never a distractor sound. 105 hiragana / 119 katakana: use
  Data::scriptCount, never 104. writeParts traces every small kana full size. Settings: strict (scoreStrokeAt 0/1/2),
  kanaHand (kana_hand.ttf = Klee One, setKanaHand in gfx), guideLines, ink. recognizeKana(): order-free stroke matching
  after fitting to the glyph box (draw-any mode); fitStrokes() for its order check.
- `src/gfx.{h,cpp}`: themes/palettes, fonts + mixed-script text (per run; spaces always UI font), wrapping, rounded shapes
  (rows overlap 1px: avoids seams at non-integer scale), pixel icons, glyph stroke drawing, windows, screenshots.
  Text is rasterised at the panel's real scale (Win::scale, 1.6 on device) for sharpness.
- `src/app.{h,cpp}`: all screens + navigation + input mapping per screen: Home (stage card, 4 cartridges: Practice/Lesson,
  Stages, Kana chart, Settings; mode row Review/Mix-ups/Timed/Stats = homeSel 4-7, U/D switches rows; top stats
  Today/Streak/Reviews due/Next unlock), Mode enum (Normal/Focus/Review/Mixup/Timed; startMode, tick() runs the 60 s clock,
  pause stops it), Stats (14-day chart, accuracy heatmap -> chart), kana keyboard (gojuon grid + dakuten/handakuten/small/long),
  Write parts (part/partStrokes/partOk; Undo/Back returns to part 1), Welcome (first launch when no progress file:
  new / know hiragana / know both / some -> Settings > Script), Settings > Script skip-ahead rows (kind 3: arrows pick,
  A confirms), Chart practice set (chartPick; tap twice/A toggles, X drill = Mode::Focus on focusSet, a single kana is
  mixed with its look-alikes and row-mates; START = FreeWrite), FreeWrite (guide full/outline/none, no progress),
  Credits (animated via tick(), BananaOS/JorreFog first), d-pad cross drawn for the "+" chip key, Show me = stroke-order
  animation (animStart, tick() redraws at ~30 fps; auto in lessons and guided writes; START), arrows on full guides,
  Draw any (FreeWrite with empty fwList; chart "Draw any"), Chart Extras tab, stage map 8 per row, Stages (14-tile map, replay lessons), Lesson (hint, stroke-order frames, practice trace),
  Unlocked, Drill (choice/typing/writing + feedback banner with hint), Pause, Result, Chart (Basic/Voiced/Combos tabs,
  scroll, tap twice / A = 5-question drill of one kana), Settings, Credits.
- Settings copy BananaOS's Settings (user asked, 2026-11-15; reference shot: BananaLinux docs/img/readme/shot-settings.png):
  6 group tiles on the bottom (Practice, Sessions, Script, Display, Progress, About; selected = yellow; tap selected / A opens),
  top panel = icon + group + "N settings" + list. Group = outlined rows "label  < value >" (tap arrows, d-pad L/R, A = next),
  top panel shows row name, value (yellow) and help. Footer pills "B Close/Back", "A Open/Change". L/R = other group.
  Progress > Reset progress needs two presses, keeps settings. About > Version reads BananaKana/VERSION.
- `src/selftest.cpp`: `--selftest` drives every screen and flow (lesson -> sessions -> unlock -> lesson, wrong answer, pause,
  map, chart focus drill, settings, light theme, katakana unlock); `--shots DIR` saves top/bottom BMPs of each screen.
- `src/main.cpp`: start-up, event loop, gamepad label swap, F/SELECT pause + F+START quit, evdev touch, backlight keeper.
- Drill types: ReadKana, RecallKana, Meaning, Write (stylus trace vs KanjiVG), Type (on-screen romaji keys).
- No Listening drill: removed on purpose (no voice clips available). Re-add only if real recordings exist.
- `src/sound.{h,cpp}`: synthesised sound effects (snd::play), Settings.sound 0-3.

## Known gaps / to verify on device
- Device test 2026-11-15 passed (old UI): touch + tracing land correctly, A/B correct, F + START quits, bottom survives sleep.
- New UI (M3/M4) not yet seen on device: check text sharpness at 1.6x, readability at F_MICRO (17px logical),
  L/R shoulder buttons (Stages script switch, Chart tabs) and that GUIDE really is the F key.
- Kana count: 105 hiragana (matches Maru), 119 katakana (with Extras).

## Device findings (first on-device test, 2026-11-14)
- BananaOS: Linux 7.2.7-banana aarch64, Buildroot glibc 2.41, SDL 2.32.10 with KMSDRM, SDL_KMSDRM_FIRST=DSI-2.
- SDL reports 2 displays, both 1024x768 (display 1 at x=1024). Top window + buttons worked; bottom panel was blank on the first build.
- Suspects for blank bottom: bottom backlight off (`/run/banana/backlight-bottom` names the device, `bl_power` 0=on/4=off, `/run/banana/panel-off`
  keeps it dark) and non-fullscreen window (KMSDRM needs fullscreen). Launcher now logs panel state and sets bl_power=0.
- Bottom panel fixed by the launcher turning the bottom backlight on (bl_power=0) + fullscreen windows.
- Touch: SDL touch did nothing on device. Now read via evdev (device whose sysfs path has fe5e0000.i2c), SDL finger events
  as fallback; touch-synthesised mouse events are ignored. Env: KT_TOUCH=sdl, KT_TOUCH_DEV, KT_TOUCH_SWAPXY/FLIPX/FLIPY.
  Launch log prints input devices, the chosen device, axis ranges and the first raw->logical touches.
- Env switches: `KT_NOFS=1` (borderless instead of fullscreen), `KT_VSYNC=1` (vsync per window), `KT_SOFTWARE=1`,
  `KT_WINSIZE=WxH` (PC window size, e.g. 1024x768 for device-scale screenshots).

## Device test 2 (new UI, 2026-11-15)
- Bottom screen showed half-drawn frames mixed with the previous screen during tracing: stroke drawing used dozens of
  1-px lines per segment (thousands of draw calls/frame). Fixed: strokes are triangles (SDL_RenderGeometry), and the main
  loop only redraws on input/touch or every 500 ms. Log also shows "ERROR: Could not restore CRTC" x2 (SDL KMSDRM) - unknown.
- "F+START doesn't quit" was user error (it works). F+SELECT also quits (BananaOS system hotkey -> SIGTERM -> we save).
  Chips: F+START Quit, F Pause. Log now includes joysticks, controller mapping and raw SDL_JOYBUTTONDOWN indices
  (SDL_GetError had said "Unexpected controller element crc"; buttons work, so probably harmless).

## Device test 3 (2026-11-15)
- F alone DOES pause in drills (it is GUIDE). User wants: F/SELECT pause also in lessons + free writing (togglePause works on
  every screen but Home/Welcome); F + START mid-practice must NOT quit (App::inPractice() -> opens pause instead; still
  quits from menus); F + SELECT is BananaOS's. Two menu keys together = combo. "F Pause" chip is tappable (B_PAUSE).
- Back key (right of F) = evdev BTN_Z (BananaLinux bananad names it "back"); the unmapped-SDL-button guess did NOT work.
  main.cpp EvBack reads BTN_Z from every /dev/input device that has it (non-grabbing) -> padButton B. KT_BACK_CODE=<evdev
  code> / KT_BACK_BUTTON=<raw SDL index> override; SDLK_AC_BACK = B. Log line "back key: code N on /dev/input/eventX".
- Confirmed from launch.log (2026-11-15 15:05): input devices = event0 rk805 pwrkey, event1 Goodix touch, event2
  adc-keys-back (ONLY key: BTN_Z 309 = the Back key; separate from the gamepad, so SDL never sees it), event3 headphone
  jack switch, event4 gpio-keys-hall (lid), event5 gpio-keys-volume (SDL also sends VolumeUp/Down keys), event6
  retrogame_joypad (js0; has no BTN_Z). /dev/input/* are root-only crw------- and we run as root (touch evdev works).
  SDL mapping: a:b0 b:b1 x:b2 y:b3 back(SELECT):b8 start:b9 guide(F):b10 l/r:b4/b5 l2/r2:b6/b7 l3/r3:b11/b12 dpad:b13-16.
- Volume / F+volume brightness are done by BananaOS's bananad even while apps run (it grabs nothing). It writes
  "volume 45" / "brightness 60" to /run/banana/osd (rename) for banana-ui's level card; main.cpp OsdWatch polls its mtime
  every 100 ms and App::showOsd draws our own card on the top screen (1.8 s).
- Confetti: App::celebrate(ms, both) on newly known kana (top), stage unlocked / daily goal / timed best (both screens).
  tick() runs every timer each call (an early return once froze the timed clock during confetti).
- Sticks: src/stickpad.h (tested in test_core) turns the analogue stick into d-pad presses (RG DS Plus has ONE stick; the code handles a second if SDL reports one). v0.1.1 jumped around
  (user 2026-11-15): now ON 20000 + 1.5x dominant axis, held axis only until under OFF 11000, re-arm only after
  120 ms at centre (kills spring-back), rest point calibrated, repeat 400/140 ms. Phantom right stick: SDL mapping has rightx:a2/righty:a3 which move WITH the real stick in other directions
  (launch.log 2026-11-15) -> right stick ignored by default (KT_STICKS=both re-enables). KT_STICKS=0 disables; launch.log
  logs the first 30 stick presses with raw values. Triggers ignored.
- 0.2.0 (user picked 2026-11-15 from my suggestions): Question.match = RecallKana with the twin from the other script as
  prompt (twinKana: same romaji + same stage, both unlocked; not ー / foreign-sound extras), a third of normal recall
  questions once both scripts are open. Rumble in sound.cpp (snd::rumble per Sfx, Settings.rumble 0/1/2, joypad has
  FF_RUMBLE; log "rumble: yes/no"). 0.2.0 right-answer buzz (45 ms, 0x2000 strong/0x6000 weak) was never felt on device
  (user: only wrong answers rumble) -> 0.3.1: same value on both channels, floor 0x5800, >= 80 ms, and PATTERNS (user asked how to tell right from
  wrong): snd::rumblePattern in sound.h (right 1 tick, wrong 2 buzzes, known 3 ticks, unlock/goal 1 long), later
  pulses played by snd::tick() from the main loop; first 8 pulses logged. Gap lengths still to be judged on device. Backup: App::backup -> BananaKana/progress-backup.txt (packaged layout only, i.e.
  data/../VERSION exists; KT_BACKUP=0 off, KT_BACKUP_PATH override), restored in main.cpp when progress.txt is missing.
  Settings rows squeeze when a group has 6+ rows.
- Word packs (user asked 2026-11-15; audio deferred, see below): tools/gen_data.py PACKS (12 packs, lists written by us,
  not copied) -> packs.tsv (id, jp title, name, desc) + words.tsv column 4 = pack (a word lives in one pack; WORDS rows
  keep their line and just join). Generator asserts every pack word only uses kana of its script. core: Pack, wordReady,
  readyWords, packInfo, PACK_MIN 4, makePackQuestion (Meaning / RecallKana with English prompt / ReadKana / Type; no
  option shares the answer's meaning). app: Screen::Packs (3x4 tiles, tap twice or A to start), Mode::Pack (packIdx),
  Home mode row = 5 tiles drawn Review, Mix-ups, Words, Timed, Stats (HOME_MODE_IDS; Words = B_MODE0+4; homeSel 4-8).
  Words reaching step 4 get "Known word!" + confetti. Result screen shows pack stats, B = back to Packs.
- Word intros + browse (user, 2026-11-15: "asking what a word means but it hasn't taught the word"): a word is
  "introduced" when it has a stat entry (Progress::introduce/introduced). makePackQuestion(..., PackSession&) shows
  Question.intro cards (max PACK_NEW_PER_SESSION 5, only while ps.left > fresh+2), quizzes each fresh word ~2 questions
  later, and only tests introduced words; intro cards don't use up a question (nextQuestion skips index++).
  makeWordQuestion turns Meaning into ReadKana when no eligible word is introduced. Screen::PackWords (15 a page,
  B_WORD0/B_WPREV/B_WNEXT; Words button or Y on Packs).
- Home redesign (user, 2026-11-15): sticker colours per cartridge (STICKER[]), MODE_ICONS pixel icons indexed by
  B_MODE0 id, selected mode tile = yellow fill, goal ring (gfx drawRing), streak FLAME, bilingual greeting by local hour
  + hinomaru drawn next to it (user asked for "a japanese flag or something").
- Word SRS + achievements (user picked from my list 2026-11-15): record() schedules w: keys like kana; dueWords/dueCount;
  makeReviewQuestion mixes due words (wordQuiz, shared with packs). Achievements: achievementList() (17), Progress::
  achieved id->day ("ach" lines), checkAchievements (silent at start-up in main.cpp; after answers, sessions and on
  Home -> App::earned toast queue + confetti + Goal sfx), award("perfect") in finishSession; Screen::Awards from Stats
  (X / tappable chip). B_HEAT0 range check matters: higher button ids live above it.
- Git (user, 2026-11-15 18:02): hold off committing/pushing/releasing until the user gives the go-ahead. 19:30: go-ahead for
  branch `handheld-theme` + PR (no merge, no release until the device test).
- Audio: user asked for licensed recordings 2026-11-15. Candidates: tofugu/japanese-vocabulary-pronunciation-audio
  (CC BY-SA 4.0, on GitHub), Lingua Libre (CC BY-SA 4.0), Kanji alive (CC BY 4.0, kanji words). None covers single kana.
  User said hold off on audio.
- On-screen keyboards (kana + romaji) have a d-pad cursor (keyRow/keyCol, kbMove skips gaps), A types, X checks, B deletes.
- Sound effects (src/sound.cpp): synthesised at start-up (Tap, Right, Wrong, Known, Unlock, Goal, Tick), SDL queue audio,
  Settings > Display > Sound (off/quiet/normal/loud). Not yet heard on device.

## Banana Player lessons applied (pre-emptive, 2026-11-14)
- Quit anywhere: F + START (confirmed working on device; SELECT + START also quits).
- Pause menu: F (or SELECT) pressed and released alone during practice -> Resume / Back to menu / Quit. Tab on PC.
- Touch buttons activate on release, slide off to cancel (stylus misfires).
- Progress saved after every answer; SIGTERM/SIGINT/SIGHUP exit cleanly with a save (lid/power-off safety).
- App re-enables the bottom backlight every 2 s if bl_power != 0 (BananaOS panel-off can re-darken it after sleep).
- Ports shelf box art: `media/box/Banana Kana.png` (512x512, size unverified) shipped in the zip.
- README install notes include the Mac "zip opens into its own folder" gotcha.

## Handheld theme (0.5.0, user chose it 2026-11-15; mockup canvas: https://claude.ai/artifact/BJVYoG1euyAfWGPgbpsoi1)
- Replaces the BananaOS look (user: "looks so much cleaner and more professional"). One design, light (default) + dark
  palettes in gfx.cpp (LIGHT_THEME/DARK_THEME + BG2, CARD_SH, PILL_*, PAPER_*). ACCENT = mode colour via setAccent(MC_*),
  chosen per screen/mode at the top of App::render (orange lessons/practice, blue review, green words, purple mix-ups,
  red timed, teal stats/settings). Primitives: gradRound, card (hard 5px drop shadow), pill (gloss gradient, outline,
  shadow; kind 0 plain/1 accent/2 disabled), drawBackdrop (stripes top, dots bottom, cached texture), lineIcon (24-grid
  polylines drawn with drawStrokeIn; NO pixel icons: user said they clash), drawRing. ui_bold.ttf (Zen Maru Gothic Bold)
  for F_HUGE..F_SMALL and setBold(). drawButtons draws PANEL/ACCENT buttons as pills, custom fills stay flat.
  header(title, right, coloured, indent): bar with bilingual title. Home top: goal ring card, week streak strip (weekday
  kanji added to fallback.ttf), script bars; Home bottom: see Home grid below.
  Settings rows: SRow.opts -> segmented pills (B_SSEG0 + row*8 + opt). Medals: App::medal; app tile: App::appTile.
  Icon: tools/make_icon.py (option A "kana tile") -> launcher/media/box/Banana Kana.png.
- Review polish (user said "do all", 2026-11-15): streak flame = gfx drawFlame + app flameTier/streakFlame (1/7/30/100
  tiers; pale ghost until today's goal is met; flickers on Home via tick flameLast; week dots use the tier colour).
  Motion (App::motion; KT_MOTION=0 / self-test off): go() fade-in (transStart, TRANS_MS), Home glide (homePos eases to
  homeSel in tick), answer pop/shake (answerAnim), result count-up (countStart) + medal on a perfect session.
  Feedback banner grows with wrappedHeight(). Selection rings = ACC_LINE (no black outlines). Header right side is
  context only (no wordmark). stageLabel(): Extras. Guide/Hint = switch (Button.icon 3 off / 4 on).
  settingPreview() for Display rows. Settings > Progress > Reset settings (resetSetArmed, two presses, keeps script +
  progress). Self-test shots hide toasts/confetti except celebration shots.
- Device test 4 (PR #1 build, 2026-11-15 20:16): (1) top backdrop showed stale/black patches: the render-target
  texture cache doesn't survive on KMSDRM/GLES -> drawBackdrop now draws cached SDL_RenderGeometry (no render targets
  anywhere; keep it that way). (2) Home motion sluggish -> glide is time-based (GLIDE_MS 170 from glideFrom to glideTo,
  independent of frame rate), fade 120 ms; main.cpp logs "render: X ms average, Y ms worst" to launch.log. (3) Choice
  questions highlighted a tile at the start (could give the answer away) -> cursor = -1 until the first d-pad/stick
  press, A does nothing before that (timed too). (4) Settings text looked squashed: drawText now shrinks evenly when
  even F_MICRO is too wide (KT_TEXTLOG=1 lists such text); group summaries shortened. (5) Stylus swipe on the Home strip:
  homeTouch/homeDrag in pointer(), strip follows the stylus (140 px per tile), settles on the nearest, a flick > 40 px moves one.
  (6) Home lost its "F+START Quit" hint in the redesign (chips() silently dropped what didn't fit): restored; chips()
  now tightens spacing when a row is too wide and KT_TEXTLOG=1 logs any dropped chip.
- Home grid (0.6.0, user picked mockup "B" and said drop the slider, 2026-11-16): bottom = B_HOME_GO pill (452x84) +
  script switch, then homeItems() as 4x2 tiles (modes solid colour, Stages/Chart/Settings white with a coloured icon).
  homeSel = focus index from homeFocus() (0 pill, 1 script, 2-9 tiles); d-pad moves in 4 directions, no wrap; up from the
  left 3 columns -> pill, from the right column -> script switch. Tapping a tile opens it. The slider, its glide and the
  stylus swipe were removed (git history has them: 0.5.0).
- Seasons (0.6.0, user asked 2026-11-16 for a "four seasons of Japan" background with gentle motion): Settings.backdrop
  0 plain (default) / 1 seasons by calendar month (Mar-May spring, Jun-Aug summer, Sep-Nov autumn, Dec-Feb winter) / 2-5
  fixed; "set backdrop". gfx setSeason() tints BG/BG2 (after applyTheme; render calls setSeason(seasonNow()) every
  frame), drawBackdrop draws the season's cached pattern (sakura blossoms, seigaiha arcs, momiji stars, snow dots),
  drawSeasonParticles(t) draws 12 top / 8 bottom drifting particles as a pure function of time (fireflies rise).
  Particles only on menuScreen() (not Drill/Lesson/FreeWrite/pause); tick redraws every 90 ms there when motion is on.
  Display row 6 "Background" (segments Plain/Auto/春/夏/秋/冬; 春夏秋冬 added to fallback.ttf) + settingPreview row 6.
- Settings groups with more than 5 rows scroll (user: rows looked squished on device, 2026-11-16): rows keep PITCH 66,
  5 visible, window follows setRow; ▲ / ▼ footer buttons (B_SUP/B_SDOWN) + "n/N" between Back and Change.
- Old v1 look (kept for history): BananaOS palette (banana yellow #FFD23F on warm neutrals), button-hint chips, cartridge tiles.
  Fonts: kana in Zen Maru Gothic Bold (or Klee One SemiBold, setting), English/UI/keyboard in Zen Maru Gothic Medium (all OFL). Pixelify Sans (Banana
  Player's pixel font) was tried and DROPPED 2026-11-15: user found it hard to read. Don't reintroduce pixel fonts.
  Fallback for missing glyphs: Noto Sans CJK JP subset (fallback.ttf) instead of Fusion Pixel (already had it, OFL).
  Source TTFs: github.com/google/fonts (ofl/zenmarugothic). Licences ship in app/data/licenses/.
  Text is rendered per run (main.cpp renderMixed/pickFont): kana -> kana.ttf, Latin -> ui.ttf, else fallback.ttf.
- Theme: Dark and Light (DONE M1: `set theme` in progress.txt, toggle on Settings top row; palettes in main.cpp Theme). On light, yellow is fill only; yellow-on-light text needs a darker shade.
- Icon: "Cartridge" design, rendered to launcher/media/box/Banana Kana.png (512x512; shelf size still unverified).
- Stages: 14 per script (10 basic rows, が-ざ-だ, ば-ぱ, 2 yōon). Katakana unlocks after hiragana stage 10.
  Each stage starts with a lesson (stroke order + hint + untimed tracing). Drills only use unlocked kana;
  word questions only use words made entirely of unlocked kana; older stages return as weighted review.
- Pass rule (CONFIRMED by user, DONE M2 in core): each kana has 4 steps. Right = +1 step, at most 2 steps per session,
  wrong = -1 step (not a reset). Known = step 4. Stage unlocks when every kana in it is known.
  Rationale: WaniKani needs 4 correct reviews spread over time to "Guru"; Nihongo Master uses a 5-correct streak
  that resets on a miss. Spreading over 2+ sessions tests memory, not short-term recall.
- Lesson hints: written originally for Banana Kana (app/data/hints.tsv, generated by tools/gen_data.py HINTS_H/HINTS_K;
  voiced, p- and combo kana hints are generated from their base kana). Wikibooks text was NOT copied, so no CC BY-SA
  on hints. Credits screen still needs KanjiVG (CC BY-SA 3.0) + font OFL notices.
- Screens: Home, Stage map, Lesson, Stage unlocked, Drill (reading/meanings/writing/typing), Feedback, Pause menu,
  Kana chart (locked/learning/known/weak), Practice mix/settings (incl. theme), Results.
- Post-M4 features (2026-11-15, user asked "do 1-7"): spaced review, mix-ups, daily goal + streak, combo writing,
  timed challenge, kana keyboard, stats screen. Not yet seen on device.
- RELEASED 0.5.0 on 2026-11-15 (Handheld redesign, PR #1 merged after device test 4; user said "release").
- RELEASED 0.1.0 on 2026-11-15 (repo made public the same day), 0.1.1 the same day (meanings, X/Y fix, sticks). Milestones: 1 fonts+theme DONE, 2 stages+progress migration DONE, 3 mockup screens/lessons/unlock DONE,
  4 chart + settings DONE (2026-11-15), 5 device test (NEEDS USER), then first release 0.1.0 via VERSION bump.
