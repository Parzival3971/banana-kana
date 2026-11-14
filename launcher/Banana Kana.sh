#!/bin/sh
# Banana Kana launcher for the BananaOS Ports shelf.
# Writes a launch log (environment, display and backlight state) - send it along if something doesn't work.
DIR="$(cd "$(dirname "$0")" && pwd)/BananaKana"
: "${HOME:=/data/home}"; export HOME
: "${XDG_CONFIG_HOME:=$HOME/.config}"; export XDG_CONFIG_HOME
STATE="$XDG_CONFIG_HOME/banana-kana"
mkdir -p "$STATE"
# One-time migration of progress from the old app name.
OLD="$XDG_CONFIG_HOME/kanji-trainer"
if [ -f "$OLD/progress.txt" ] && [ ! -f "$STATE/progress.txt" ]; then cp "$OLD/progress.txt" "$STATE/progress.txt"; fi
LOG="$STATE/launch.log"
{
  echo "=== banana-kana $(cat "$DIR/VERSION" 2>/dev/null) $(date) ==="
  uname -a
  (ldd --version 2>&1 | head -1) 2>/dev/null
  env | grep -E '^(SDL_|BANANA|BP_|KT_|XDG_|HOME=|DISPLAY|WAYLAND)' | sort
  echo "--- /run/banana:"; ls -l /run/banana 2>&1
  echo "--- backlights:"
  for b in /sys/class/backlight/*; do
    [ -d "$b" ] && echo "$b brightness=$(cat "$b/brightness" 2>&1) max=$(cat "$b/max_brightness" 2>&1) bl_power=$(cat "$b/bl_power" 2>&1)"
  done
} > "$LOG" 2>&1

# BananaOS may leave the bottom backlight off for Ports apps. Switch it on (0 = on, 4 = off).
BL="$(cat /run/banana/backlight-bottom 2>/dev/null)"
{
  echo "--- bottom backlight id: '$BL'"
  if [ -n "$BL" ]; then
    case "$BL" in /*) P="$BL" ;; *) P="/sys/class/backlight/$BL" ;; esac
    echo "path $P  bl_power before: $(cat "$P/bl_power" 2>&1)  brightness: $(cat "$P/brightness" 2>&1)"
    echo 0 > "$P/bl_power" 2>&1
    if [ "$(cat "$P/brightness" 2>/dev/null)" = "0" ] && [ -f "$P/max_brightness" ]; then cat "$P/max_brightness" > "$P/brightness" 2>&1; fi
    echo "after: bl_power=$(cat "$P/bl_power" 2>&1) brightness=$(cat "$P/brightness" 2>&1)"
  fi
  ls -l /run/banana/panel-off 2>&1
} >> "$LOG" 2>&1

cd "$DIR" || exit 1
exec ./banana_kana --data "$DIR/data" --state-dir "$STATE" >> "$LOG" 2>&1
