# banana-kana

Hiragana/katakana drills and stylus tracing for the Anbernic RG DS Plus (BananaOS).

Learn hiragana then katakana in 14 stages each: a short lesson per stage (hint, stroke order, tracing), then practice
(reading, word meanings, writing with the stylus, typing). A kana is known after 4 steps spread over at least two
sessions; knowing a whole stage unlocks the next. Dark and light themes.

Releases are built by GitHub Actions when `VERSION` changes on `main`.
Stroke data: [KanjiVG](https://kanjivg.tagaini.net/) (CC BY-SA 3.0). Fonts: Zen Maru Gothic, Pixelify Sans, Noto Sans CJK JP (SIL OFL 1.1).

## Install (BananaOS on RG DS Plus)
1. Unzip `BananaKana-<version>-aarch64.zip` into `roms/ports` on the SD card. You should end up with
   `roms/ports/Banana Kana.sh`, `roms/ports/BananaKana/` and `roms/ports/media/box/Banana Kana.png`.
   On a Mac the zip may open into a folder of its own: move its contents out into `roms/ports`.
2. Open the Ports shelf and choose **Banana Kana**.

Controls: touch/stylus on the bottom screen (buttons act when you lift), D-pad + A to choose, B back,
F + START (or SELECT + START) quits to the menu. Progress is saved after every answer.
Problems? Send `home/.config/banana-kana/launch.log` from the card.

Uninstall: delete `Banana Kana.sh`, `BananaKana/` and `media/box/Banana Kana.png` from `roms/ports`.
