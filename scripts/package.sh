#!/usr/bin/env bash
# Builds dist/KanjiTrainer-<version>-aarch64.zip in the BananaOS Ports layout:
#   Kanji Trainer.sh + KanjiTrainer/   (unzip into roms/ports on the SD card)
# Needs dist/aarch64/kanji_trainer (from scripts/docker-build-aarch64.sh) or a path as 2nd arg.
set -euo pipefail
version="${1:?usage: package.sh <version> [aarch64-binary]}"
root="$(cd "$(dirname "$0")/.." && pwd)"
bin="${2:-$root/dist/aarch64/kanji_trainer}"
stage="$(mktemp -d)"; trap 'rm -rf "$stage"' EXIT

[ -f "$root/launcher/Kanji Trainer.sh" ] || { echo "launcher/Kanji Trainer.sh missing"; exit 1; }
[ -f "$bin" ] || { echo "aarch64 binary missing: $bin"; exit 1; }
file "$bin" | grep -q "ARM aarch64" || { echo "$bin is not an aarch64 binary"; exit 1; }

cp "$root/launcher/Kanji Trainer.sh" "$stage/"; chmod +x "$stage/Kanji Trainer.sh"
mkdir -p "$stage/KanjiTrainer"
cp "$bin" "$stage/KanjiTrainer/kanji_trainer"; chmod +x "$stage/KanjiTrainer/kanji_trainer"
cp -r "$root/app/data" "$stage/KanjiTrainer/data"
echo "$version" > "$stage/KanjiTrainer/VERSION"

mkdir -p "$root/dist"
(cd "$stage" && zip -qr "$root/dist/KanjiTrainer-$version-aarch64.zip" .)
echo "Built dist/KanjiTrainer-$version-aarch64.zip"; unzip -l "$root/dist/KanjiTrainer-$version-aarch64.zip" | tail -n +4 | head -12
