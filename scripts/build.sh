#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIO="${PIO_BIN:-$(command -v pio || true)}"
[ -n "$PIO" ] || { echo 'PlatformIO not found; install pio or set PIO_BIN.' >&2; exit 1; }
"$ROOT/scripts/install_firmware.sh"
"$PIO" run -d "$ROOT/build-firmware" "$@"
