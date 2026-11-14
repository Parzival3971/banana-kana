#!/usr/bin/env bash
# Builds dist/BananaKana-<version>-aarch64.zip in the BananaOS Ports layout:
#   Banana Kana.sh + BananaKana/   (unzip into roms/ports on the SD card)
# Needs dist/aarch64/banana_kana (from scripts/docker-build-aarch64.sh) or a path as 2nd arg.
set -euo pipefail
version="${1:?usage: package.sh <version> [aarch64-binary]}"
root="$(cd "$(dirname "$0")/.." && pwd)"
bin="${2:-$root/dist/aarch64/banana_kana}"
stage="$(mktemp -d)"; trap 'rm -rf "$stage"' EXIT

[ -f "$root/launcher/Banana Kana.sh" ] || { echo "launcher/Banana Kana.sh missing"; exit 1; }
[ -f "$bin" ] || { echo "aarch64 binary missing: $bin"; exit 1; }
file "$bin" | grep -q "ARM aarch64" || { echo "$bin is not an aarch64 binary"; exit 1; }

cp "$root/launcher/Banana Kana.sh" "$stage/"; chmod +x "$stage/Banana Kana.sh"
mkdir -p "$stage/media/box"; cp "$root/launcher/media/box/Banana Kana.png" "$stage/media/box/"
mkdir -p "$stage/BananaKana"
cp "$bin" "$stage/BananaKana/banana_kana"; chmod +x "$stage/BananaKana/banana_kana"
cp -r "$root/app/data" "$stage/BananaKana/data"
echo "$version" > "$stage/BananaKana/VERSION"

mkdir -p "$root/dist"
(cd "$stage" && zip -qr "$root/dist/BananaKana-$version-aarch64.zip" .)
echo "Built dist/BananaKana-$version-aarch64.zip"; unzip -l "$root/dist/BananaKana-$version-aarch64.zip" | tail -n +4 | head -12
