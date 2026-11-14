#!/usr/bin/env bash
# Builds the aarch64 binary in an arm64 container (QEMU on x86 hosts). Output: dist/aarch64/
set -euo pipefail
cd "$(dirname "$0")/.."
docker run --rm --platform linux/arm64 -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -v "$PWD":/src -w /src debian:bookworm bash scripts/build-aarch64.sh
