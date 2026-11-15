#!/usr/bin/env bash
# Prints the release notes for a version: its CHANGELOG.md section, then the device requirements.
# Usage: release-notes.sh <version> [requirements-file]
set -euo pipefail
version="${1:?usage: release-notes.sh <version> [requirements-file]}"
root="$(cd "$(dirname "$0")/.." && pwd)"
req="${2:-$root/dist/aarch64/REQUIREMENTS.txt}"
section="$(awk -v v="$version" '
  /^## / { if (found) exit; if (index($0, "## " v " ") == 1 || $0 == "## " v) { found = 1; next } }
  found { print }' "$root/CHANGELOG.md")"
[ -n "$section" ] || { echo "CHANGELOG.md has no section for $version" >&2; exit 1; }
printf '%s\n\n### Install\nUnzip `BananaKana-%s-aarch64.zip` into `roms/ports` on the SD card, then open **Banana Kana** from the Ports shelf.\n' "$section" "$version"
if [ -f "$req" ]; then printf '\n### Requirements\n```\n%s\n```\n' "$(cat "$req")"; fi
