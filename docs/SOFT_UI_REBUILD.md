# Soft UI rebuild

This branch turns the approved 640×480 mock-ups into the production SDL interface while preserving the existing lesson,
progress, input, and seasonal systems. The RG DS Plus scales each logical screen to 1024×768.

## Phase 1 — visual foundation

- Warm ivory surfaces, plum type and outlines, softer mode colours, framed headers, cards and controls.
- Shared primitives apply the design to every screen without adding expensive textures or effects.
- Existing dark theme remains available.

## Phase 2 — Home and navigation

- Final Home hierarchy: lesson action, script switch, eight activity tiles, Kitsu streak strip and progress overview.
- Visible, focusable Quit control plus a Cancel-first confirmation dialog.
- `F + START` remains the quick hardware shortcut.

## Phase 3 — learning flows

- Lessons, reading/meaning/type/write drills, free writing, pause and results inherit the new components.
- Writing paper, guides and stroke-order feedback remain high contrast and unchanged functionally.

## Phase 4 — progress and collection screens

- Stages, kana chart, word packs, stats, achievements, Your Fox and settings use the same design tokens.
- Existing save format and all progression rules remain compatible.

## Phase 5 — Kitsu and seasonal polish

- New Kitsu/banana-tail brand mark is shared by the BananaOS shelf, Welcome, unlock and Credits screens.
- New pixel Kitsu appears on Home for the first growth stage, including equipped accessory overlays.
- Later growth forms continue to use the scalable animated fox artwork.
- Calendar auto-sync, fixed-season overrides and motion settings are preserved. Seasonal particles are clipped to each LCD
  and rendered behind opaque content cards.

## Phase 6 — verification and handheld package

- Native core tests and the scripted two-screen UI suite must pass.
- The scripted suite captures every major screen, both themes, all seasons and the Quit confirmation.
- The aarch64/QEMU job must build, run tests and produce the BananaOS ZIP artifact.
- Final acceptance happens on the RG DS Plus; no release is tagged until that device test passes.

## Phase 7 — learning and onboarding polish

- Home previews session composition and time; Sessions offers Quick, Standard, Study and Review-only presets.
- Feedback tracks a session answer streak and lets Kitsu celebrate without adding expensive animation assets.
- Results show strengthened steps and daily-goal progress, with a focused Review mistakes action.
- Kana details expose accuracy, last practice, next review and mix-ups.
- Setup teaches D-pad movement and A-button selection before asking how much kana the learner already knows.
