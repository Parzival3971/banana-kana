#!/bin/sh
# Kanji Trainer launcher for the BananaOS Ports shelf (unzip next to this script's KanjiTrainer/ folder).
# Writes a launch log with the environment and display info; send it along if something doesn't start.
DIR="$(cd "$(dirname "$0")" && pwd)/KanjiTrainer"
: "${HOME:=/data/home}"; export HOME
: "${XDG_CONFIG_HOME:=$HOME/.config}"; export XDG_CONFIG_HOME
STATE="$XDG_CONFIG_HOME/kanji-trainer"
mkdir -p "$STATE"
LOG="$STATE/launch.log"
{
  echo "=== kanji-trainer $(cat "$DIR/VERSION" 2>/dev/null) $(date) ==="
  uname -a
  (ldd --version 2>&1 | head -1) 2>/dev/null
  env | grep -E '^(SDL_|BANANA|BP_|XDG_|HOME=|DISPLAY|WAYLAND)' | sort
} > "$LOG" 2>&1
cd "$DIR" || exit 1
exec ./kanji_trainer --data "$DIR/data" --state-dir "$STATE" >> "$LOG" 2>&1
