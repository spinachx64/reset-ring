#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-$ROOT/build-firmware}"
FACTORY_SHA=83de653c0be4e51e82079bee2386b90fb0538a25
if [ ! -d "$TARGET/.git" ]; then
    git clone https://github.com/m5stack/M5StopWatch-UserDemo.git "$TARGET"
    git -C "$TARGET" checkout --detach "$FACTORY_SHA"
fi
if [ "$(git -C "$TARGET" rev-parse HEAD)" != "$FACTORY_SHA" ]; then
    echo "Factory revision mismatch; use a separate checkout at $FACTORY_SHA" >&2; exit 1
fi
if [ ! -d "$TARGET/components/lvgl/src" ]; then
    (cd "$TARGET" && python3 fetch_repos.py)
fi
rm -rf "$TARGET/main/apps/app_codex_timer"
cp -R "$ROOT/firmware/app_codex_timer" "$TARGET/main/apps/"
python3 - "$TARGET" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1])
def insert(file,anchor,line):
    p=root/file; text=p.read_text()
    if line in text: return
    if text.count(anchor)!=1: raise SystemExit(f'Integration anchor mismatch: {file}')
    p.write_text(text.replace(anchor,anchor+'\n'+line))
insert('main/apps/apps.h','#include "app_template/app_template.h"','#include "app_codex_timer/app_codex_timer.h"')
p=root/'main/main.cpp'; text=p.read_text()
old='    GetMooncake().installApp(std::make_unique<AppLauncher>());'
new='    auto launcher = std::make_unique<AppLauncher>();\n    auto* launcher_ptr = launcher.get();\n    GetMooncake().installApp(std::move(launcher));'
if old in text: p.write_text(text.replace(old,new))
insert('main/main.cpp','GetMooncake().installApp(std::move(launcher));','    const int quota_app_id = GetMooncake().installApp(std::make_unique<QuotaWatchApp>());\n    launcher_ptr->openApp(quota_app_id);')
# Skip only the blocking first-boot tutorial when an app was explicitly selected.
# Launcher owns the open/close transition, so both-key exit works normally.
p=root/'main/apps/app_launcher/app_launcher.cpp'; text=p.read_text()
old='void AppLauncher::show_guide_page()\n{'
new=old+'\n    if (_going_to_open_app_id >= 0) return; // Codex StopWatch autostart'
if 'Codex StopWatch autostart' not in text:
    if old not in text: raise SystemExit('Launcher guide anchor mismatch')
    p.write_text(text.replace(old,new))
p=root/'sdkconfig.defaults'; text=p.read_text()
if 'CONFIG_BT_NIMBLE_ENABLED=y' not in text:
    p.write_text(text+'\n# Codex StopWatch NUS peripheral\nCONFIG_BT_ENABLED=y\nCONFIG_BT_NIMBLE_ENABLED=y\nCONFIG_BT_NIMBLE_ROLE_CENTRAL=n\nCONFIG_BT_NIMBLE_ROLE_OBSERVER=n\n')
# PlatformIO uses its board-specific sdkconfig as well.
p=root/'sdkconfig.m5stack-stopwatch'
if p.exists():
    text=p.read_text().replace('# CONFIG_BT_ENABLED is not set','CONFIG_BT_ENABLED=y')
    if 'CONFIG_BT_NIMBLE_ENABLED=y' not in text:
        text+='\nCONFIG_BT_NIMBLE_ENABLED=y\nCONFIG_BT_NIMBLE_ROLE_CENTRAL=n\nCONFIG_BT_NIMBLE_ROLE_OBSERVER=n\n'
    p.write_text(text)
PY
printf 'Integrated Codex StopWatch into %s\n' "$TARGET"
