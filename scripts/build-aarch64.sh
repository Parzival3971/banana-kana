#!/usr/bin/env bash
# Runs INSIDE an arm64 Debian bookworm container (or natively on arm64).
# Builds, runs the core tests and the scripted UI self-test, and records what the binary needs.
set -euo pipefail
cd "$(dirname "$0")/.."
# Hand build output back to the host user (container runs as root) even if the build fails.
trap '[ -n "${HOST_UID:-}" ] && chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" dist build-arm 2>/dev/null || true' EXIT
[ "$(uname -m)" = aarch64 ] || { echo "must run on aarch64 (use scripts/docker-build-aarch64.sh)"; exit 1; }
if command -v apt-get >/dev/null; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends build-essential cmake pkg-config binutils ca-certificates \
    libsdl2-dev libsdl2-ttf-dev >/dev/null
fi
cmake -S . -B build-arm -DCMAKE_BUILD_TYPE=Release -DKT_STATIC_STDCPP=ON >/dev/null
cmake --build build-arm -j"$(nproc)"
ctest --test-dir build-arm --output-on-failure
SDL_VIDEODRIVER=dummy KT_SOFTWARE=1 timeout 600 build-arm/banana_kana --selftest --state-dir /tmp/kt --data app/data

mkdir -p dist/aarch64
cp build-arm/banana_kana dist/aarch64/banana_kana
strip dist/aarch64/banana_kana
{
  echo "Banana Kana aarch64 build requirements"
  echo "built on: $(. /etc/os-release && echo "$PRETTY_NAME"), glibc $(ldd --version | head -1 | grep -o '[0-9.]*$')"
  echo "built against SDL2 $(pkg-config --modversion sdl2), SDL2_ttf $(pkg-config --modversion SDL2_ttf)"
  echo "needs glibc symbols up to: $(objdump -T dist/aarch64/banana_kana | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
  echo "shared libraries (NEEDED):"
  readelf -d dist/aarch64/banana_kana | grep NEEDED | sed 's/.*\[\(.*\)\]/  \1/'
} | tee dist/aarch64/REQUIREMENTS.txt
