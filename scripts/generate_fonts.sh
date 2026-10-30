#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
for SIZE in 44 49 68; do
    npx --yes lv_font_conv@1.5.3 --font firmware/app_codex_timer/fonts/RobotoMono.ttf \
        --size "$SIZE" --bpp 4 --format lvgl --symbols '0123456789:-%' --no-compress \
        -o "firmware/app_codex_timer/fonts/quota_mono_$SIZE.c"
done
