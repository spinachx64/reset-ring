#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
python3 -m unittest discover -s tests -v
CJSON="${CJSON_DIR:-$ROOT/build-firmware/managed_components/espressif__cjson/cJSON}"
if [ ! -f "$CJSON/cJSON.c" ]; then
    if [ -z "${CJSON_DIR:-}" ] && [ -n "${IDF_PATH:-}" ]; then
        CJSON="$IDF_PATH/components/json/cJSON"
    fi
fi
if [ ! -f "$CJSON/cJSON.c" ]; then
    echo 'C++ tests need cJSON sources; set CJSON_DIR or configure IDF_PATH.' >&2
    exit 1
fi
mkdir -p .state
cc -c "$CJSON/cJSON.c" -I"$CJSON" -o .state/cJSON.o
c++ -std=c++17 -Wall -Wextra -Ifirmware/app_codex_timer -I"$CJSON" tests/test_quota_clock.cpp \
    firmware/app_codex_timer/controller/orientation.cpp firmware/app_codex_timer/model/quota_state.cpp \
    .state/cJSON.o -o .state/test_quota_clock
.state/test_quota_clock
