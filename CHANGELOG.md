# Changelog

All notable changes to Banana Kana. Releases are built by GitHub Actions when `VERSION` changes on `main`;
the release notes come from the matching section below.

## Unreleased - soft UI redesign test build

- Rebuilt every screen around the approved warm-ivory, deep-plum handheld design while keeping the two-screen
  640×480 layouts, controller focus, touch targets and progress format intact.
- New Kitsu brand icon across the BananaOS shelf, Welcome/unlock/Credits screens and README, with a banana-curved tail
  and kana tile. Home, Stats, feedback, results and Your fox now share one scalable cel-shaded anime Kitsu system,
  including four tail evolutions, three moods, seasonal details and equipped accessories.
- Kitsu accessories use pose- and evolution-specific head, neck, body and ground anchors, so they stay attached in
  the large portrait, compact accessory cards and sleeping poses instead of using the old fox coordinates.
- Credits replaces the old blocky banana parade with smooth, outlined banana badges that match the redesigned palette.
- Home has a visible, focusable Quit button and a Cancel-first confirmation dialog; F + START remains available.
- Seasonal backgrounds still follow the calendar (or a fixed override) and motion still follows the user's setting.
- Home previews the next session's new/review balance and duration; Quick, Standard, Study and Review-only presets
  make session length and intent easier to choose.
- Answer streaks now bring Kitsu into feedback, while results show strengthened kana, daily-goal progress and a direct
  Review mistakes action.
- Kana details now show accuracy, last practice, next review and mix-up counts, backed by a compatible extension to
  existing progress files.
- First-run setup includes an interactive D-pad and A-button introduction.
- Regenerated the complete README screenshot gallery from the redesigned executable.

## 0.8.3 - 2026-11-18

- Flatter buttons: every button now has one smooth shade instead of the shiny two-tone gloss. Outlines, shadows and
  the press feel are unchanged.
- Pause menu buttons have the same rounded corners as the rest of the app (they were the only capsule-shaped ones).
- Draw any: the side buttons are evenly sized, like the Write drill (the "What is it?" button was double height).

## 0.8.2 - 2026-11-18

- RG DS support (untested): Banana Kana now follows BananaOS's own description of the screens, like Banana Player
  does: the top screen's connector (`BANANA_TOP`) and the bottom touchscreen (`BANANA_TOUCH`). On the RG DS Plus
  nothing changes. `launch.log` shows which display it picked as the top screen.

## 0.8.1 - 2026-11-18

- Word questions no longer show the English when it sounds like the answer (すし "sushi", カメラ "camera", ホテル "hotel"...): it gave the reading away.
- The romaji keyboard is bigger (taller, full-width keys with larger letters) and sits at the bottom of the touch screen like the kana keyboard, so it's easier to hit and reach. The d-pad highlight is no longer hidden under the next key.
- Home: Kitsu's "N more answers to wake her" line no longer runs into the weekday strip (it wraps instead).
- Settings › Display: long help text no longer disappears behind the preview card (it shrinks to fit).

## 0.8.0 - 2026-11-17

- Pause › Back to menu keeps your session: Home's big button becomes **Continue · 3 of 10** and picks up at the next
  question with your score so far. Before, leaving threw the session away and starting again asked the same new kana
  from question 1. Starting any other session (or switching script) drops the held one.
- Every button hint along the bottom of the screen can be tapped with the stylus and does what its button does
  (tap **B Back** to go back, **A Open** to open, the left or right half of **L/R** for each shoulder). The d-pad and
  F+START Quit hints stay hints, so a stray tap can't quit.
- Saving now tells `launch.log` if anything goes wrong, and the log shows how much progress was loaded at start-up.

- Steps now need time to stick: you still gain at most 2 steps per kana (or word) in a sitting, but the next ones only
  count when you come back at least 4 hours later. Before, quitting and starting a new session straight away reset
  the limit, so a kana could become "known" in a minute. The feedback tells you when the next step is available.
- Typing accepts Kunrei and keyboard spellings: *si*, *ti*, *tu*, *hu*, *zi*, *sya*/*tya*/*zya*, *nn* for ん and
  *o* for を. Single kana are checked exactly, so *sei* no longer passes for せ (words still forgive long vowels).
- Fixed: lowering the daily goal below what you'd already answered that day meant the goal couldn't be met that day,
  and your streak could break.
- Fixed: the streak could reset just after midnight on the night the clocks go forward.
- Fixed: reviews could ask a guided trace, which never clears a review, so the same kana kept coming back. Reviews
  only ask for writing from memory now.
- Mix-ups with っ / ッ always ask "which one is small tsu?" (it has no sound of its own to ask about).
- Safer saves: progress is flushed to storage before it replaces the old file. If the progress file is empty or
  unreadable, it's set aside as `progress.txt.broken` and your SD-card backup is restored, instead of starting from
  scratch and later overwriting the backup.
- Developers: `--shots` creates its folder; unused code and compiler warnings tidied up.

## 0.7.3 - 2026-11-17

- The season's petals, fireflies, leaves and snow now drift behind the cards and buttons instead of over them.
- Long settings groups (Display, Progress) scroll with the stylus: drag the list up or down. A drag never changes the
  row it started on.
- First launch now also asks about vibration (Off, Light or Strong), and buzzes as you choose so you can feel it.
  Settings › Progress › Run setup again includes the new step.
- Lessons, writing drills and free writing now always show kana in the handwriting font, so they match the stroke-order
  examples (print そ, for one, looks quite different from a handwritten one). Drills and menus still follow
  Settings › Display › Kana font.

## 0.7.2 - 2026-11-16

- The streak flame is gone for good: Kitsu now sits in the Streak box on Stats too, as she does on Home. This week's
  goal days on Home still change colour as your streak grows.

## 0.7.1 - 2026-11-16

- Autumn now has proper maple (momiji) leaves: seven pointed lobes and a stem, in the background pattern, the falling
  leaves and on Kitsu's head. They used to be five-point stars.

## 0.7.0 - 2026-11-16

- Meet Kitsu, your streak fox 🦊. She starts as an egg that hatches on your first day meeting the daily goal, then grows
  with the days you practise: 1 tail, 3 tails and a red scarf (7 days), 5 glowing tails (30 days) and a nine-tailed
  kyūbi with fox-fire (100 days). Tails are never lost, even if a streak breaks.
- She lives on the Home streak card: asleep until today's goal is met, awake after, with a happy hop the moment you hit it.
- Seasonal looks follow the background: a sakura blossom in spring, an uchiwa fan in summer, a momiji leaf in autumn
  and a blue scarf in winter.
- 8 accessories (suzu bell, hachimaki, omamori, lantern, fox mask, scroll, kanzashi, daruma) unlocked by achievements.
- New "Your fox" page (Stats, then START): her stage, progress to the next one, accessories, and Rename (up to 6 kana).
- A celebration screen when she hatches or grows. Existing players start with a fox that has already grown to match
  the days they have practised.
- First launch now also asks for a background: Seasons of Japan or Plain (shown live as you tap), between picking
  Light or Dark and saying what you already know.
- Settings > Progress > Run setup again: go back through the first-launch steps any time. Your progress and streak
  stay, and skipping ahead only ever moves you forwards.

## 0.6.0 - 2026-11-16

- New Home grid: the scrolling strip is gone. A big Lesson / Practise button and the あ → ア switch sit on top, with
  all eight modes below as tiles (Review, Mix-ups, Words, Timed, Stats, Stages, Kana chart, Settings). Everything is
  visible and one tap away, and the d-pad moves around the grid in all four directions.
- Seasons of Japan backgrounds (Settings > Display > Background): sakura in spring, seigaiha waves and fireflies in
  summer, momiji in autumn, snow in winter, in light and dark. On by default ("Auto" follows the calendar), or pick
  one season, or Plain for the old stripes. Petals, fireflies, leaves and snow drift gently over menus (never in drills
  or writing).
- Settings groups with more than five rows scroll (with ▲ ▼ buttons) instead of squeezing the rows.
- First launch now starts by picking Light or Dark (it changes the whole app as you tap), then what you already know.

## 0.5.0 - 2026-11-15

A whole new look: the "Handheld" design.

- Light (the new default) and dark versions of one clean design: a pale striped top screen and a dotted bottom screen,
  white cards with soft shadows, glossy pill buttons and smooth line icons (no more pixel art).
- Every mode has its own colour: lessons and practice orange, Review blue, Words green, Mix-ups purple, Timed red,
  Stats and Settings teal. Headers are bilingual (れんしゅう · Stage 3, ふくしゅう · Review).
- New Home: a scrolling strip of mode tiles (tap one, or scroll with the d-pad and press A), a big Lesson / Practise
  button (or START), a daily goal ring and a streak strip for the week (月 火 水 …).
- Answers are marked ○ or ×, like a Japanese test (せいかい! / ざんねん). Writing happens on a notepad page.
- Settings rows are segmented pills: every option is one tap away.
- Achievements get gold medals; Pause is a ひとやすみ card; results cheer in Japanese.
- New app icon: an orange あ tile with a little ア (`tools/make_icon.py` renders it).
- Bold text for headings and buttons (Zen Maru Gothic Bold, OFL). Stronger rumble for right answers.
- The streak flame grows with your streak: orange, then bigger and red at 7 days, blue at 30, purple at 100. It stays a
  pale outline until you meet today's goal ("today's goal keeps it lit"), then lights up and flickers.
- Gentle motion: screens fade in, the Home strip glides between tiles, the right answer pops and a wrong one shakes,
  and the results score counts up (a medal for a perfect session). `KT_MOTION=0` turns it off.
- Settings > Display shows a live preview (theme, kana font, canvas lines, ink). New **Reset settings** in Settings >
  Progress (press twice; your progress stays).
- Swipe the Home strip with the stylus: it follows your pen and settles on the nearest tile.
- Choice questions start with nothing highlighted: the d-pad cursor appears on your first d-pad or stick press, so a
  highlighted tile can't give the answer away.
- Fixes: the F+START Quit hint is back on Home; the top screen background could show black or stale patches on the device; small text is never squashed
  (Settings tiles); the Home strip glides in a fixed time even when frames are slow; tapping a side tile on Home now slides it to the centre, so every tile can be reached by touch; long mix-up
  hints no longer spill out of the answer banner; stage chips on Home fit their card; Extras is called Extras, not
  Stage 15; empty days on the Stats chart look empty; the Guide/Hint button is a proper on/off switch.

## 0.4.0 - 2026-11-15

- New words are taught before they're tested: a pack session shows up to 5 new words on cards (kana, rōmaji, meaning
  and the kana it's made of), then asks each one again a few questions later. "What does it mean?" questions, in packs
  and in normal practice, only use words you've already met.
- Browse a pack's words: the new **Words** button (or Y) on the pack screen lists every word with its meaning and
  whether it's new, learning, known or still locked (and which kana it needs).
- Spaced review for words: known words come back after 1, 3, 7, 14, 30 and 90 days, just like kana, and due words
  count in the Review tile.
- Achievements: 17 to earn (learning kana and words, streaks, timed scores, a perfect session...). A card and confetti
  pop up when you earn one; see them all, with your progress towards each, from Stats (X).
- A brighter Home screen: coloured cartridge stickers, icons on the mode tiles, a daily goal ring, a streak flame and
  a bilingual greeting (こんばんは · Good evening) with a little Japanese flag.

## 0.3.1 - 2026-11-15

- Fix: rumble only happened on wrong answers. The right-answer buzz was too short and soft to start the RG DS Plus
  motor; every buzz now uses the main motor channel with a minimum strength and length.
- Rumble patterns you can tell apart without looking: one tick for right, two buzzes for wrong, three quick ticks
  when a kana or word is learned, and one long rumble for an unlock or your daily goal.

## 0.3.0 - 2026-11-15

- Word packs: 12 topic packs (greetings, food & drink, numbers, time & days, people & family, animals, body, home &
  things, town & travel, nature & weather, actions, describing & colours), about 340 words written for Banana Kana.
  Open them from the new **Words** tile on Home. Pack quizzes mix kana → meaning, meaning → kana, reading and typing,
  and a word joins its pack once every kana in it is unlocked. Words have 4 steps like kana, with a cheer when one
  becomes known.
- Normal practice draws on the bigger word list too.

## 0.2.0 - 2026-11-15

- Hiragana ↔ katakana matching: once both scripts are open, some reading questions show a kana in one script and ask
  for its twin in the other (あ → ア). Counts towards the kana you're learning.
- Rumble: a tiny buzz for right answers, a firmer one for wrong answers, and a long one for unlocks and your daily goal.
  Settings > Display > Rumble (off, light, strong).
- Progress backup on the SD card: a copy is kept in `roms/ports/BananaKana/progress-backup.txt` (after sessions,
  unlocks, on exit and at most once a minute) and restored automatically if the main progress file is missing, e.g.
  after a reflash. Settings > Progress shows where it is.
- Settings groups with six rows now fit above the footer buttons.

## 0.1.4 - 2026-11-15

- Fix: the stick still made the selector jump. The RG DS Plus reports a phantom second stick that moves along with the
  real one in a different direction; only the real (left) stick is used now. `KT_STICKS=both` turns the second one back on.

## 0.1.3 - 2026-11-15

- Fix: the analogue stick no longer makes the selector jump around. Wobbling near a diagonal, the stick springing back
  past centre when let go, and a stick that rests slightly off centre no longer count as extra presses.

## 0.1.2 - 2026-11-15

- The little banana icon is gone from the top-left header; the BANANA KANA wordmark sits there instead.

## 0.1.1 - 2026-11-15

- The analogue stick moves like the d-pad (hold to repeat).
- Fix: the X and Y buttons were swapped on the device (X now checks, Y now shows hints).
- Word questions (reading a word, spelling it in kana or romaji, っ/ー words) show the English meaning under the word,
  whether or not the Word meanings drill is on.

## 0.1.0 - 2026-11-15

First release for BananaOS on the Anbernic RG DS Plus.

### Learning
- Hiragana, then katakana, in 15 stages each: the 10 basic rows, voiced kana, combinations, then Extras
  (small っ, the long-vowel ー and katakana for foreign sounds such as ティ, ファ and ヴ). 105 hiragana, 119 katakana.
- Every stage opens with a lesson: an original memory hint, stroke-order frames and untimed tracing.
- A kana is known after 4 steps, with at most 2 per session, so it has to stick across days. A miss takes one step away.
  Knowing a whole stage unlocks the next. Katakana unlocks after hiragana stage 10.
- Spaced review: known kana come back after 1, 3, 7, 14, 30 and 90 days. Home shows how many are due, and Review drills just those.
- Mix-ups: 24 built-in look-alike sets (さ/き, ね/れ/わ, シ/ツ, ソ/ン...) with notes on telling them apart, plus any pairs you've confused yourself.
- Daily goal (5 to 30 answers) and a streak.
- Staged practice: a new kana is multiple choice only, then typing, then writing. Writing starts over the full guide,
  then a faint outline, then from memory (one level per session; guided writing never costs a step).
- Already know some kana? Choose on first launch, or skip ahead (and unlock katakana early) in Settings > Script.
  Skipped kana count as known, with a quick review in a day or two.

### Drills
- Reading (kana to sound and back), word meanings (only words made of kana you've unlocked), writing with the stylus
  (scored stroke by stroke against KanjiVG; combinations are traced in two parts), and typing in romaji or on a kana keyboard.
- Timed challenge: 60 seconds of one-tap answers, with a personal best per script.
- Practice sets: pick kana in the chart and drill just those. A single kana is mixed with its look-alikes and row-mates.
- Free writing: trace any kana with a full guide, an outline or none. It doesn't touch your progress.
- Draw any kana: write freely and the app recognises which kana it is, then checks your stroke order.
- Stroke-order animation (Show me / START) and direction arrows on the guide.
- Writing options: rounded or handwriting (Klee One) kana font, canvas lines (cross, grid, none), ink thickness,
  and a relaxed / normal / strict writing check.

### App
- Two-screen layout: prompt and feedback on the top screen, answers, keyboard and tracing canvas on the touch screen.
- Kana chart (Basic / Voiced / Combos), stage map, stats with a 14-day chart and an accuracy heatmap.
- Settings modelled on BananaOS Settings, with dark and light themes. Hint chips draw the d-pad as a cross.
- Animated credits for BananaOS, KanjiVG, the fonts and Banana Player.
- Zen Maru Gothic fonts, rendered at the panel's real resolution.
- Progress is saved after every answer and on power-off. F + START quits; F or SELECT pauses during practice
  (or tap the Pause hint), also in lessons and free writing; F + START mid-practice pauses instead of quitting.
  The Back key next to F works as B (read straight from the input device). On-screen keyboards work with the d-pad and A.
- Confetti for newly known kana, unlocked stages, the daily goal and a new timed best.
- Volume and brightness (F + volume) show a level bar on the top screen, like the BananaOS menu.
- Sound effects (synthesised): taps, right and wrong answers, newly known kana, unlocks, daily goal, last seconds of
  the timed challenge. Off / quiet / normal / loud in Settings > Display.
