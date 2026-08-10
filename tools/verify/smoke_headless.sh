#!/usr/bin/env bash
# Start the engine on the Vulkan renderer, headless, draw frames, and quit.
#
# This does not need a GPU and it does not need a retail install. Xvfb gives SDL
# a display, lavapipe gives Vulkan a device, and tools/verify/fixtures is the
# smallest game directory the engine will accept - four files, three of which are
# the price of entry rather than content. See the README there.
#
# What it covers: loading the renderer module, the refimport and refexport
# handshake, instance, device, surface, swapchain, the shader pak, fonts, the
# menu system, and then an ordinary frame loop that draws 2D and presents - and
# a clean shutdown through the engine's own quit rather than a signal.
#
# It ends with a screenshot, which is checked for being a picture rather than a
# flat colour. A frame that drew nothing still presents and still writes a file;
# the only thing that separates it from a working one is the content.
#
# Where the validation layer is installed it is switched on and any message
# fails the run. That is not decoration: this is how a set of descriptor sets
# bound past the end of the pipeline layout was found - twenty-one spec
# violations per frame on any device reporting the Vulkan minimum of eight bound
# sets, invisible on a desktop GPU with a limit of 32.
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

HERE="$(cd "$(dirname "$0")" && pwd)"
ARCH="$(uname -m)"
ENGINE="$BUILD/jkx_ja.$ARCH"
RENDERER="$BUILD/code/rd-vulkan/rdsp-vulkan_$ARCH.so"

[ -f "$ENGINE" ] || { echo "not built: $ENGINE" >&2; exit 2; }

# The renderer is inside the engine in a monolith build and a file beside it
# otherwise. Either is fine here; what matters is that one of them turns up in
# the log below.
[ -f "$RENDERER" ] || RENDERER=""

RUN="$(mktemp -d)"
DISPLAY_NUM="${JKX_SMOKE_DISPLAY:-:99}"
XVFB_PID=""
cleanup() {
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null || true
    rm -rf "$RUN"
}
trap cleanup EXIT

mkdir -p "$RUN/home" "$RUN/xdg"
cp -r "$HERE/fixtures/base" "$RUN/base"
cp "$ENGINE" "$RUN/"
[ -n "$RENDERER" ] && cp "$RENDERER" "$RUN/"

if [ -n "$PAK" ]; then
    [ -f "$PAK" ] || { echo "no such shader pak: $PAK" >&2; exit 2; }
    cp "$PAK" "$RUN/base/shaders.pak"
else
    python3 "$HERE/../shadergen/shadergen.py" build --out "$RUN/spv" >/dev/null
    python3 "$HERE/../shadergen/shadergen.py" pack --spv-dir "$RUN/spv" \
        --out "$RUN/base/shaders.pak" >/dev/null
fi

# The validation layer, if this machine has one. Enabled through the loader
# rather than by the engine, so that a plain release build is what gets tested.
VALIDATION=0
if [ -f /usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json ] &&
   [ "${JKX_SMOKE_NO_VALIDATION:-0}" != "1" ]; then
    VALIDATION=1
    export VK_LOADER_LAYERS_ENABLE='*validation*'
fi

Xvfb "$DISPLAY_NUM" -screen 0 1280x720x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2

# Sixty frames is enough to be past everything that only happens once, and few
# enough that a software rasteriser running under the validation layer still
# finishes in under a minute. The engine quits itself, so a non-zero exit is a
# real failure and not the timeout it used to be.
set +e
( cd "$RUN" && \
  DISPLAY="$DISPLAY_NUM" \
  XDG_RUNTIME_DIR="$RUN/xdg" \
  VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}" \
  timeout 300 "./$(basename "$ENGINE")" \
      +set fs_basepath "$RUN" +set fs_homepath "$RUN/home" \
      +set s_initsound 0 \
      +wait 60 +screenshot_tga jkx_smoke \
      +toggleconsole +wait 30 +screenshot_tga jkx_console \
      +wait 10 +quit ) > "$RUN/run.log" 2>&1
status=$?
set -e

fail=0
report() {
    echo "$1"
    fail=1
}

if [ "$status" -ne 0 ]; then
    report "the engine exited with $status, and it was asked to quit"
fi

require() {
    if ! grep -q -- "$1" "$RUN/run.log"; then
        report "missing from the log: $1"
    fi
}

# There is one renderer and it is inside the engine, so this is a check that it
# started rather than a check on which one started.
if ! grep -q -- 'renderer: rd-vulkan, built in' "$RUN/run.log"; then
    report "the log does not say the Vulkan renderer came up"
fi

require 'selected physical device'
require 'VK_RENDERER:'
require 'selected presentation mode'
require 'Common Initialization Complete'
require 'Wrote screenshots/jkx_smoke.tga'
require 'Wrote screenshots/jkx_console.tga'

if grep -qE 'Segmentation fault|signal SIGSEGV|SIGABRT' "$RUN/run.log"; then
    report "the engine died on a signal"
fi

# Checked from the log rather than from the exit code, because UndefinedBehavior
# Sanitizer prints and carries on by default: a build that reports on every frame
# still exits zero. This is what makes running the sanitizer build worth
# anything - building it and never running it checks nothing.
if grep -qE 'runtime error:|AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer' "$RUN/run.log"; then
    report "a sanitizer had something to say:"
    grep -E 'runtime error:|ERROR: (Address|Leak)Sanitizer' "$RUN/run.log" | head -10
fi

if [ "$VALIDATION" = "1" ]; then
    if grep -qE 'VUID-|Validation Error|Validation Warning' "$RUN/run.log"; then
        report "the validation layer had something to say:"
        grep -oE 'VUID-[A-Za-z0-9-]+' "$RUN/run.log" | sort | uniq -c | sort -rn | head -10
    fi
elif [ "${JKX_SMOKE_NO_VALIDATION:-0}" = "1" ]; then
    echo "  (validation switched off by JKX_SMOKE_NO_VALIDATION)"
else
    echo "  (no validation layer installed, Vulkan usage not checked)"
fi

# Two screenshots: the menu, and the console over it. The console frame is the
# text check - the fixture ships a distance field atlas, so the console draws a
# page of real glyphs, and a page of antialiased text has hundreds of distinct
# greys in it where an empty panel has a handful.
#
# It has already earned its place twice over: a null dereference in the console
# draw path, and a fragment shader that sampled a descriptor set nobody had
# written, which on this software rasteriser is a segfault inside the JIT
# several frames after anything to do with fonts.
for pair in "jkx_smoke 2" "jkx_console 200"; do
    set -- $pair
    name="$1"
    want="$2"
    SHOT="$RUN/home/base/screenshots/$name.tga"
    if [ -f "$SHOT" ]; then
        # A frame that drew nothing presents and writes a file just the same.
        # What separates it from a working one is what is in the picture.
        if ! python3 "$HERE/tga_is_a_picture.py" "$SHOT" "$want"; then
            report "$name.tga did not draw what it should have"
        fi
        if [ -n "${JKX_SMOKE_KEEP_SHOT:-}" ]; then
            cp "$SHOT" "${JKX_SMOKE_KEEP_SHOT%.tga}_$name.tga"
            echo "  screenshot: ${JKX_SMOKE_KEEP_SHOT%.tga}_$name.tga"
        fi
    else
        report "no $name.tga was written"
    fi
done

if [ "$fail" -ne 0 ]; then
    echo
    echo "--- last 40 lines ---"
    tail -40 "$RUN/run.log"
    exit 1
fi

echo "OK: the engine drew frames on the Vulkan renderer and quit on its own"
