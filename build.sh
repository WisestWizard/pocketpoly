#!/bin/sh
# fm1-base's build on this repository's firmware (settings in app/fm1.toml).
#   ./build.sh                 app/build/pocketpoly.fwsc        (JieLi toolchain in a container, AC79 SDK)
#   ./build.sh --host          app/build/host/pocketpoly_host   (the simulator; the system cc only)
#   ./build.sh --web           app/build/emu/                   (the browser emulator; Emscripten)
#   ./build.sh --release X.Y   app/build/pocketpoly-X.Y.fwsc
# A first argument naming another directory with an fm1.toml builds that one instead.
set -e
cd "$(dirname "$0")"
PYTHON="${PYTHON:-/opt/homebrew/bin/python3.14}"
APP=app
if [ -n "$1" ] && [ -f "$1/fm1.toml" ]; then
    APP="$1"
    shift
fi
exec "$PYTHON" base/tools/build.py "$APP" "$@"
