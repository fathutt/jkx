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

# Which of the two games. They are the same engine built twice - jkx_jk2 is
# code/ with -DJK2_MODE plus games/jk2/game - so the second one is not a copy of
# this test, it is a second configuration of the same code. Enough of it differs
# to be worth running: the string packages, the whole of games/jk2/cgame, and
# every JK2_MODE branch in shared code.
GAME_ID="${JKX_SMOKE_GAME:-jka}"
case "$GAME_ID" in
    jka) ENGINE="$BUILD/jkx_jka.$ARCH"; GAME="$BUILD/games/jka/game/jkagame$ARCH.so" ;;
    jk2) ENGINE="$BUILD/jkx_jk2.$ARCH"; GAME="$BUILD/games/jk2/game/jk2game$ARCH.so" ;;
    *)  echo "JKX_SMOKE_GAME must be jka or jk2, not $GAME_ID" >&2; exit 2 ;;
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
    # JKX_SMOKE_SHOT_DIR keeps the screenshots and nothing else, which is what a
    # comparison between two runs wants: the run directory is a hundred megabytes
    # of engine and fixtures, and the frames are the only part worth keeping.
    if [ -n "${JKX_SMOKE_SHOT_DIR:-}" ] && [ -d "$RUN/home/base/screenshots" ]; then
        mkdir -p "$JKX_SMOKE_SHOT_DIR"
        cp "$RUN"/home/base/screenshots/*.tga "$JKX_SMOKE_SHOT_DIR/" 2>/dev/null || true
    fi
    if [ -n "${JKX_SMOKE_KEEP_RUN:-}" ]; then
        echo "  run directory kept: $RUN"
    else
        rm -rf "$RUN"
    fi
}
trap cleanup EXIT

mkdir -p "$RUN/home" "$RUN/xdg"
cp -r "$HERE/fixtures/base" "$RUN/base"

# JKX_SMOKE_NO_SHADERS removes the material definitions, which used to be a
# fatal error three words long. An installation whose game data is in the wrong
# place hits exactly this, so the interesting question is whether the engine
# still starts and says something useful rather than whether it draws correctly.
if [ "${JKX_SMOKE_NO_SHADERS:-0}" = "1" ]; then
    rm -f "$RUN"/base/shaders/*.shader
    # Without materials there is no picture to assert, so the picture checks
    # step aside the same way they do for JKX_SMOKE_PLAIN. What is being tested
    # is that the engine starts, says what is missing and quits on its own -
    # which it could not do at all until now, because this was fatal.
    JKX_SMOKE_PLAIN=1
    export JKX_SMOKE_PLAIN
fi

# The sound subsystem has never been started by this bench: every run so far has
# passed s_initsound 0, so a whole client subsystem - mixer, codecs, ambient sets,
# dynamic music - was outside everything it checks. SDL's dummy audio driver gives
# us a device that consumes samples and produces nothing, which is enough to run
# the code. The fixture deliberately has no sound/sound.txt, so this also exercises
# the missing-ambient-sets path.
SOUND_STEP=( +set s_initsound 0 )
if [ "${JKX_SMOKE_SOUND:-0}" = "1" ]; then
    SOUND_STEP=( +set s_initsound 1 )
    export SDL_AUDIODRIVER=dummy
fi

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

# Did it actually start? Xvfb exits immediately if the display number is already
# taken, and the run then attaches to whatever server is already there - at
# whatever size that one was created with. What comes out is a frame of the
# right content at the wrong geometry, so the checks that ask where something is
# fail and the ones that ask whether it is there pass. That is a confusing way
# to lose an afternoon, and it costs one line to rule out.
if ! kill -0 "$XVFB_PID" 2>/dev/null; then
    echo "Xvfb did not start on $DISPLAY_NUM - is another one already using it?" >&2
    exit 2
fi

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
#
# How long to wait after a map change before the frame is worth looking at.
#
# "wait" counts frames, and frames keep ticking while a map loads - the command
# buffer is not held. So a wait placed after +map does not begin after the load,
# it overlaps it, and what is left over on the far side is however many frames
# the load did not use. That remainder is real time divided by frame time, and
# neither is fixed.
#
# Measured, twice, with a screenshot every ten frames after +map jkx_room: the
# world first appeared at frame 80 in one run and at frame 60 in the other, and
# the first frame it appeared in was not yet the settled one (5667 lit pixels,
# then 5833, then 5832 for the rest of the run). The wait here was 80. It sat
# exactly on that edge, so the same command line drew the world in one run and
# a black screen in the next.
#
# Nothing said so. Every picture check steps aside under JKX_SMOKE_PLAIN, so a
# frame the engine never drew passed the run and went on to be compared against
# one that had - which is why the prepass stage reported thousands of changed
# pixels and pointed at the depth pre-pass, and why the fog stage said the floor
# was in the wrong place. One race, two accusations, neither of them true. The
# flat-colour gate further down is the other half of this fix.
#
# The same constant covers the screen wipe, which is the second thing here that
# is timed in milliseconds and waited for in frames. RE_ProcessDissolve runs for
# fDISSOLVE_SECONDS - 0.75 - measured with Sys_Milliseconds2, so how many frames
# it takes is a function of how fast the machine is drawing.
#
# The fog stage caught that the hard way. Its unfogged frame is checked for at
# least 2000 white pixels of floor and it had "+wait 90" in front of it, which
# put the screenshot inside the wipe. Measured with a screenshot every twenty
# frames after setviewpos: 1016 white pixels, 1156, 1160, 1654, 4824, 6090,
# 6573, 6873, 6853, 7002, 7022, 7022. The threshold falls between the fourth
# and the fifth sample. Run on its own the stage landed above it; run after
# twelve other stages it landed below, twice, and read as a rendering fault in
# a commit that had already been shown to produce a byte-identical binary.
#
# Two hundred is two and a half times the worst load measured, and by the same
# series the wipe is over by then with room to spare. It is not a proof - the
# only proof would be a fence the engine does not offer - but it is a margin
# with numbers behind it instead of a number that happened to work.
SETTLE=200

INMAP_STEP=( +wait 20 +map jkx_room +wait $SETTLE
             +screenshot_tga jkx_inmap )



# The crosshair, twice: once absent, once drawn large enough that nothing else
# in the frame can outweigh it. What is being measured is where it lands, and it
# has to be measured this way because the crosshair is white and so is the
# fixture's floor - no colour tells them apart, only the fact that one of the
# two frames has it.
#
# Two consecutive frames from a standing position is the same construction the
# fog stage uses, and the wait is what makes it stable: the shot is queued and
# the file appears a frame later.
INMAP_STEP+=( +cg_drawCrosshair 0 +wait 10 +screenshot_tga jkx_nocross
              +cg_crosshairDotSize 48 +cg_drawCrosshair 1 +wait 10
              +screenshot_tga jkx_cross
              +cg_crosshairDotSize 3 )
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

# Vertex animation, which is the one kind of animation this bench has never
# drawn - every model in the fixture had a single frame until make_test_md3.py.
#
# testmodel puts a model a hundred units in front of the camera; given a second
# argument it sets frame 1, oldframe 0 and that backlerp, so 1 is the first
# frame and 0 is the second. The square is in a different place in each, so the
# two shots have to differ. A build whose vertex buffer holds one frame draws
# the same picture twice, and that is exactly what this engine did.
#
# Not in the fog lane, and that is an open question rather than a tidy-up. With
# these four steps added, a fog run stops responding at the end - stuck inside
# the driver in vk_present_frame, on the frame after the last screenshot, three
# times out of three, while the same run without them passes. The test model is
# cleared long before the fog steps begin and the fog lane is the only one that
# hangs, so something about drawing a mesh through the batch path leaves state
# that the fog pass then chokes on. That is a real finding and it is written
# down in the backlog rather than left as a flaky stage; this check runs in the
# other seven lanes meanwhile.
if [ "${JKX_SMOKE_FOG:-0}" != "1" ]; then
INMAP_STEP+=( +testmodel models/jkx/anim.md3 1 +wait 30 +screenshot_tga jkx_md3_a
              +wait 10
              +testmodel models/jkx/anim.md3 0 +wait 30 +screenshot_tga jkx_md3_b
              +wait 10 +testmodel +wait 10 )
fi

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
                  +load jkx_save +wait $SETTLE +screenshot_tga jkx_loaded )
else
    # Then turn around. This is not padding: for as long as this step existed it
    # only ever looked along +Y, and +Y is the one face out of six where the sky
    # box's parameterisation and the cube map's agree. A cubemap built with the
    # wrong table therefore passed every assertion in this file while five of its
    # six faces were rotated or mirrored, and it took a pixel comparison against
    # the box path to see it.
    #
    # setviewpos needs cheats, which in this engine is a cvar called helpUsObi,
    # and it subtracts 25 for eye height - so z is 25 to stand where the spawn is.
    # It takes a yaw and no pitch, so up and down are still not covered here;
    # tests/sky_projection_test.cpp covers all six, without a renderer.
    # And the screen wipe, held still over this load rather than over the first
    # one - because this is the only load in the run where the two pictures
    # differ.
    #
    # A wipe blends the screen it captured against the screen being drawn now,
    # so a check on it is only a check when those two are different. The first
    # map load has a grey load screen on one side and a grey room on the other:
    # both are the clear colour, 191 in every channel, and the blend between
    # them is 191 at every alpha. The measurement below read that frame as flat
    # and reported a one pixel edge - against a build whose ramp was measured at
    # 128 pixels by hand. It was reading a picture with nothing in it.
    #
    # Here the old screen is the load screen and the new one is the sky room,
    # which is green along the middle row. 191 to 0 across the band is a ramp
    # with somewhere to go.
    #
    # r_dissolveFreeze holds the wipe at one percentage instead of running it,
    # so the picture does not depend on how long the load took - with the
    # validation layer on it takes long enough that a shot a fixed number of
    # frames later catches the wipe already over. r_dissolveType pins which of
    # the six it is, so the boundary is a vertical line and the middle row
    # crosses it.
    #
    # The wait after the screenshot is not padding either: a screenshot is
    # queued and lands a frame or two later, so a cvar changed on the next line
    # changes the frame that gets written. That is measurable - the shots in the
    # run that found this were each one step ahead of the value they were named
    # after.
    INMAP_STEP+=( +set r_dissolveType 1 +set r_dissolveFreeze 45
                  +wait 20 +map jkx_room2 +wait $SETTLE
                  +screenshot_tga jkx_wipe +wait 20
                  +set r_dissolveFreeze -1 +wait 20
                  +screenshot_tga jkx_sky +wait 20 )

    # The turned views belong to the sky, and are skipped when there is no sky.
    # That is not a dodge around frames that would not compare: a plain run draws
    # no sky, so three more headings are three more chances for the camera to be
    # a pixel from where it was last time and nothing else at all.
    if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ]; then
        INMAP_STEP+=( +setviewpos 0 0 -15 0   +wait 90 +screenshot_tga jkx_sky_rt +wait 10
                      +setviewpos 0 0 -15 180 +wait 90 +screenshot_tga jkx_sky_lf +wait 10
                      +setviewpos 0 0 -15 270 +wait 90 +screenshot_tga jkx_sky_ft +wait 10 )

    if [ "${JKX_SMOKE_FOG:-0}" = "1" ]; then
        # The fog, measured against itself.
        #
        # Two frames from one standing position, seconds apart, differing by one
        # cvar. That shape is the point: comparing a fog frame from one run against a
        # no-fog frame from another run is comparing two camera positions as well,
        # and this bench has already spent an afternoon on exactly that mistake
        # (JKX_SMOKE_PLAIN, and the note above it). Within a single run, with the
        # player settled and the view pinned, the only thing that changes between
        # these two screenshots is r_drawfog.
        #
        # The view is pinned here the same way a plain run pins it: no trailing
        # third-person camera, no view bob. Those move on real time, and real time is
        # what the frame counter is not.
        #
        # r_drawfog is 0 for the whole run otherwise, because a global fog repaints
        # the clear colour and washes every other colour check in this file.
        INMAP_STEP+=( +cg_thirdPerson 0
                      +cg_bobup 0 +cg_bobpitch 0 +cg_bobroll 0
                      +setviewpos 0 0 -15 90 +wait $SETTLE
                      +screenshot_tga jkx_nofog +wait 15
                      +r_drawfog 1 +wait 20 +screenshot_tga jkx_fog +wait 15
                      +r_drawfog 0 +wait 10 )
    fi
    fi
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

# A model with two frames in it, and the only one in this fixture that has more
# than one. That is not a detail: the vertex buffer built for an MD3 holds a
# single frame, so every animated model was drawn frozen at its first - and
# nothing here had a second frame to be frozen at. See make_test_md3.py.
mkdir -p "$RUN/base/models/jkx"
python3 "$HERE/make_test_md3.py" "$RUN/base/models/jkx/anim.md3" >/dev/null

# The screen wipe used to need a mask picture here, and without it the engine
# printed "no screen wipe" and skipped the whole dissolve path - which is how a
# crash lived there undisturbed: R_DissolveCaptureScreen compared image_t::width,
# the size that was asked for, against the capture size, when what it had built
# was uploadWidth - the size after clamping to glConfig.maxTextureSize, which is
# 2048. Wider than that, the two disagree and it uploads sixteen times more
# texels than the image holds. The wide lane runs at 2560, so it is the one that
# reaches it; below 2048 nothing is clamped and nothing goes wrong.
#
# The boundary is geometry now, so there is no file to generate and no
# configuration in which the path is skipped. That is the better state: the
# capture above is exercised by every run rather than by every run that
# remembered to write a fixture.
# The sky room's floor carries a texture wider than the renderer used to keep.
# See the shader, and the require below.
python3 "$HERE/make_test_sky.py" --wide "$RUN/base/textures/jkx/wide.tga" >/dev/null
# Both rooms are generated. jkx_room.bsp used to be a committed copy of this
# generator's output, and the two drifted the moment the generator learned to
# write a light grid: the run kept loading a map without one and the check for it
# failed while the generator's own self-test passed. A generated artifact that is
# also committed is two sources of truth.
#
# jkx_smoke.bsp stays committed, because it is not this generator's output - it
# is a deliberately truncated file, and the point of it is to be broken.
python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room.bsp" >/dev/null
# The fog is opt-in, and that is not shyness. A global fog repaints the clear
# colour for the whole map - RB_BeginDrawingView clears to it once there is a
# scene - so putting one in the shared fixture moves every other colour check in
# this file. It gets its own lane instead: JKX_SMOKE_FOG, and the "fog" stage in
# tools/ci/local.sh.
FOG_ARGS=()
if [ "${JKX_SMOKE_FOG:-0}" = "1" ]; then
    FOG_ARGS=( --fog textures/jkx/fog )
fi
python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room2.bsp" \
    --shader textures/jkx/wide --sky textures/jkx/sky \
    "${FOG_ARGS[@]}" >/dev/null

# Extra cvars, as "name=value name=value". This exists for A/B runs: two passes
# of the same fixture that differ by one setting, compared pixel for pixel. The
# cubemap sky was landed that way, and the comparison is what showed that five
# of its six faces were rotated - the fixture's camera looks along +Y, which is
# the one face the two code paths already agreed about, so every assertion in
# this file passed on a sky that was wrong everywhere else.
#
# Note that a latched cvar only takes here because it is set on the command
# line before the renderer starts.
SET_STEP=()
for pair in ${JKX_SMOKE_SET:-}; do
    SET_STEP+=( +set "${pair%%=*}" "${pair#*=}" )
done

# JKX_SMOKE_PLAIN draws the scene and only the scene - no sky, no interface -
# and asserts nothing about the picture. It exists for comparing one run against
# another rather than for checking either one on its own, and everything it
# removes was measured before it was removed:
#
#   the sky sits at the far plane, so a hundredth of a degree of view angle left
#   over after the camera has otherwise settled is worth several pixels. Two runs
#   of the identical binary differ by about four per cent of the frame on the sky
#   views and by nothing at all anywhere else. That was chased for an hour as if
#   it were a defect in the renderer;
#
#   the interface is drawn against real time rather than against the frame
#   counter - the console cursor blinks, and the fixture's own head-up display
#   moves - so two runs disagree on a few hundred pixels of it however carefully
#   the camera is pinned.
#
# Neither is a reason to widen the tolerance on a frame comparison. A tolerance
# wide enough to swallow a blinking cursor is wide enough to swallow a wall.
# The console frame is the text check, and its cursor blinks on real time rather
# than on the frame counter - thirty-four pixels that differ between any two runs
# and have nothing to do with anything being rendered. A plain run is for
# comparing scenes, so it does not open the console at all.
CONSOLE_STEP=( +toggleconsole +wait 30 +screenshot_tga jkx_console +wait 20 +toggleconsole )
if [ "${JKX_SMOKE_PLAIN:-0}" = "1" ]; then
    CONSOLE_STEP=()
fi

if [ "${JKX_SMOKE_PLAIN:-0}" = "1" ]; then
    SET_STEP+=( +set r_fastsky 1 +set cg_draw2D 0
                +set cg_thirdPerson 0
                +set cg_bobup 0 +set cg_bobpitch 0 +set cg_bobroll 0
                +set cg_runpitch 0 +set cg_runroll 0 )
fi

set +e
( cd "$RUN" && \
  DISPLAY="$DISPLAY_NUM" \
  XDG_RUNTIME_DIR="$RUN/xdg" \
  VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}" \
  timeout -k 10 "${JKX_SMOKE_TIMEOUT:-600}" "./$(basename "$ENGINE")" \
      +set fs_basepath "$RUN" +set fs_homepath "$RUN/home" \
      "${SOUND_STEP[@]}" +set com_errorDialog 0 +set con_notifytime 0 \
      +set cg_hudFiles ui/jkx_hud.txt +set g_char_model jkx \
      +set helpUsObi 1 +set r_drawfog 0 \
      "${SET_STEP[@]}" \
      +wait 60 +screenshot_tga jkx_smoke \
      "${CONSOLE_STEP[@]}" \
      +wait 20 +map jkx_smoke +wait 12 +screenshot_tga jkx_wiping \
      +wait $SETTLE +screenshot_tga jkx_wiped \
      "${INMAP_STEP[@]}" \
      +imagelist +wait 20 +quit ) > "$RUN/run.log" 2>&1
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

# The map's light grid has to actually load. Both of its lumps used to be empty
# in the generated map, which fails the size check in R_LoadLightGridArray and
# throws the grid away - so every headless run lit its models by the fall-back
# and never once executed R_SetupEntityLightingGrid, the eight-way interpolation
# every model in every real map goes through. The warning this forbids is the
# one that branch now prints.
forbid 'the grid is being discarded'

# There is one renderer and it is inside the engine, so this is a check that it
# started rather than a check on which one started.
if ! grep -q -- 'renderer: rd-vulkan, built in' "$RUN/run.log"; then
    report "the log does not say the Vulkan renderer came up"
fi

require 'selected physical device'
require 'VK_RENDERER:'
require 'selected presentation mode'
require 'Common Initialization Complete'
if [ "${JKX_SMOKE_NO_SHADERS:-0}" = "1" ]; then
    require 'no .shader files found'
fi
if [ "${JKX_SMOKE_SOUND:-0}" = "1" ]; then
    # The device opened. SNDDMA_Init prints this only after SDL_OpenAudioDevice
    # returned a handle, so a driver that failed to come up would not reach it.
    require 'SDL audio driver is "dummy"'
    # And the ambient set language, absent from the fixture, is a warning rather
    # than the ERR_FATAL it was: the run has to get past it to reach the map.
    require 'no ambient sound sets in sound/sound.txt'
fi
require 'Wrote screenshots/jkx_smoke.tga'
[ "${JKX_SMOKE_PLAIN:-0}" = "1" ] || require 'Wrote screenshots/jkx_console.tga'
require 'Wrote screenshots/jkx_wiped.tga'

# The second map is a real one, and this is the half of the run that goes
# through the server: CM_LoadMap on a BSP that parses, SV_InitGameProgs, entity
# spawn, a client connecting to a local server, a player model, and cgame
# drawing a world view with a head-up display over it. Everything above this
# line is the menu. Eight of the defects found so far were only reachable from
# here, six of them reads or writes out of bounds.
SHOTS=( "jkx_smoke 2" "jkx_console 200" "jkx_wiped 2" )
if [ "${JKX_SMOKE_PLAIN:-0}" = "1" ]; then
    SHOTS=( "jkx_smoke 2" "jkx_wiped 2" )
fi
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
if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ] && [ -f "$RUN/home/base/screenshots/jkx_inmap.tga" ]; then
    if ! python3 "$HERE/tga_colour_where.py" \
        "$RUN/home/base/screenshots/jkx_inmap.tga" \
        "255,255,255@0.3,0.75,0.7,1.0"; then
        report "the map's floor is not where it should be in jkx_inmap.tga"
    fi
fi

# The wipe's boundary is a ramp rather than a step. See tga_soft_edge.py; the
# bar is forty and the band is a tenth of the screen, so there is room either
# side of it.
#
# The frame only exists when the sky map was loaded - the save-and-load lane
# stops after the first map, and the first map has nothing to wipe to. Tested by
# the file rather than by repeating the condition that produced it, so that a
# change to the run order cannot leave this checking a stale frame from the run
# before it: the run directory is new every time.
if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ] && [ -f "$RUN/home/base/screenshots/jkx_wipe.tga" ]; then
    if ! python3 "$HERE/tga_soft_edge.py" 40 \
        "$RUN/home/base/screenshots/jkx_wipe.tga"; then
        report "the screen wipe's edge is a step, not a ramp"
    fi
fi

# Vertex animation moves the model. The box is the whole frame rather than a
# corner of it, because where the square lands depends on the camera and the
# question here is only whether the two frames are different pictures. Sixteen
# hundred pixels is a fifth of the square's area at this distance - enough that
# a stray pixel of dithering cannot pass, little enough that the exact placement
# does not matter.
if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ] \
   && [ -f "$RUN/home/base/screenshots/jkx_md3_a.tga" ] \
   && [ -f "$RUN/home/base/screenshots/jkx_md3_b.tga" ]; then
    if ! python3 "$HERE/tga_diff_where.py" \
        "$RUN/home/base/screenshots/jkx_md3_a.tga" \
        "$RUN/home/base/screenshots/jkx_md3_b.tga" \
        "0.0,0.0,1.0,1.0" 1600; then
        report "vertex animation does not move the model between its two frames"
    fi
fi

# The crosshair is a disc at the centre of the window, on every shape of window.
#
# The box is tight because there is nothing approximate about the answer: the
# crosshair is either at the middle of the screen or at the middle of something
# else. The pixel count is the shape. At this window size a 48-unit dot is 72
# pixels across, so a square one covers 5184 of them and a disc covers pi/4 of
# that, about 4070 - and about 3630 of those clear the tool's difference
# threshold, the rest being the feathered edge. The band is measured rather than
# derived, and it excludes both a square of that size and nothing at all: the
# square version of this crosshair scored 5605.
if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ] \
   && [ -f "$RUN/home/base/screenshots/jkx_cross.tga" ] \
   && [ -f "$RUN/home/base/screenshots/jkx_nocross.tga" ]; then
    if ! python3 "$HERE/tga_diff_where.py" \
        "$RUN/home/base/screenshots/jkx_nocross.tga" \
        "$RUN/home/base/screenshots/jkx_cross.tga" \
        "0.47,0.47,0.53,0.53" 2800 4600; then
        report "the crosshair is not a centred disc"
    fi
fi

# The fog pass, measured in both directions against the same floor.
#
# RB_FogPass had never been reached by a headless run - the generated map had no
# fogs and the retail maps are not in this repository - so a second blended pass
# over every fogged surface, its shader permutation and its texture coordinate
# generation were all unexecuted. They run now, including under the sanitizers
# and the validation layer.
#
# The two frames are one standing position seconds apart, differing by r_drawfog
# alone, so the floor is the same floor in both. Without fog it is the unfogged
# white of its own texture; with fog it is not, because the fog colour is one
# nothing else in the fixture uses.
#
# The numbers are not arbitrary. The floor comes out around (245, 168, 245),
# which is what the blend asks for: the fog is (0.9, 0.1, 0.9) and at this
# distance its alpha is about 0.38, so red is 0.898 * 0.38 + 1 * 0.62 = 0.96,
# and green is 0.098 * 0.38 + 0.62 = 0.66. Asserting the white rather than the
# blend keeps the check independent of where exactly the camera stands, which is
# the one thing about this bench that has repeatedly turned out not to be fixed.
if [ "${JKX_SMOKE_FOG:-0}" = "1" ] && [ -f "$RUN/home/base/screenshots/jkx_fog.tga" ]; then
    if ! python3 "$HERE/tga_has_colour.py" \
        "$RUN/home/base/screenshots/jkx_nofog.tga" 255,255,255:2000 >/dev/null; then
        report "the floor is not unfogged white in jkx_nofog.tga - nothing to compare against"
    fi
    if python3 "$HERE/tga_has_colour.py" \
        "$RUN/home/base/screenshots/jkx_fog.tga" 255,255,255:2000 >/dev/null 2>&1; then
        report "the floor is still unfogged white in jkx_fog.tga - the fog pass did nothing"
    fi
fi

# The texture ceiling, checked against a texture rather than against the number
# the renderer prints about itself.
#
# The sky room's floor is 4096 texels wide. The renderer used to clamp every
# texture to 2048 - not because of hardware, which reports 16384 here, but
# because maxTextureSize was derived from the block size of an image memory
# allocator that had already been replaced by VMA, and because ResampleTexture
# kept its column tables in a fixed array of that size. Under that ceiling this
# line reads 2048 and 32, and nothing anywhere says a texture was halved.
#
# imagelist is asked for at the end of the run for this one line. Only the lanes
# that reach the sky room have loaded the texture at all - the savegame lane
# spends its second map on a save and load round trip instead.
if [ -f "$RUN/home/base/screenshots/jkx_sky.tga" ] &&
   ! grep -qE "4096 +64 +RGBA.*textures/jkx/wide" "$RUN/run.log"; then
    report "the 4096-wide floor texture was not kept at 4096 - the texture ceiling is back"
    grep -E "textures/jkx/wide" "$RUN/run.log" | tail -2
fi

if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ] && [ -f "$RUN/home/base/screenshots/jkx_sky.tga" ]; then
    if ! python3 "$HERE/tga_colour_where.py" \
        "$RUN/home/base/screenshots/jkx_sky.tga" \
        "0,153,0@0.3,0.2,0.7,0.8" "0,0,0@0.0,0.3,1.0,0.7"; then
        report "the sky is not the face it should be, or not the way up it should be"
    fi
fi

# The other three headings. Which image each one shows is sky_texorder at work:
# the face along +X is "rt", along -X is "lf" and along -Y is "ft", and the two
# that get swapped by that table are exactly the pair a naive reading gets
# backwards.
#
# The black stripe is asserted too, and it is the orientation half of the check:
# the base colour alone says which image was sampled, not which way up it went.
# Each face carries its stripe down one edge, so on screen the stripe is a
# vertical band and its centre of mass sits at mid height. Turn any face a
# quarter turn and that band lies down: the stripe runs along the top or bottom
# edge of the face instead, which either pushes the centroid out of the middle
# band or takes it off screen entirely. Either way this fails, which is what it
# is for - the colours passed on a sky whose faces were all quarter-turned.
#
# A half turn is not caught here, because two stripes are on screen at once and
# a half turn swaps them. tests/sky_projection_test.cpp checks the corners
# against the specification by hand and does catch it.
sky_view() {
    local shot="$RUN/home/base/screenshots/$1.tga"; shift
    local what="$1"; shift
    if [ ! -f "$shot" ]; then
        report "no $what screenshot: the camera was not turned"
        return
    fi
    if ! python3 "$HERE/tga_colour_where.py" "$shot" "$@"; then
        report "the $what sky face is the wrong one, or the wrong way up"
    fi
}

if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ] && [ -f "$RUN/home/base/screenshots/jkx_sky.tga" ]; then
    sky_view jkx_sky_rt "+X (rt)" "204,0,0@0.3,0.2,0.7,0.8"   "0,0,0@0.0,0.3,1.0,0.7"
    sky_view jkx_sky_lf "-X (lf)" "0,51,255@0.3,0.2,0.7,0.8"  "0,0,0@0.0,0.3,1.0,0.7"
    sky_view jkx_sky_ft "-Y (ft)" "255,204,0@0.3,0.2,0.7,0.8" "0,0,0@0.0,0.3,1.0,0.7"
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
if [ "$GAME_ID" = "jka" ]; then
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
    if [ "$GAME_ID" = "jka" ]; then
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
    [ "${JKX_SMOKE_PLAIN:-0}" = "1" ] && break
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

# The one check a plain run still owes.
#
# JKX_SMOKE_PLAIN turns off the sky, the interface, the third-person camera and
# the view bob, which is what makes two runs comparable pixel for pixel. It also
# steps around every loop above, and until this block there was nothing left in
# the run that looked at a scene frame at all. A plain run could therefore write
# five files the engine had drawn nothing into and report success.
#
# It did. See the note on SETTLE. The thresholds up there are tuned for a frame
# with a head-up display over it and mean nothing here, but the floor does not
# need tuning: a scene frame that drew has a floor and a wall in it, and one
# that did not is a single flat colour. Two is enough to tell them apart, and
# two is all this claims to check.
if [ "${JKX_SMOKE_PLAIN:-0}" = "1" ]; then
    for name in jkx_smoke jkx_wiped jkx_inmap; do
        SHOT="$RUN/home/base/screenshots/$name.tga"
        [ -f "$SHOT" ] || continue
        if ! python3 "$HERE/tga_is_a_picture.py" "$SHOT" 2 >/dev/null; then
            report "$name.tga is one flat colour - the scene never drew"
        fi
    done
fi

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
    [ "${JKX_SMOKE_PLAIN:-0}" = "1" ] && break
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
    [ "${JKX_SMOKE_PLAIN:-0}" = "1" ] && break
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
