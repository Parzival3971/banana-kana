#!/usr/bin/env bash
# Builds dist/KanjiTrainer-<version>-aarch64.zip in the BananaOS Ports layout:
#   Kanji Trainer.sh + KanjiTrainer/   (unzip into roms/ports on the SD card)
set -euo pipefail
version="${1:?usage: package.sh <version>}"
root="$(cd "$(dirname "$0")/.." && pwd)"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

[ -f "$root/launcher/Kanji Trainer.sh" ] || { echo "launcher/Kanji Trainer.sh missing"; exit 1; }
[ -d "$root/app" ] || { echo "app/ missing"; exit 1; }

cp "$root/launcher/Kanji Trainer.sh" "$stage/"
chmod +x "$stage/Kanji Trainer.sh"
mkdir "$stage/KanjiTrainer"
cp -r "$root/app/." "$stage/KanjiTrainer/"
echo "$version" > "$stage/KanjiTrainer/VERSION"

mkdir -p "$root/dist"
(cd "$stage" && zip -qr "$root/dist/KanjiTrainer-$version-aarch64.zip" .)
echo "Built dist/KanjiTrainer-$version-aarch64.zip"
