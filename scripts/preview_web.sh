#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-8765}"
exec python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$ROOT/web-preview"
