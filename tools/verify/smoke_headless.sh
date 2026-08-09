#!/usr/bin/env bash
# Start the engine on the Vulkan renderer, headless, with no game data.
#
# This does not need a GPU and it does not need a retail install. Xvfb gives SDL
# a display, lavapipe gives Vulkan a device, and the only game data it fabricates
# is the two files the engine refuses to start without: a default.cfg and one
# shader. Everything after that - loading the renderer module, the refimport and
# refexport handshake, instance, device, surface, swapchain, the shader pak,
# registering the first font - is the real thing.
#
# The run always ends in a fatal error, because there is no ui/menus.txt and
# there cannot be one without the retail assets. That is the success condition:
# reaching the missing menu means everything before it worked. What this catches
# is anything that fails earlier - and it has already caught a cvar_t * that
# existed only to satisfy the linker and was null at the first font.
#
# Usage:
#   tools/verify/smoke_headless.sh <build dir> [shaders.pak]

set -euo pipefail

BUILD="${1:-}"
PAK="${2:-}"
if [ -z "$BUILD" ] || [ ! -d "$BUILD" ]; then
    echo "usage: $0 <build dir> [shaders.pak]" >&2
    exit 2
fi

ARCH="$(uname -m)"
ENGINE="$BUILD/jkx_ja.$ARCH"
RENDERER="$BUILD/code/rd-vulkan/rdsp-vulkan_$ARCH.so"

for f in "$ENGINE" "$RENDERER"; do
    [ -f "$f" ] || { echo "not built: $f" >&2; exit 2; }
done

RUN="$(mktemp -d)"
DISPLAY_NUM="${JKX_SMOKE_DISPLAY:-:99}"
XVFB_PID=""
cleanup() {
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null || true
    rm -rf "$RUN"
}
trap cleanup EXIT

mkdir -p "$RUN/base/shaders" "$RUN/home" "$RUN/xdg"
cp "$ENGINE" "$RUN/"
cp "$RENDERER" "$RUN/"

# The engine will not start without a default.cfg, and the renderer will not
# finish R_Init without at least one .shader file. Neither is a stand-in for
# game data; they are the price of getting to the part being tested.
printf 'bind ESCAPE "togglemenu"\n' > "$RUN/base/default.cfg"
printf 'jkx/smoke\n{\n\t{\n\t\tmap $whiteimage\n\t}\n}\n' > "$RUN/base/shaders/jkx_smoke.shader"

if [ -n "$PAK" ]; then
    [ -f "$PAK" ] || { echo "no such shader pak: $PAK" >&2; exit 2; }
    cp "$PAK" "$RUN/base/shaders.pak"
else
    python3 "$(dirname "$0")/../shadergen/shadergen.py" build --out "$RUN/spv" >/dev/null
    python3 "$(dirname "$0")/../shadergen/shadergen.py" pack --spv-dir "$RUN/spv" \
        --out "$RUN/base/shaders.pak" >/dev/null
fi

Xvfb "$DISPLAY_NUM" -screen 0 1280x720x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2

# The engine ends this run through Sys_Error, which opens an SDL message box and
# then waits for someone to dismiss it. Nobody will, so waiting for the process
# to exit would mean waiting for the timeout every time. Instead: watch the log
# for the line that says it got where it was going, and stop it there.
touch "$RUN/run.log"
( cd "$RUN" && \
  DISPLAY="$DISPLAY_NUM" \
  XDG_RUNTIME_DIR="$RUN/xdg" \
  VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}" \
  exec "./$(basename "$ENGINE")" \
      +set fs_basepath "$RUN" +set com_homepath smoke \
      +set cl_renderer rdsp-vulkan ) > "$RUN/run.log" 2>&1 &
engine=$!

deadline=$(( SECONDS + 120 ))
while [ "$SECONDS" -lt "$deadline" ]; do
    if grep -q 'menu file not found' "$RUN/run.log" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$engine" 2>/dev/null; then
        break
    fi
    sleep 1
done

# SIGTERM first, then SIGKILL. The engine's fatal path opens an SDL message box,
# which forks and waits, and a polite signal to something already blocked inside
# a wait is not reliably enough to end it.
set +e
if kill -0 "$engine" 2>/dev/null; then
    kill "$engine" 2>/dev/null
    for _ in 1 2 3 4 5; do
        kill -0 "$engine" 2>/dev/null || break
        sleep 1
    done
    kill -9 "$engine" 2>/dev/null
    pkill -9 -P "$engine" 2>/dev/null
fi
wait "$engine"
status=$?
set -e

# 143 is the kill above, and 124/1 are ordinary ways for this run to stop. A
# signal that is not SIGTERM means something broke.
case "$status" in
    0|1|124|137|143) ;;
    *) echo "engine exited with status $status"; tail -40 "$RUN/run.log"; exit 1 ;;
esac

fail=0
require() {
    if ! grep -q -- "$1" "$RUN/run.log"; then
        echo "missing from the log: $1"
        fail=1
    fi
}

require 'Trying to load "rdsp-vulkan_'
require 'selected physical device'
require 'VK_RENDERER:'
require 'selected presentation mode'
require 'menu file not found'

if grep -qE 'Segmentation fault|signal SIGSEGV|SIGABRT' "$RUN/run.log"; then
    echo "the engine died on a signal"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo
    echo "--- last 40 lines ---"
    tail -40 "$RUN/run.log"
    exit 1
fi

echo "OK: the engine came up on the Vulkan renderer and stopped at the missing game data"
