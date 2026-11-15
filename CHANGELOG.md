# Changelog

All notable changes to Banana Kana. Releases are built by GitHub Actions when `VERSION` changes on `main`;
the release notes come from the matching section below.

## 0.1.1 - unreleased

- Both analogue sticks move like the d-pad (hold to repeat).
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
