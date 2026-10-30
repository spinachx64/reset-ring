#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LABEL=com.spinach.codex-stopwatch
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
PYTHON="${BRIDGE_PYTHON:-$ROOT/.venv/bin/python}"
CODEX="${CODEX_BIN:-$(command -v codex || true)}"
if [ ! -x "$PYTHON" ] || [ -z "$CODEX" ]; then
    echo 'Create .venv, install requirements.txt and install Codex CLI first.' >&2; exit 1
fi
mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs/CodexStopWatch"
python3 - "$ROOT" "$PYTHON" "$CODEX" "$PLIST" <<'PY'
import os, plistlib, sys
from pathlib import Path
root,python,codex,output=sys.argv[1:]
logs=Path.home()/'Library/Logs/CodexStopWatch'
config={
    'Label':'com.spinach.codex-stopwatch',
    'ProgramArguments':[python,'-m','bridge.main','--codex',codex,'--log-file',str(logs/'bridge.log')],
    'WorkingDirectory':root,
    'RunAtLoad':True,'KeepAlive':True,'ThrottleInterval':30,
    'ProcessType':'Background',
    'EnvironmentVariables':{'PATH':os.environ['PATH'],'PYTHONUNBUFFERED':'1'},
    # App logs rotate themselves. Do not duplicate them into an unbounded launchd log.
    'StandardOutPath':'/dev/null','StandardErrorPath':'/dev/null',
}
if os.environ.get('CODEX_HOME'):
    config['EnvironmentVariables']['CODEX_HOME']=os.environ['CODEX_HOME']
with open(output,'wb') as f: plistlib.dump(config,f)
PY
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
printf 'Started %s; log: %s/Library/Logs/CodexStopWatch/bridge.log\n' "$LABEL" "$HOME"
