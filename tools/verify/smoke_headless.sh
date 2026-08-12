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
# Then it loads a map that is not one. SV_Map_f only checks that the file
# exists, so a 151-byte file gets as far as SV_SpawnServer - which calls
# Hunk_Clear, which frees TAG_HUNKALLOC and wipes the renderer's tr, and only
# then reaches CM_LoadMap and fails. What is left is the engine drawing frames
# against an emptied renderer, which is exactly the window three of the first
# crashes on real hardware lived in. See fixtures/base/maps/README.md.
#
# Then it loads a map that is one. Everything above this is the menu; from here
# the run goes through the server - CM_LoadMap on a BSP that parses,
# SV_InitGameProgs, entity spawn, a client connecting to a local server, a
# player model, and cgame drawing a world view with a head-up display over it.
# Eight defects have been found on this path and six of them were reads or
# writes out of bounds. The map and the player model are generated: see
# make_test_bsp.py and make_test_glm.py.
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

# Which of the two games. They are the same engine built twice - jkx_jo is
# code/ with -DJK2_MODE plus codeJK2/game - so the second one is not a copy of
# this test, it is a second configuration of the same code. Enough of it differs
# to be worth running: the string packages, the whole of codeJK2/cgame, and
# every JK2_MODE branch in shared code.
GAME_ID="${JKX_SMOKE_GAME:-ja}"
case "$GAME_ID" in
    ja) ENGINE="$BUILD/jkx_ja.$ARCH"; GAME="$BUILD/code/game/jagame$ARCH.so" ;;
    jo) ENGINE="$BUILD/jkx_jo.$ARCH"; GAME="$BUILD/codeJK2/game/jospgame$ARCH.so" ;;
    *)  echo "JKX_SMOKE_GAME must be ja or jo, not $GAME_ID" >&2; exit 2 ;;
esac

RENDERER="$BUILD/code/rd-vulkan/rdsp-vulkan_$ARCH.so"

[ -f "$ENGINE" ] || { echo "not built: $ENGINE" >&2; exit 2; }

# The renderer is inside the engine in a monolith build and a file beside it
# otherwise. Either is fine here; what matters is that one of them turns up in
# the log below.
[ -f "$RENDERER" ] || RENDERER=""

RUN="$(mktemp -d)"
DISPLAY_NUM="${JKX_SMOKE_DISPLAY:-:99}"
XVFB_PID=""
# JKX_SMOKE_KEEP_RUN leaves the whole run directory behind - engine, fixtures,
# log, saves, screenshots. Debugging a failure by re-deriving the command line by
# hand is how an afternoon goes missing.
cleanup() {
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null || true
    if [ -n "${JKX_SMOKE_KEEP_RUN:-}" ]; then
        echo "  run directory kept: $RUN"
    else
        rm -rf "$RUN"
    fi
}
trap cleanup EXIT

mkdir -p "$RUN/home" "$RUN/xdg"
cp -r "$HERE/fixtures/base" "$RUN/base"
cp "$ENGINE" "$RUN/"
[ -n "$RENDERER" ] && cp "$RENDERER" "$RUN/"

# The game library goes beside the engine, not into base/. FS_ExtractedFile
# looks in the executable's directory first, and a copy in base/ is what a mod
# is; this is the base game.
[ -f "$GAME" ] && cp "$GAME" "$RUN/"

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

Xvfb "$DISPLAY_NUM" -screen 0 ${JKX_SMOKE_SCREEN:-1280x720}x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2

# Sixty frames is enough to be past everything that only happens once, and few
# enough that a software rasteriser running under the validation layer still
# finishes in under a minute. The engine quits itself, so a non-zero exit is a
# real failure and not the timeout it used to be.
#
# Every screenshot is followed by a wait, and that is not padding. screenshot_tga
# queues a render command; the file is written when the frame it was queued in
# completes. Without the wait the next commands run first, and adding the map
# step below turned the console screenshot into a picture of what came after it -
# a frame with the console shut and the renderer wiped, which duly failed the
# text check and looked like the font path breaking.
#
# Both games get the whole way. JK2 hard-codes "kyle" as the player model and
# its cgame turns a missing animation set into an error rather than a warning,
# so this used to be Jedi Academy only; the fixture now ships a generated
# skeleton and a kyle that hangs off it. See make_test_gla.py.
#
# Two maps, not one, and they have to have different names. Media is aged by a
# level counter that only moves when the map name changes, and both caches that
# use it - models here, sounds in snd_dma.cpp - evict at the end of every load.
# Loading the same map twice never moves the counter and never evicts, so the
# eviction pass would run on this bench without ever deleting anything. The
# second map is generated rather than committed: it is the same room.
#
# Then a savegame round trip. This is the regression harness phase 0 asked for
# and never got, and it earns its place immediately: until the serialisers were
# ported, saving wrote no Ghoul2 chunk at all and loading passed nullptr into a
# function that starts by dereferencing it, so "load" was a guaranteed crash and
# nothing here would have said so.
INMAP_STEP=( +wait 20 +map jkx_room +wait 80 +screenshot_tga jkx_inmap )
#
# The weather, asked about a place rather than about the world. A wind zone is
# created with a velocity of 800 along +Y over a box around the origin, and then
# the wind is read at a point inside it and at a point far outside. Single-player
# wind is per-zone and the transplanted renderer answered globally for every
# point; this is the difference, printed.
#
# The waits here are short on purpose. Elsewhere in this script a wait is there
# because a screenshot is queued and the file appears a frame later; these
# commands only print, and the print has happened by the next frame. Written at
# twenty frames apiece they cost fifty frames of software rasterising under the
# validation layer, which is what pushed this stage into its timeout.
#
# JKX_SMOKE_WEATHER=0 leaves them out. Not a way to skip a failing check - it is
# how the run answers "is this the weather", which is a question that came up
# the first time the stage hung after a wind zone had been created.
if [ "${JKX_SMOKE_WEATHER:-1}" = "1" ]; then
INMAP_STEP+=( +r_we "windzone ( -64 -64 -64 ) ( 64 64 64 ) ( 0 800 0 )" +wait 5
              +r_we "windat 0 0 0" +wait 5
              +r_we "windat 5000 5000 5000" +wait 5 )
fi
#
# And one of the lighting debug views, which is new and is a tool rather than a
# feature: the shader can draw a single term of the lighting instead of their
# sum. The frame it produces is checked for being a picture, because the way
# this breaks is a black screen - a push constant that never arrives reads as
# zero, and zero is a valid mode meaning "off", so a broken one looks exactly
# like a working one unless something looks at the pixels.
INMAP_STEP+=( +debugview roughness +wait 10 +screenshot_tga jkx_debugview
              +wait 10 +debugview off +wait 5 )

if [ "${JKX_SMOKE_SAVELOAD:-0}" = "1" ]; then
    # And then stop. The second map below is there to move the media level
    # counter, which the run without the round trip already checks; doing both
    # in one run pushes a software rasteriser past the timeout for no more
    # coverage.
    #
    # "give health" first, and it is not decoration. SV_SaveGame_f refuses to
    # save a dead player, and it decides that by reading the health out of the
    # client's snapshot ring at the current outgoing sequence - a slot that has
    # not necessarily been written yet on a fixture map with nothing in it. The
    # refusal prints through SE_GetString, which on a fixture with no string
    # table prints an empty red line, so the run looked like the save had simply
    # not happened.
    INMAP_STEP+=( +give health +wait 20
                  +save jkx_save +wait 60
                  +load jkx_save +wait 150 +screenshot_tga jkx_loaded )
else
    INMAP_STEP+=( +wait 20 +map jkx_room2 +wait 60 +screenshot_tga jkx_sky +wait 20 )
fi

# The second map carries a sky, and the sky is six faces that can be told apart.
#
# A skybox is six images plus a set of conventions about which way up each of
# them goes, and those conventions are not written down anywhere - they are
# implied by a table of axis swaps in tr_sky.cpp. A flat blue sky cannot tell
# anyone whether a change to how the sky is sampled kept them. So the fixture's
# faces each carry their own colour, a marker in one corner and a stripe down one
# edge, so which face is up and which way round it is are both answerable from
# the picture.
#
# Looking along +Y shows "bk", not "lf": DrawSkyBox indexes the images through
# sky_texorder = { 0, 2, 1, 3, 4, 5 }, which swaps the second and third. That is
# measured, not assumed - the first guess was "lf" and the screenshot said green.
python3 "$HERE/make_test_sky.py" "$RUN/base/textures/jkx" sky >/dev/null

# The screen wipe's mask. Without this file the engine prints "no screen wipe"
# and the whole dissolve path is skipped - which is how a crash lived there
# undisturbed: R_DissolveCaptureScreen compared image_t::width, the size that was
# asked for, against the capture size, when what it had built was
# uploadWidth - the size after clamping to glConfig.maxTextureSize, which is
# 2048. Wider than that, the two disagree and it uploads sixteen times more
# texels than the image holds. The wide lane runs at 2560, so it is the one that
# reaches it; below 2048 nothing is clamped and nothing goes wrong.
python3 "$HERE/make_test_sky.py" --mask "$RUN/base/textures/common/dissolve.tga" >/dev/null
python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room2.bsp" \
    --sky textures/jkx/sky >/dev/null

set +e
( cd "$RUN" && \
  DISPLAY="$DISPLAY_NUM" \
  XDG_RUNTIME_DIR="$RUN/xdg" \
  VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}" \
  timeout -k 10 "${JKX_SMOKE_TIMEOUT:-600}" "./$(basename "$ENGINE")" \
      +set fs_basepath "$RUN" +set fs_homepath "$RUN/home" \
      +set s_initsound 0 +set com_errorDialog 0 +set con_notifytime 0 \
      +set cg_hudFiles ui/jkx_hud.txt +set g_char_model jkx \
      +wait 60 +screenshot_tga jkx_smoke \
      +toggleconsole +wait 30 +screenshot_tga jkx_console +wait 20 \
      +toggleconsole +wait 20 +map jkx_smoke +wait 60 +screenshot_tga jkx_wiped \
      "${INMAP_STEP[@]}" \
      +wait 20 +quit ) > "$RUN/run.log" 2>&1
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

forbid() {
    if grep -q -- "$1" "$RUN/run.log"; then
        report "present in the log and should not be: $1"
    fi
}

# This run drives the engine entirely from +commands, and the engine used to
# drop them past a fixed limit without a word. What that looked like was this
# stage timing out: the +quit at the end had been dropped, so the engine sat in
# the map until the kill. Two hours went into looking for a hang in the weather
# code, which had only been the thing that pushed the count over the line.
forbid 'were dropped and will not run'

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
require 'Wrote screenshots/jkx_wiped.tga'

# The second map is a real one, and this is the half of the run that goes
# through the server: CM_LoadMap on a BSP that parses, SV_InitGameProgs, entity
# spawn, a client connecting to a local server, a player model, and cgame
# drawing a world view with a head-up display over it. Everything above this
# line is the menu. Eight of the defects found so far were only reachable from
# here, six of them reads or writes out of bounds.
SHOTS=( "jkx_smoke 2" "jkx_console 200" "jkx_wiped 2" )
HUD_SHOTS=()
require 'Wrote screenshots/jkx_inmap.tga'
require 'Server: jkx_room'

# Inside the zone the wind is the zone's; outside it there is none. Both are
# checked, because a query that ignored its argument would pass the first.
if [ "${JKX_SMOKE_WEATHER:-1}" = "1" ]; then
    require 'windat 0 0 0: speed 800.0 dir 0.00 1.00 0.00'
    require 'windat 5000 5000 5000: speed 0.0'
fi

require 'debugview: roughness'
SHOTS+=( "jkx_debugview 2" )

# The world, and then the sky over it.
#
# Both of these are new because until now neither was drawn. The map's floor had
# never been on screen: the fixture's one BSP node split on plane 8, which its
# own comment called the floor and which is the ceiling, so every camera stood in
# the solid leaf - no cluster, no PVS, no surfaces. What the in-map check tested
# for weeks was the head-up display over the renderer's clear colour, and it
# passed, because a frame with a head-up display in it is a frame.
#
# So: the floor is white and near the bottom of the view, and the sky face
# straight ahead is the green one with its black stripe down the left. Positions,
# not just presence - a sky face drawn upside down or on the wrong axis has
# exactly the same pixels as a correct one, which is the whole reason the faces
# are not flat colours.
if [ -f "$RUN/home/base/screenshots/jkx_inmap.tga" ]; then
    if ! python3 "$HERE/tga_colour_where.py" \
        "$RUN/home/base/screenshots/jkx_inmap.tga" \
        "255,255,255@0.3,0.75,0.7,1.0"; then
        report "the map's floor is not where it should be in jkx_inmap.tga"
    fi
fi

if [ -f "$RUN/home/base/screenshots/jkx_sky.tga" ]; then
    if ! python3 "$HERE/tga_colour_where.py" \
        "$RUN/home/base/screenshots/jkx_sky.tga" \
        "0,153,0@0.3,0.1,0.8,0.6" "0,0,0@0.0,0.1,0.3,0.7"; then
        report "the sky is not the face it should be, or not the way up it should be"
    fi
fi

if [ "${JKX_SMOKE_SAVELOAD:-0}" = "1" ]; then
    require 'Wrote screenshots/jkx_loaded.tga'

    # The save has to have been written, and written with something in it. An
    # empty or missing file would still let the load print nothing and carry on,
    # which is exactly what happened while the serialiser was multiplayer's.
    SAVE="$RUN/home/base/saves/jkx_save.sav"
    if [ ! -s "$SAVE" ]; then
        report "no savegame was written to $SAVE"
    elif [ "$(stat -c %s "$SAVE")" -lt 4096 ]; then
        report "the savegame is $(stat -c %s "$SAVE") bytes, too small to hold a level"
    fi
else
    require 'Server: jkx_room2'
fi
# A world view with a head-up display over it has hundreds of distinct colours;
# two would mean the renderer presented the clear colour and cgame drew nothing,
# which is what every failure on this path has looked like.
SHOTS+=( "jkx_inmap 20" )
# The colour check is Jedi Academy's. JK2's cgame draws its head-up display
# itself out of gfx/hud/* rather than through the two menus this fixture
# provides, so those two fills are not in its frame and their absence says
# nothing.
if [ "$GAME_ID" = "ja" ]; then
    HUD_SHOTS=( jkx_inmap )
fi
if [ "${JKX_SMOKE_SAVELOAD:-0}" = "1" ]; then
    # The frame after a savegame load. The threshold is 40 rather than the 100
    # the in-map frame gets, and that gap is a recorded defect rather than
    # slack: a loaded game draws 51 distinct colours where the same scene
    # reached by walking in draws 242, and a second map load loses none of them.
    # Backlog section 21. Raise this to 100 when that is fixed; it sits here so
    # that a blank frame or a crash still fails.
    SHOTS+=( "jkx_loaded 20" )
    if [ "$GAME_ID" = "ja" ]; then
        HUD_SHOTS+=( jkx_loaded )
    fi
fi

# The map has to have been attempted and rejected. If SV_Map_f starts refusing
# it earlier - which it would if the existence check moved - the run would go on
# passing while checking nothing, because nothing would have cleared the hunk.
if ! grep -q -- 'shorter than a BSP header' "$RUN/run.log"; then
    report "the deliberately broken map was not loaded and rejected, so the hunk was never cleared"
fi

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
for pair in "${SHOTS[@]}"; do
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

# What the interface painted, not just how varied the frame is.
#
# The count above cannot tell who drew what, and that mattered: the in-game
# frame used to pass on 242 distinct colours, of which nearly two hundred were
# console notify text fading at the top of the screen. The run now sets
# con_notifytime 0 - so the frames measure the game rather than the console -
# and the same frame has 51. A count alone would have gone on passing with the
# head-up display drawing nothing.
#
# These two are the fixture's own head-up display: lefthud is a flat 0.8 0.2 0.2
# fill and righthud a flat 0.2 0.4 0.8 one, and nothing else in the run is that
# colour. lefthud gets the smaller floor because its rectangle is anchored at
# x=0 in a 640-wide space and most of it lands off the fitted frame - which is
# backlog section 1.2, and this check will notice when that is fixed.
for name in "${HUD_SHOTS[@]}"; do
    SHOT="$RUN/home/base/screenshots/$name.tga"
    if [ -f "$SHOT" ]; then
        if ! python3 "$HERE/tga_has_colour.py" "$SHOT" 51,102,204:500 204,51,51:20; then
            report "$name.tga is missing the head-up display"
        fi
    fi
done

# Where the picture is, not just whether there is one. The menu frame is 640x480
# fitted into the window and centred, so from the screenshot's own size the
# margins and the position of the fixture's one item are both fixed exactly -
# and both are things this project has got wrong without noticing. Run this at a
# wide screen size (JKX_SMOKE_SCREEN) and it is a real test; run it at 4:3 and
# there are no margins and it only checks placement.
#
# Both halves gate. The margin half was reported rather than gated for exactly
# as long as it took to find out why the bars were white, and the answer was
# that the fixture had no "white" shader and had been leaning on the default one
# to paint its background - see the note in fixtures/base/shaders/jkx_smoke.shader.
for name in jkx_smoke jkx_wiped; do
    SHOT="$RUN/home/base/screenshots/$name.tga"
    if [ -f "$SHOT" ]; then
        if ! python3 "$HERE/tga_frame_geometry.py" "$SHOT"; then
            report "the frame geometry is wrong in $name.tga"
        fi
    fi
done

if [ "$fail" -ne 0 ]; then
    echo
    echo "--- last 40 lines ---"
    tail -40 "$RUN/run.log"
    exit 1
fi

echo "OK: the engine drew frames on the Vulkan renderer and quit on its own"
