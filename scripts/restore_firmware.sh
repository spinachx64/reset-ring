#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:?Usage: restore_firmware.sh SERIAL_PORT [BACKUP]}"
BACKUP="${2:-$ROOT/backups/stopwatch-original-20261030.bin}"
PYTHON="${ESPTOOL_PYTHON:-python3}"
"$PYTHON" -c 'import os,sys; sys.exit(0 if os.path.getsize(sys.argv[1]) == 16777216 else "Expected full 16MiB backup")' "$BACKUP"
"$PYTHON" -m esptool --chip esp32s3 --port "$PORT" --baud 921600 write-flash 0 "$BACKUP"
