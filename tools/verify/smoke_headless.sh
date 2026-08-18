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

# The item and weapon tables, written for THIS game rather than shipped as one
# file for both.
#
# They used to be checked in under fixtures/base, one copy shared by jka and
# jk2, hand written, and covering the weapons the fixture map happened to need.
# That was fine until a real map asked for one it did not have and the level
# died on "Couldn't find item for weapon 27" - see the long note in
# make_item_tables.py. The two games also have different WP_ enums, so a shared
# file was wrong for one of them by construction.
python3 "$HERE/make_item_tables.py" --game "$GAME_ID" --out "$RUN/base/ext_data" \
    >/dev/null

# The lane that reproduces the character-skin handle offset.
#
# Two halves, and both have to be here or the number does not appear.
#
# The first is a loading screen that registers four skins before the map
# registers its first - ui/jkx_loadscreen.menu, and the long note at the top of
# it is the reason. Without it index and handle agree exactly and there is
# nothing to see.
#
# The second is A CHARACTER THE MAP OWNS. That was the missing half for a while
# and it is worth stating plainly: JKX_SMOKE_CHAR is not enough, because the
# player connects AFTER cgame has initialised. His skin reaches the
# CS_CHARSKINS configstrings after the loop in CG_RegisterGraphics has already
# run, so cgs.skins stays empty and the comparison never happens. Measured: with
# JKX_SMOKE_CHAR alone there is not one line about skins in the whole engine
# log. A map's NPC is spawned with the rest of the entities, before any of that,
# which is what every retail level does.
#
# So: a generated map with two NPC_spawner entities in it, and
# ext_data/npcs/jkx.npc to say what they wear. Two, because the defect is an
# OFFSET and one number cannot tell a constant shift from a drift.
if [ "${JKX_SMOKE_SKINSHIFT:-0}" = "1" ]; then
    python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_skin.bsp" \
        --npc jkx --npc jkx2 >/dev/null
    JKX_SMOKE_MAP=jkx_skin
    export JKX_SMOKE_MAP

    # ui/ingame.txt, and the name is not a choice: ui_main.cpp hardcodes it for
    # the in-game load, which is the only menu set loaded AFTER the renderer is
    # restarted for the map.
    #
    # That timing is the whole of it and it cost a run to learn. Single player
    # tears the renderer down and calls R_Init again between the main menu and
    # the map - "----- R_Init -----" appears in every log right after "Game
    # Initialization" - and R_Init calls R_InitSkins, which sets tr.numSkins
    # back to one. Anything the MAIN menu set registered is gone by then. A
    # loading screen put in ui_menuFiles therefore registers four skins and then
    # has all four thrown away before the map registers its first, which is
    # exactly what happened: the two skins that fail printed their names, twice,
    # and the offset was still zero.
    #
    # Written here rather than committed under fixtures/base, because this file
    # is loaded unconditionally by every run that loads a map. Committed, it
    # would put the offset into every lane at once.
    printf '{\n\tloadmenu\n\t{\n\t\t"ui/jkx_loadscreen.menu"\n\t}\n}\n' \
        > "$RUN/base/ui/ingame.txt"
fi

# JKX_SMOKE_MAP loads a different map instead of the fixture's.
#
# It only makes sense with JKX_SMOKE_EXTRA_BASE, and the pair is how a RETAIL map
# gets onto this bench: point the extra base at an unpacked copy in /tmp and name
# a map inside it. Nothing about it is committed and no lane sets it - a retail
# install is not redistributable - so the default run is unchanged and a machine
# with no copy of the game is unaffected.
#
# It forces JKX_SMOKE_PLAIN, and that is not laziness. Every picture assertion
# below is a statement about the fixture map - this many pixels of that colour at
# that place - and a real map satisfies none of them. What is left is what a real
# map is actually being asked: does it load, does the game library spawn its
# entities, do the scripts run, does the frame come out. Those are all in the log
# and in whether the run survives to +quit.
#
# It was worth building. The first two maps run through it - yavin1 out of Jedi
# Academy and kejim_post out of Outcast - found two defects nothing in this bench
# could reach, one of which stopped every ICARUS script in both games from
# loading at all on Linux. Neither map needs its textures for that: a map that
# loads with no texture in sight still answers whether the world parsed, whether
# the entities spawned, whether the scripts ran and whether a frame came out.
#
#   JKX_SMOKE_EXTRA_BASE=/tmp/retail/jka JKX_SMOKE_MAP=yavin1 \
#   JKX_SMOKE_DISPLAY=:70 tools/verify/smoke_headless.sh BUILD_DIR
if [ -n "${JKX_SMOKE_MAP:-}" ]; then
    JKX_SMOKE_PLAIN=1
    export JKX_SMOKE_PLAIN
fi

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
#
# Two variables and not one. VK_LOADER_LAYERS_ENABLE is the current spelling and
# the loader has only honoured it since 1.3.234; VK_INSTANCE_LAYERS is the old
# one, deprecated and still obeyed everywhere. On a loader too old for the first
# there is no error and no layer - the run comes back silent, and silent is
# exactly what a clean run looks like. That is the shape of defect this bench
# keeps finding in itself, so it is worth the second line: a check that cannot
# fail loudly should at least not fail quietly.
VALIDATION=0
if [ -f /usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json ] &&
   [ "${JKX_SMOKE_NO_VALIDATION:-0}" != "1" ]; then
    VALIDATION=1
    export VK_LOADER_LAYERS_ENABLE='*validation*'
    export VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
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

# Vertical sync changed while the game is running - the first rung of the ladder
# that replaces vid_restart. See the note in RE_BeginFrame.
#
# A CONFIG FILE AND NOT COMMAND-LINE GROUPS. Two spellings were tried first and
# both fail the same way, neither loudly.
#
# "+r_swapInterval 1" - a bare cvar name is a console command but not a
# command-line group - and "+set r_swapInterval 1" - Com_StartupVariable scans
# the command line for set commands and consumes them before the buffer runs.
# Either way a token goes missing and everything after it shifts by one. What
# that looks like is not an error: screenshot_tga executes without its name, so
# a file called shot<date>.tga appears where jkx_sky_ft.tga should be, and then
# +quit is swallowed as somebody's argument and the engine draws frames forever.
#
# Three timeouts were raised before the two logs were compared side by side. The
# run had done all of its work in fifty seconds and then never stopped: 1495
# lines and a CL_Shutdown in the baseline against 47849 lines and no CL_Shutdown
# here. A SLOW RUN AND A BROKEN RUN LOOK IDENTICAL until you ask whether it
# finished rather than how long it took.
#
# Inside a config file the commands execute where they are written, which is
# what the cutscene-skip test already does. The value goes 0 -> 1 -> 0 so the
# run ends where it started and the frames after it stay comparable with every
# other lane's.
if [ "${JKX_SMOKE_VSYNC:-0}" = "1" ]; then
    {
        echo "wait 5"
        echo "set r_swapInterval 1"
        echo "wait 12"
        echo "screenshot_tga jkx_vsync_on"
        echo "wait 5"
        echo "set r_swapInterval 0"
        echo "wait 12"
        echo "screenshot_tga jkx_vsync_off"
        echo "wait 5"
    } > "$RUN/base/jkx_vsync.cfg"

    INMAP_STEP+=( +exec jkx_vsync.cfg +wait 45 )
fi

# The window resized while the game is running - the second rung, AND THIS LANE
# IS RED ON PURPOSE. It is not in the stage list in tools/ci/local.sh.
#
# What it found on its first run, which is exactly what a lane is for: the
# window resizes and the renderer does not follow it.
#
#   resizing the window to 800 600
#   vkCreateSwapchainKHR(): pCreateInfo->imageExtent (1280, 720), which is
#   outside the bounds returned by vkGetPhysicalDeviceSurfaceCapabilitiesKHR():
#   currentExtent = (800,600), minImageExtent = (800,600)
#   vkCmdBlitImage(): srcOffsets[1].x is 1280 which exceed srcSubresource width
#   extent (800)
#
# So SDL resized the window, the surface knows its new size, and the renderer
# rebuilt the swapchain at the OLD extent and then blitted a 1280-wide region
# out of an 800-wide image.
#
# HALF OF THAT IS FIXED. The extent came from a floor that never moved:
# vk_create_swapchain clamps the extent up to gls.windowWidth, which was set
# once at startup to the size the window was launched at. The clamp is there for
# minimisation - a minimised window reports zero and a zero swapchain crashes
# the driver - and the comment beside it says in as many words that something
# more dynamic would be needed if windowed resizing were ever implemented. The
# floor moves with the window now and the vkCreateSwapchainKHR error is gone.
#
# WHAT IS LEFT is the blit, still asking for a 1280-wide region out of an image
# that is now 800 wide. Its region comes from glConfig.vidWidth, which IS 800 by
# then - so the frame being complained about was recorded before the rebuild and
# is referring to attachments that have since been replaced. That points at the
# ORDER of the rebuild rather than at any one number: RE_BeginFrame is early in
# the frame but not before everything. Next step is to find what is already
# recorded by the time it runs.
#
# The rung stays in - the cvars are unlatched, WIN_Resize works, the swapchain
# is rebuilt - because all of that is correct and none of it is what is wrong.
# What is missing is one number in the renderer, and the lane names it.
#
#
# The observable is the SIZE OF THE PICTURE. A screenshot is the window, so a
# resize that worked produces a smaller file with different dimensions, and one
# that silently did nothing produces the same 1280x720 as everything else. That
# is a stronger check than a log line because it cannot pass while the frame is
# wrong.
#
# 800x600 and back, so the run ends at the geometry every other check is written
# against. r_mode -1 is the custom size; the modes below it are a fixed table.
if [ "${JKX_SMOKE_RESIZE:-0}" = "1" ]; then
    {
        echo "wait 5"
        echo "set r_customwidth 800"
        echo "set r_customheight 600"
        echo "set r_mode -1"
        echo "wait 25"
        echo "screenshot_tga jkx_resized"
        echo "wait 10"
        echo "set r_customwidth 1280"
        echo "set r_customheight 720"
        echo "set r_mode -1"
        echo "wait 25"
        echo "screenshot_tga jkx_resized_back"
        echo "wait 10"
    } > "$RUN/base/jkx_resize.cfg"

    INMAP_STEP+=( +exec jkx_resize.cfg +wait 90 )
fi


# The map's own furniture, photographed twice: as early as a frame can be had,
# and again once everything has settled.
#
# Two frames rather than one, because the question is not whether a prop draws.
# It is WHEN. A misc_model_breakable gets its model through G_ModelIndex, which
# is a configstring index, and cgame turns that index into a renderer handle in
# two different places: the loop in CG_RegisterGraphics, which runs behind the
# loading screen, and CG_ConfigStringModified, which runs when a configstring
# arrives afterwards. A prop that goes through the first is there when the level
# appears. A prop that goes through the second arrives late, visibly, in front
# of the player - which is what was reported from hardware about the tree in the
# first mission, and about characters missing from cutscenes.
#
# So the early shot is the measurement and jkx_inmap, the settled frame of the
# same map, is its control. The
# prop is cyan and nothing else in this fixture is; if the colour is in the late
# frame and not in the early one, the pop-in is reproduced without a retail map,
# and if it is in both then whatever is happening on hardware is not this.
#
# Thirty frames rather than zero, and the number is measured. Shots every few
# frames after the map command give, in cyan pixels of prop and white pixels of
# floor:
#
#     +6    prop 0      floor 0          the screen wipe, covering everything
#     +16   prop 5749   floor 140031     the wipe clearing
#     +30   prop 5776   floor 271225     and from here to +646, unchanged
#
# That series was taken on an idle machine. Under CI load the same thirty frames
# landed at 5700 rather than 5776 - the wipe was not quite finished - and the
# equality below failed on seventy-six pixels of edge. So the number is sixty,
# not thirty: still a fraction of the two hundred the settled shot waits, and
# far enough past the wipe that how busy the machine is does not decide it.
#
# The floor under this is the wipe itself. Nothing can be measured while the
# screen is covered, so "as early as possible" means "as early as the wipe
# allows", and pinning an equality to an instant that moves with machine load is
# the same mistake the sky frame in the depth pre-pass lane cost a day on.
#
# What the pair says as it stands: on this bench a prop the MAP owns is drawn in
# the same frame the floor is, and the two counts are equal at thirty frames and
# at two hundred. The pop-in reported from hardware is therefore NOT the plain
# configstring path - a model named in a map entity goes through
# CG_RegisterGraphics behind the loading screen and is ready when the level
# appears. Whatever arrives late there arrives by some other route, and the
# candidates left are an entity a script spawns after the level has started,
# whose G_ModelIndex lands after that loop has run, or a model list long enough
# for something in it to be missed. Neither is reachable from a fixture with two
# models in it and no scripts; this lane is what will notice if the route that
# DOES work stops working.
#
# The early shot goes INSIDE the load this file already does rather than after a
# second one of its own. That was the first arrangement and it cost the lane
# more than three minutes of wall clock - a whole extra map load and settle, on
# a software rasteriser - for a frame that was already available. The settled
# shot needs no arrangement at all: jkx_inmap IS the settled frame of this map.
if [ "${JKX_SMOKE_MAPENT:-0}" = "1" ]; then
    INMAP_STEP=( +wait 20 +map jkx_room
                 +wait 60 +screenshot_tga jkx_prop_early
                 +wait $SETTLE +screenshot_tga jkx_inmap )
fi

# Two shots of the same settled scene, separated in time rather than in space.
#
# tcMod is a function of the clock, so one frame of it says nothing: a scrolled
# texture and an unscrolled one look equally plausible in a single picture. The
# measurement is the DIFFERENCE between moments, and everything else in the
# frame is pinned and static, which is what makes that difference attributable.
#
# THREE shots, at 137 frames and then another 83, and both of those numbers are
# the result of a measurement rather than a preference. A tcMod is periodic and
# a rate whose period divides the sampling interval comes back to exactly where
# it started: at 0.5 per second the scroll moved 55 pixels between one pair of
# shots and 0 between the next, at the same rate, because the engine's clock
# does not advance in step with the frame counter. Two unequal intervals and odd
# rates mean aliasing would have to happen on both at once.
if [ "${JKX_SMOKE_TCMOD:-0}" = "1" ] || [ "${JKX_SMOKE_DEFORM:-0}" != "0" ]; then
    INMAP_STEP=( +wait 20 +map jkx_room
                 +wait $SETTLE +screenshot_tga jkx_inmap
                 +wait 137 +screenshot_tga jkx_tc_later
                 +wait 83 +screenshot_tga jkx_tc_later2 )
fi



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
# Not in the plain lane, and not in the fog lane, for two different reasons.
#
# Plain is the A/B mode: two runs of the same scene compared frame by frame, and
# the depth pre-pass stage is built on it. An animated model does not belong in
# a comparison of two runs - but it did get in, and what it found is worth
# keeping: with r_depthPrepass 1 the second shot differs by 184 pixels, worst
# channel delta 255. That is the pre-pass changing the picture for a mesh drawn
# through the batch path, which is a defect the pre-pass stage exists to catch
# and which nothing could reach while every mesh went through the vertex
# buffer. Written down; r_depthPrepass is off by default.
#
# Fog is the other one, and that is an open question rather than a tidy-up. With
# these four steps added, a fog run stops responding at the end - stuck inside
# the driver in vk_present_frame, on the frame after the last screenshot, three
# times out of three, while the same run without them passes. The test model is
# cleared long before the fog steps begin and the fog lane is the only one that
# hangs, so something about drawing a mesh through the batch path leaves state
# that the fog pass then chokes on. That is a real finding and it is written
# down in the backlog rather than left as a flaky stage; this check runs in the
# other seven lanes meanwhile.
if [ "${JKX_SMOKE_FOG:-0}" != "1" ] && [ "${JKX_SMOKE_PLAIN:-0}" != "1" ]; then
INMAP_STEP+=( +testmodel models/jkx/anim.md3 1 +wait 30 +screenshot_tga jkx_md3_a
              +wait 10
              +testmodel models/jkx/anim.md3 0 +wait 30 +screenshot_tga jkx_md3_b
              +wait 10 +testmodel +wait 10 )

# The lit one, which is the only picture on this bench where a normal decides a
# colour. One frame, so it can be compared with itself; two quads sharing a
# middle edge whose vertices are duplicated with normals forty degrees apart, so
# there is a seam to close.
#
# Through a config file, because the command line has a limit and this went over
# it: the engine said "were dropped and will not run", one line among several
# hundred, and every screenshot after that point was taken with half the setup
# missing. The bench's own check caught it, which is the second time - the same
# thing happened when the character shots were added. Anything adding more than a
# couple of steps belongs in a cfg from the start.
{
    echo "testmodel models/jkx/seam.md3"
    echo "wait 30"
    echo "screenshot_tga jkx_seam"
    echo "wait 10"
    echo "testmodel"
    echo "wait 10"
    echo "testmodel models/jkx/seam1.md3"
    echo "wait 30"
    echo "screenshot_tga jkx_seam1"
    echo "wait 10"
    echo "testmodel"
} > "$RUN/base/jkx_seam.cfg"
INMAP_STEP+=( +exec jkx_seam.cfg +wait 120 )
fi

# The character, framed so that a person can see what he is wearing.
#
# Every other shot in this file is of the room. The player is in most of them -
# single player forces the third-person camera - but he is small, off centre and
# the same colour as the floor, so no assertion here has ever been about him and
# the two false reproductions of the custom-skin defect both came from reading
# floor pixels as model pixels.
#
# So: pin the camera, push it back far enough that the whole model fits, drop the
# pitch so the camera looks at him rather than over him, and take one shot from
# behind and one from the front. Nothing is asserted about the result - this is a
# lane for looking, and it is the only honest kind of lane for a question whose
# answer is "does his face have the right texture on it".
#
# This lane is for LOOKING, and it cannot be used for comparing. Measured, after
# a claim was made on it that turned out to be false: two runs of the identical
# binary with the identical settings differ by
#
#     jkx_char_front   26321 pixels
#     jkx_char_face    5842 pixels
#     jkx_char_back    4025 pixels
#
# because the character is ANIMATED. His idle plays against real time, so two
# runs are at different points in it, and no amount of pinning the camera changes
# that - setviewpos resets the camera damping exactly and the man is still
# breathing. A difference of forty thousand pixels was read here as the effect of
# a change and it was the floor.
#
# So: to prove something about a model's shading from a picture, the model has to
# hold still. What is missing is a fixture model that is static, LIT - the
# fixture's own models are rgbGen const, which ignores normals entirely, so no
# change to a normal can ever show on one - and carrying a duplicated vertex.
# Written down rather than worked around, because a lane that cannot see a thing
# must not be used to say the thing happened.
#
# It goes into a config file rather than onto the command line, and that is not
# tidiness. The command line here is already long enough that adding ten more
# "+" arguments overflows the command buffer, and what the engine says when it
# does is "were dropped and will not run" - one line among several hundred, with
# every screenshot still written and every check still passing on frames that
# were taken with half the setup missing. A cfg is executed a line at a time and
# has no such limit.

# A player model with no textures of its own, and a skin name that will not
# register. This is the retail shape, and until it was measured this bench had
# the opposite of it.
#
# Every surface in the retail kyle/model.glm carries an EMPTY shader name - all
# eighty-two of them, read out of the file with a struct reader. The comment in
# the gamecode said the other thing ("it still loads the default skin's tga's
# because they're referenced in the .glm") and had said it since 2003. So a skin
# that fails to register is not a model falling back to its own materials, it is
# a model with no materials, drawn through the default shader: the black and
# white man in the frames that found this.
#
# The fixture's committed model.glm bakes jkx/smoke into its one surface, which
# means it can never show that failure. Rather than change the committed model -
# every other lane's expected colours come from it - this puts a second player
# model beside it with the shader names taken out, which is the retail shape, and
# gives it one skin file.
#
# It is a copy of the committed model rather than a fresh one from
# make_test_glm.py's defaults, and that was learned the hard way: a model built
# with the defaults has no tags and the wrong bone count, G_SetG2PlayerModelInfo
# rejects it, the gamecode quietly falls back to a mouse md3 and the frame
# contains no character at all. Which duly failed the check below, for a reason
# that had nothing to do with skins.
#
# Then the lane asks for a three-part skin the model does not have. Anything
# other than model_default takes the "|head|torso|legs" branch in the Academy
# gamecode, and a model with no such parts registers nothing. What should happen
# is the fallback to model_default.skin - green here. What used to happen is no
# skin at all.
#
# No retail data, so this one can live in CI.
if [ "${JKX_SMOKE_SKINFALL:-0}" = "1" ]; then
    mkdir -p "$RUN/base/models/players/jkxbare"
    python3 "$HERE/glm_strip_shaders.py" \
        "$RUN/base/models/players/jkx/model.glm" \
        "$RUN/base/models/players/jkxbare/model.glm" >/dev/null
    printf '// The only place this model has any textures at all.\nbody,jkx/skin_body\n' \
        > "$RUN/base/models/players/jkxbare/model_default.skin"
    JKX_SMOKE_CHAR=jkxbare
    JKX_SMOKE_CHAR_SKIN=a1
    JKX_SMOKE_CHARSHOT=1
fi

# A character wearing a material with a normal map on it.
#
# The bench ran the physically-based renderer for weeks and never drew a single
# physically-based draw. A shader permutation is generated from what a material
# asks for, and no material here asked for a normal map, so the permutation that
# reads descriptor set five was never generated and the code that binds set five
# was never run. It was wrong - the DrawItem path bound sets zero to four and
# pushed nothing into five - and the first draw that exercised it was a
# segmentation fault in a lavapipe worker thread, found with a retail model.
#
# This lane produces such a draw with no retail data: two flat maps from
# make_test_material.py and the jkx/pbr_body material that names them, on the
# fixture's own character.
if [ "${JKX_SMOKE_PBRCHAR:-0}" = "1" ]; then
    mkdir -p "$RUN/base/models/players/jkxpbr" "$RUN/base/textures/jkx"
    python3 "$HERE/make_test_material.py" "$RUN/base/textures/jkx" >/dev/null

    # The committed model with its shader names taken out, so the only material
    # it can get is the one the skin file names. Same tool the skin lane uses.
    python3 "$HERE/glm_strip_shaders.py" \
        "$RUN/base/models/players/jkx/model.glm" \
        "$RUN/base/models/players/jkxpbr/model.glm" >/dev/null
    printf '// The material with the normal map, and nothing else.\nbody,jkx/pbr_body\n' \
        > "$RUN/base/models/players/jkxpbr/model_default.skin"

    JKX_SMOKE_CHAR=jkxpbr
    JKX_SMOKE_CHARSHOT=1
    JKX_SMOKE_PBR=1
fi

# A Ghoul2 model standing in a menu, which is a subsystem this bench has never
# drawn through.
#
# The menu SYSTEM does run here - the fixture's mainMenu is what the first
# screenshot of every run is a picture of - but every item in it until now was a
# coloured rectangle. Item_Model_Paint, its own refdef, its own light and the
# whole of the asset_model and model_g2skin path had never executed, and that is
# where the untextured lightsaber hilt lived.
#
# The model is generated with jkx/glm_baked, which is pure blue and which
# nothing else in this fixture draws, so "the model kept its own materials" is a
# colour rather than an impression. See ui/jkx_model.menu for what the empty
# model_g2skin line is doing and why it is the whole point.
if [ "${JKX_SMOKE_MENUMODEL:-0}" = "1" ] || [ "${JKX_SMOKE_MENULIGHT:-0}" = "1" ]; then
    mkdir -p "$RUN/base/models/players/jkxmenu"

    # jkx/glm_baked is flat blue and answers "did the model keep its own
    # materials". jkx/menu_lit is white and lit, and answers a different
    # question: what did the menu's lighting actually put on the screen. The
    # first cannot answer the second - a constant colour ignores the light
    # entirely - which is why the bench could not see a blown-out menu model at
    # all, and a player sees one before he sees anything else in the game.
    MENU_SHADER=jkx/glm_baked
    if [ "${JKX_SMOKE_MENULIGHT:-0}" = "1" ]; then
        MENU_SHADER=jkx/menu_lit
    fi

    # The same skeleton the rest of the fixture's characters use: a .glm names
    # its .gla and the loader refuses a name it cannot find, so this cannot be
    # left at the generator's default.
    python3 "$HERE/make_test_glm.py" \
        "$RUN/base/models/players/jkxmenu/model.glm" \
        --shader "$MENU_SHADER" \
        --model-name models/players/jkxmenu/model.glm >/dev/null
fi

if [ "${JKX_SMOKE_CHARSHOT:-0}" = "1" ]; then
    {
        echo "cg_thirdPersonRange 55"
        echo "cg_thirdPersonVertOffset -12"
        echo "cg_thirdPersonPitchOffset 0"
        echo "setviewpos 0 0 -15 90"
        echo "wait 60"
        echo "screenshot_tga jkx_char_back"
        echo "wait 20"
        echo "cg_thirdPersonAngle 180"
        echo "setviewpos 0 0 -15 90"
        echo "wait 60"
        echo "screenshot_tga jkx_char_front"
        echo "wait 20"
        # And a close-up of the head and shoulder, which is where the other two
        # questions about this model live: the faceting at texture seams that
        # has been there since 2003, and whether the face animates or cuts.
        # The vertical offset raises the CAMERA and leaves the aim where it was,
        # so a bigger number does not frame something higher - it looks down
        # from further up, and the man slides towards the bottom of the picture.
        # Measured: at range 24 an offset of -30 fills the frame with his belt
        # and +12 puts the top of his head at three quarters of the way down.
        # The aim is what has to move, and that is the pitch offset - positive
        # is downwards, the same sign convention as every other pitch here.
        #
        # Three of them in one pass, because a run of this lane is a quarter of
        # an hour and guessing twice costs half an hour.
        for pitch in 0 10 20; do
            echo "cg_thirdPersonRange 30"
            echo "cg_thirdPersonVertOffset 20"
            echo "cg_thirdPersonPitchOffset $pitch"
            echo "setviewpos 0 0 -15 90"
            echo "wait 50"
            echo "screenshot_tga jkx_char_face$pitch"
            echo "wait 20"
        done
        echo "cg_thirdPersonPitchOffset 0"
        echo "cg_thirdPersonAngle 0"
        echo "cg_thirdPersonRange 50"
        echo "cg_thirdPersonVertOffset 16"
    } > "$RUN/base/jkx_char.cfg"
    INMAP_STEP+=( +exec jkx_char.cfg +wait 200 )
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
    # The wait after unfreezing is SETTLE and not twenty, and that number cost
    # three red builds to arrive at.
    #
    # r_dissolveFreeze holds the wipe at forty-five per cent for the shot above;
    # setting it back to -1 lets it run to the end, and how far it gets in a
    # given number of frames is a question about real time rather than about
    # frames. Twenty was not always enough. What was left was the wipe's
    # boundary, a few pixels wide, down the left edge of the sky shot - and the
    # depth pre-pass stage, which compares two runs frame for frame, saw it as
    # 56 to 1134 pixels of disagreement at x = 2 to 5 with a channel delta of
    # 255. It read as a renderer defect and it is the fixture not waiting.
    #
    # It only started failing when the map's floor became visible, because until
    # then there was nothing behind the boundary for it to differ against.
    INMAP_STEP+=( +set r_dissolveType 1 +set r_dissolveFreeze 45
                  +wait 20 +map jkx_room2 +wait $SETTLE
                  +screenshot_tga jkx_wipe +wait 20
                  +set r_dissolveFreeze -1 +wait $SETTLE )

    # A plain run pins the camera before this shot, and the reason is worth
    # writing down because it was read as a renderer defect twice.
    #
    # Every other frame in the A/B lane compares to the pixel. This one came
    # back at 0, 1 or 151 differing pixels depending on the run, and 151 fails.
    # Dumping the coordinates settled what kind of difference it is: a
    # single-pixel-wide staircase along one long diagonal edge, the two runs
    # stepping it at different rows. That is a camera a fraction of a unit from
    # where it was, not a wall drawn differently.
    #
    # What is measured, and it is less than the first version of this comment
    # claimed:
    #
    #   com_maxfps 60 against com_maxfps 20, unpinned: identical, twice. Which
    #   looks like a refutation of "it is a timing difference" and is not -
    #   lavapipe at 1280x720 never gets near either cap, so neither run was
    #   capped and nothing was varied. Check that the knob turns something;
    #
    #   the same settings on both sides - r_depthPrepass 0 against
    #   r_depthPrepass 0 - with six busy processes on the machine, unpinned:
    #   they differ. So this lane was not measuring the depth prepass at all,
    #   it was measuring how long the frames happened to take;
    #
    #   with the pin below: identical, three pairs out of three under the same
    #   load, and every frame in the lane at zero.
    #
    # The first version of this comment said the player falls to the floor after
    # spawning and lands differently depending on the frame duration. That is
    # wrong and the fixture says so: make_test_bsp.py puts info_player_start
    # standing on the floor precisely so that nothing falls, and its docstring
    # explains that it was moved there to stop this class of difference.
    #
    # What is left, read from the code rather than measured: single player
    # forces the third-person camera, and that camera follows its ideal
    # position through an exponential filter whose ratio is
    # powf(1 - cg_thirdPersonTargetDamp, elapsed_ms / 50) - cg_view.cpp. A
    # filter like that reaches its target only in the limit, so what remains
    # after any number of frames depends on the sequence of frame durations, for
    # ever. setviewpos calls CG_ResetThirdPersonViewDamp, which copies ideal to
    # current exactly; from there the difference is exactly zero and stays zero
    # whatever the frame durations are, which is why the pin works and why
    # sixty frames is enough.
    #
    # Stated as a hypothesis, not a result: switching the damping off did not
    # reproduce the difference either way in the two pairs it was given, because
    # the unpinned difference itself only shows up in some runs. The pin is
    # justified by the third measurement above regardless of which mechanism is
    # behind it - this lane asks about the depth prepass and must not answer
    # about anything else.
    if [ "${JKX_SMOKE_PLAIN:-0}" = "1" ]; then
        INMAP_STEP+=( +setviewpos 0 0 -15 90 +wait 60 )
    fi

    INMAP_STEP+=( +screenshot_tga jkx_sky +wait 20 )

    # Tearing the renderer down and building it again, with a map loaded.
    #
    # This bench has started the renderer several hundred times and shut it down
    # once per run, at the end, on its way out. It had never once done the thing
    # a player does from the video menu: destroy every Vulkan object while the
    # game is running and make them all again. On the hardware that crashes, and
    # so does quitting from the menu, and both crash reports name the same line
    # - vkDestroyDevice inside vk_shutdown - so whatever it is, it is a thing
    # this lane can reach and nothing here was reaching it.
    #
    # The wait afterwards is long because a restart reloads every shader, image
    # and model, and under a software rasteriser with the validation layer on
    # that is not quick. The screenshot is the evidence that the engine is still
    # drawing on the other side of it rather than merely still running.
    if [ "${JKX_SMOKE_VIDRESTART:-0}" = "1" ]; then
        # Markers, because "it crashes somewhere in the next two hundred and
        # forty frames" is not a place.
        #
        # The restart is followed by a run of short waits with an echo between
        # each, so the last line in the log says how far the engine got. The
        # frames are what matters: a fault during re-initialisation dies before
        # the first marker, and one that needs drawing dies after some number of
        # them. Through a config file - the command line has a limit and this
        # bench has overflowed it twice.
        {
            echo "vid_restart"
            for n in 1 2 3 5 10 20 40 80 160; do
                echo "wait $n"
                echo "echo JKXMARK frames $n"
            done
            echo "screenshot_tga jkx_vidrestart"
            echo "wait 20"
        } > "$RUN/base/jkx_vidrestart.cfg"
        INMAP_STEP+=( +exec jkx_vidrestart.cfg +wait 360 )
    fi

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

# And a model whose shading can be measured: still, lit, and seamed.
#
# Everything else in this fixture is rgbGen const, which means no change to a
# normal, a light or a shading term could alter one pixel of any model here. A
# weld of model normals was landed and reported with a number that turned out to
# be the noise floor of an animated character, precisely because there was no
# still, lit model to look at instead. See make_test_seam.py.
python3 "$HERE/make_test_seam.py" "$RUN/base/models/jkx/seam.md3" >/dev/null

# The same model with ONE frame, which is what sends it through the vertex buffer
# instead of the batch - two different draw paths, and until this fixture existed
# only one of them had ever been looked at. Every prop, every weapon on the
# ground and every piece of debris in the game is a single-frame MD3.
python3 "$HERE/make_test_seam.py" "$RUN/base/models/jkx/seam1.md3" --frames 1 >/dev/null

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
if [ "${JKX_SMOKE_LIGHTMAP:-0}" = "1" ]; then
    # The same room, lit by a real lightmap page instead of by white vertices.
    #
    # This closes a subsystem the bench had never executed once: R_LoadLightmaps,
    # the lightmap atlas, the lightmap texture coordinates, and every USE_LIGHTMAP
    # variant the shader generator builds - which is how every real map in the
    # game is lit. The fixture had LIGHTMAP_BY_VERTEX on every surface and an
    # empty lightmap lump.
    #
    # It also makes the map's brightness a number. The ordinary floor is white by
    # design, so "did the map get brighter" runs into 255 and stops; that is
    # exactly where a question about overbright bits ran aground. The lightmap is
    # a flat 32, and the floor's shader draws the lightmap and nothing else, so
    # what comes out is the engine's scaling and nothing else.
    python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room.bsp" \
        --shader jkx/lightmapped --lightmap >/dev/null
elif [ "${JKX_SMOKE_PHYS:-0}" = "1" ]; then
    # Seven squares in a column: six packings of the same three physical values
    # and one control with a different roughness.
    #
    # A column rather than a row for the same reason as the other two lanes -
    # the camera is pinned looking down +Y and heights are what fits. Six
    # squares of half-side 11, twenty-four apart, from z 70 down to z -50 at
    # y 200: the lowest edge lands at -61 against a floor at -64, and the
    # highest reaches 0.47 of the view against a vertical field of 0.56. The
    # first arrangement put the bottom square through the floor and it came
    # back at 881 pixels of the 1936 it should have.
    python3 "$HERE/make_test_material.py" "$RUN/base/textures/jkx" >/dev/null
    python3 "$HERE/make_test_material.py" --phys "$RUN/base/textures/jkx" >/dev/null

    PHYS_PROPS=()
    PHYS_Z=70
    for name in rmo rmos moxr mosr orm orms; do
        python3 "$HERE/make_test_md3.py" \
            "$RUN/base/models/jkx/ph_$name.md3" \
            --flat --size 11 --shader "jkx/ph_$name" >/dev/null
        PHYS_PROPS+=( --prop "models/jkx/ph_$name.md3:200:$PHYS_Z" )
        PHYS_Z=$(( PHYS_Z - 24 ))
    done

    python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room.bsp" \
        "${PHYS_PROPS[@]}" >/dev/null
elif [ "${JKX_SMOKE_DEFORM:-0}" != "0" ]; then
    # Four squares, one deformVertexes each, in flat colours because what moves
    # here is the geometry rather than the texture coordinates.
    #
    # JKX_SMOKE_DEFORM=glm builds the same four out of Ghoul2 models instead of
    # MD3s, and that is not a variation for its own sake - it is the experiment
    # that decides where "move and bulge do nothing" lives.
    # ShaderRequiresCPUDeforms answers qfalse - meaning the GPU will do the
    # deform in the vertex shader - ONLY for tess.vbo_model && surfType ==
    # SF_MDX, which is Ghoul2. An MD3 prop is told the CPU will do it instead.
    # So the same four materials on the two kinds of model run down two
    # different implementations of the same keyword, and comparing them says
    # which one is broken rather than that something is.
    DEFORM_EXT=md3
    if [ "${JKX_SMOKE_DEFORM}" = "glm" ]; then
        DEFORM_EXT=glm
    fi

    for name in ref wave move bulge; do
        if [ "$DEFORM_EXT" = "glm" ]; then
            python3 "$HERE/make_test_glm.py" \
                "$RUN/base/models/jkx/df_$name.glm" \
                --shader "jkx/df_$name" \
                --model-name "models/jkx/df_$name.glm" >/dev/null
        else
            python3 "$HERE/make_test_md3.py" \
                "$RUN/base/models/jkx/df_$name.md3" \
                --flat --frames "${JKX_SMOKE_DEFORM_FRAMES:-1}" \
                --size 14 --shader "jkx/df_$name" >/dev/null
        fi
    done

    python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room.bsp" \
        --prop "models/jkx/df_ref.$DEFORM_EXT:200:60" \
        --prop "models/jkx/df_wave.$DEFORM_EXT:200:20" \
        --prop "models/jkx/df_move.$DEFORM_EXT:200:-20" \
        --prop "models/jkx/df_bulge.$DEFORM_EXT:200:-60" >/dev/null
elif [ "${JKX_SMOKE_TCMOD:-0}" = "1" ]; then
    # Five squares side by side, one per tcMod, each painted in its own colour
    # so a count of a colour is a count of a square.
    #
    # They are spread along z rather than across the view because the fixture's
    # camera is pinned looking down +Y and a column is what fits: five squares
    # at a half-side of 14, thirty apart, reach 0.44 of the view from its centre
    # at y 200, which is inside the vertical field.
    python3 "$HERE/make_test_material.py" --tc \
        "$RUN/base/textures/jkx" >/dev/null

    for name in ref scroll rotate stretch scale; do
        python3 "$HERE/make_test_md3.py" \
            "$RUN/base/models/jkx/tc_$name.md3" \
            --flat --size 14 --shader "jkx/tc_$name" >/dev/null
    done

    python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room.bsp" \
        --prop models/jkx/tc_ref.md3:200:60 \
        --prop models/jkx/tc_scroll.md3:200:30 \
        --prop models/jkx/tc_rotate.md3:200:0 \
        --prop models/jkx/tc_stretch.md3:200:-30 \
        --prop models/jkx/tc_scale.md3:200:-60 >/dev/null
elif [ "${JKX_SMOKE_TRANSPARENCY:-0}" = "1" ]; then
    # A backdrop of a known colour with three translucent squares in front of
    # it, each at a different height so no two overlap each other.
    #
    # The geometry is arithmetic rather than taste. The player stands at y 0 and
    # looks down +Y with his eye near z -14; the backdrop is at y 240 with a
    # half-side of 90, and the squares are at y 180 with a half-side of 16,
    # offset in z by -30, 0 and +30. Divide each extent by its distance and the
    # squares reach 0.33 of the view from the centre while the backdrop reaches
    # 0.43, so every square is inside it ON SCREEN with room to spare - which is
    # the point. A translucent surface blended against the sky or against the
    # floor would be measuring where it is rather than how it blends.
    #
    # The backdrop's own bottom edge is below the floor and clipped by it. That
    # is checked too: the lowest square stops at -0.18 of the view and the floor
    # cuts the backdrop at -0.21, so the square still has something behind it.
    #
    # --flat is what makes the numbers exact: one frame, no shift, no seam. The
    # ordinary square is somewhere else in each of its two frames, and a blend
    # result between two surfaces that move is not a number.
    for pair in "backdrop:90" "add:16" "blend:16" "filter:16"; do
        name="${pair%%:*}"
        half="${pair##*:}"
        python3 "$HERE/make_test_md3.py" \
            "$RUN/base/models/jkx/trans_$name.md3" \
            --flat --size "$half" --shader "jkx/trans_$name" >/dev/null
    done

    python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room.bsp" \
        --prop models/jkx/trans_backdrop.md3:240:0 \
        --prop models/jkx/trans_add.md3:180:30 \
        --prop models/jkx/trans_blend.md3:180:0 \
        --prop models/jkx/trans_filter.md3:180:-30 >/dev/null
elif [ "${JKX_SMOKE_MAPENT:-0}" = "1" ]; then
    # The same room with one piece of furniture in it. See the note above
    # INMAP_STEP for what the two shots of it are for; the model is the
    # fixture's own animated square, which is cyan and nothing else here is.
    python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room.bsp" \
        --prop models/jkx/anim.md3 >/dev/null
else
    python3 "$HERE/make_test_bsp.py" "$RUN/base/maps/jkx_room.bsp" >/dev/null
fi
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

# Movement, as a number rather than as a picture.
#
# This lane could not have existed a day ago: the fixture's only brush was the
# room's own volume marked solid, so the player stood inside solid and could not
# move at all. See make_test_bsp.py's brushsides(). With a floor under him he
# walks and jumps, and the first thing worth asking is how high.
#
# Single player integrates movement once per rendered frame with that frame's
# own duration as the step - pml.msec in bg_pmove.cpp, clamped to [1, 200] and
# not divided into anything smaller. pmove_fixed and pmove_msec, which exist in
# the multiplayer branch to cut the interval into fixed steps, are not in this
# tree at all. A jump is the sharpest symptom: gravity is subtracted once per
# step, so a different number of steps between leaving the floor and coming back
# gives a different apex.
#
# The trajectory is read from the game rather than from the console. The first
# version of this lane asked for "viewpos" once a frame, and that answer is the
# third-person CAMERA - rounded to whole units, damped towards the player over
# real time, and only produced on the frames the console buffer got to run. It
# was enough to see that the apex moves and not enough to measure by.
#
# g_moveTrace prints the player's own origin and velocity once per server frame,
# with the length of the step that produced them. Every step, full precision,
# nothing between the simulation and the number.
#
# THE MEASUREMENT IS A FALL, not the jump, and that is the second version of
# this lane. A jump looked like the sharpest symptom and is the wrong
# instrument: force jump keeps assigning the vertical velocity for as long as
# the key is held, and this script holds it for three FRAMES - which is 15 to 66
# milliseconds on an idle machine and 100 to 220 on a busy one. So the input
# itself changed with the frame rate, and the apex came back bimodal: 12.54,
# 10.45, 12.63, 10.63, 10.45 on five identical runs.
#
# A fall has no input in it at all. The player is put in the air and gravity is
# integrated until he lands, so what is measured is the integration and nothing
# else.
#
# Two numbers come out and the IMPACT SPEED is the one to read. The distance is
# fixed by the room, so how fast he is going when he arrives is a property of
# the integration and of nothing else. Measured:
#
#   pmove_fixed 0, idle:     -615.2, -611.2   (73 and 64 steps)
#   pmove_fixed 0, loaded:   -574.4, -600.8   (10 and 11 steps)
#   pmove_fixed 1, idle:     -616.0, -619.2   (126 and 135 steps)
#   pmove_fixed 1, loaded:   -615.2, -619.2   (92 and 81 steps)
#
# Ten integration steps for a whole fall is what a busy machine gives, and Euler
# with a step that size arrives seven per cent slow. With fixed steps the
# loaded runs land on the idle numbers.
#
# The duration is reported too but is the weaker number: under load the fixed
# path also hits the 200 ms arrears clamp in Pmove, which discards simulated
# time, so the fall covers fewer milliseconds without the trajectory being any
# different. The speed at the floor does not care how many milliseconds the
# simulation decided to run.
#
# The jump is still here, after the fall, and still gates - but on "he left the
# ground", not on how high.
if [ "${JKX_SMOKE_MOVE:-0}" = "1" ]; then
    {
        # setviewpos takes an eye position and subtracts 25, so this puts the
        # player's origin at 200 - about 240 units above where he comes to rest.
        echo "setviewpos 0 0 225 90"
        echo "wait 5"
        echo "g_moveTrace 1"
        echo "wait 200"
        echo "+moveup"
        echo "wait 3"
        echo "-moveup"
        echo "wait 60"
        echo "g_moveTrace 0"
    } > "$RUN/base/jkx_move.cfg"
    INMAP_STEP+=( +exec jkx_move.cfg +wait 260 )
fi

# Retail data, for a run that is a diagnosis rather than a check.
#
# Everything this bench draws is generated, and the second entry in the blind
# spot list says why that matters: a fixture without variety measures nothing
# beyond itself. The character path is the sharpest case. The generated .glm has
# one skin with one part, so the renderer's skin HANDLE and the configstring
# INDEX are both 1 - the two numbering spaces agree by accident, which is
# precisely why passing one where the other belongs survived for years and could
# not be seen from a picture here.
#
# JKX_SMOKE_EXTRA_BASE names directories - colon separated - whose contents are
# copied over the fixture's base/ before the run. Nothing from them is committed
# and nothing here goes looking for them: a retail install is not redistributable
# and must not enter this repository. Point it at an unpacked copy in /tmp when
# there is a question only real data can answer, and leave it unset otherwise, so
# the lane list stays runnable on a machine that owns no copy of the game.
#
# This is deliberately dumb - a copy, not a search path - because the thing being
# tested is what the engine does with the files, and an extra layer between the
# files and the engine is one more thing that can be wrong.
if [ -n "${JKX_SMOKE_EXTRA_BASE:-}" ]; then
    IFS=':' read -r -a EXTRA_DIRS <<< "$JKX_SMOKE_EXTRA_BASE"
    for extra in "${EXTRA_DIRS[@]}"; do
        [ -n "$extra" ] || continue
        if [ ! -d "$extra" ]; then
            echo "JKX_SMOKE_EXTRA_BASE names $extra, which is not a directory" >&2
            exit 2
        fi
        cp -r "$extra"/. "$RUN/base/"
        echo "extra base data: $extra"
    done
fi

# Everything above builds a game directory out of loose files, and every run so
# far has read it that way. A pk3 is a zip, and it is how the retail assets
# arrive and how every downloaded mod arrives - so the archive half of the
# filesystem was outside this bench entirely: FS_LoadZipFile, the per-pack hash
# table, the pak branch of FS_FOpenFileRead, unzReadCurrentFile in FS_Read and
# the pk3 case of FS_Seek had never executed here once.
#
# JKX_SMOKE_PK3 packs the fixture and deletes what it packed, so there is no
# loose file left to fall back to. The run either reads through the archive or
# it fails, which is the only arrangement worth having: a fixture that keeps
# both copies proves nothing, because the directory branch comes first in the
# search path and would serve every read.
if [ "${JKX_SMOKE_PK3:-0}" = "1" ]; then
    python3 "$HERE/make_pk3.py" "$RUN/base" "$RUN/base/jkx_fixture.pk3" >/dev/null
fi

# Extra cvars, as "name=value name=value". This exists for A/B runs: two passes
# of the same fixture that differ by one setting, compared pixel for pixel. The
# cubemap sky was landed that way, and the comparison is what showed that five
# of its six faces were rotated - the fixture's camera looks along +Y, which is
# the one face the two code paths already agreed about, so every assertion in
# this file passed on a sky that was wrong everywhere else.
#
# Note that a latched cvar only takes here because it is set on the command
# line before the renderer starts.
# Tearing the renderer down before anything has been loaded.
#
# The lane above restarts with a map up, and it crashes. Four things have been
# ruled out by running it without them - the validation layer, PBR, the present
# mode, and destroying the instance and surface - so the next question is whether
# the crash needs a world at all. If it happens here too, nothing about the map,
# its models, its lightgrid or the screen wipe can be the cause, and the search
# is over a much smaller piece of code. If it does not, the difference between
# the two lanes is the answer.
#
# JKX_SMOKE_VIDRESTART=menu, and it is a separate word rather than a second lane
# because it is the same question asked in one fewer place.
MENU_RESTART_STEP=()
if [ "${JKX_SMOKE_VIDRESTART:-0}" = "menu" ]; then
    MENU_RESTART_STEP=( +vid_restart +wait 240 +screenshot_tga jkx_menurestart +wait 20 )
fi

SET_STEP=()
for pair in ${JKX_SMOKE_SET:-}; do
    SET_STEP+=( +set "${pair%%=*}" "${pair#*=}" )
done

# On a real map, turn the script interpreter's own trace on.
#
# g_ICARUSDebug is WL_ERROR by default, which says only that something failed.
# WL_DEBUG prints every statement as it runs - "npc_roshintro1(52): sound(
# CHAN_VOICE_GLOBAL, sound/chars/rosh/01rop003.mp3 ); [24100]" - and that is the
# observable for the question a retail map is here to answer: did the scripted
# sequence actually run, and how far did it get. The fixture map has no scripts
# in it, so this costs the ordinary lanes nothing and is left off there.
#
# g_subtitles puts the spoken lines in the log as well, which is how a run with
# no sound assets still shows what was said.
if [ -n "${JKX_SMOKE_MAP:-}" ]; then
    SET_STEP+=( +set g_ICARUSDebug 4 +set g_subtitles 1 )
fi

# A different menu set, which is what makes the menu-model lane opt-in. The
# ordinary one is ui/menus.txt and every other lane loads it, so the frame all
# the other checks are written against does not change.
if [ "${JKX_SMOKE_MENUMODEL:-0}" = "1" ] || [ "${JKX_SMOKE_MENULIGHT:-0}" = "1" ]; then
    SET_STEP+=( +set ui_menuFiles ui/jkx_model.txt )
fi

# A menu set with a loading screen in it, and the loading screen registers four
# skins before the map registers its first.
#
# That is the ONLY thing it changes, and it is the whole point: the offset
# between a character skin's configstring index and its renderer handle exists
# on every real installation and has never existed here, because the fixture had
# no loading screen. See the long note in ui/jkx_loadscreen.menu.
#
# HALF BUILT, and saying so is worth more than leaving it to be discovered. The
# four early registrations happen - the run log shows two of them failing by
# name, which is the proof they were asked for - but the offset still does not
# appear, because the second half is missing: the fixture map puts NO character
# skin into the CS_CHARSKINS configstrings, so cgs.skins is empty and the loop
# in CG_RegisterGraphics that compares the two numbering spaces breaks out on
# its first iteration. Measured with JKX_SMOKE_CHAR=jkx: not one line about
# skins in the whole engine log.
#
# What is needed next is a fixture character that takes a configstring slot the
# way a map's own NPC does. Until then this switch proves only that the early
# registrations land.
#
# Worth pairing with JKX_SMOKE_CHAR, since the defect this exposes is one that
# only shows on a character.


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

# Which lighting path this run measures, said out loud instead of inherited from
# a device limit.
#
# Every picture in this file was taken under fastlight, and not because anybody
# chose it: the PBR path needed more descriptor sets than lavapipe has, so it
# switched itself off and nothing here ever exercised it. That is fixed - the
# five material textures share one pushed set now - which means the default lane
# would silently start measuring a different renderer than the one its expected
# colours came from.
#
# So the default lane asks for fastlight, and PBR gets a lane of its own.
# JKX_SMOKE_PBR=1 turns it on; it is not in the stage list yet, for the same
# reason the vid_restart lane is not: it still has something to say.
# The character's skin is not checked here yet, and what is missing is written
# down because it was nearly claimed twice.
#
# The defect is measured, and by the engine rather than by a frame. With the
# three-part skin name this fixture cannot satisfy, G_SetG2PlayerModel printed
#
#     "models/players/jkx/|head_a1|torso_a1|lower_a1" handle 0, configstring 1
#
# - RE_RegisterSkin said it could not register that skin, and the gamecode set a
# custom skin of 1 anyway, which the renderer resolves as a skin HANDLE through
# R_GetSkinByHandle. A model wearing whatever skin 1 happens to be.
#
# What cannot be shown from a picture yet, and why:
#
#   the character has not been identified in any frame here. The white blob this
#   file checks is the FLOOR - the check a few lines up says so - and the frame
#   is byte-identical with the skin path fixed and broken, which is the proof
#   that the model is not what those pixels are;
#
#   and with the single skin this fixture has, the handle and the configstring
#   index are BOTH 1. They agree by accident, which is the whole reason this
#   defect survived - a fixture where the two numbering spaces cannot diverge
#   cannot tell them apart.
#
# So: put the character in frame and identifiable, then register a second skin
# first so the handle is not 1, and the colours below become the check. The
# green, red and white are here for that.
#
# A lane was written that asserted the model wears model_default.skin - green in
# this fixture, against the white baked into the .glm - and it failed, and the
# failure was read as the mCustomSkin defect. It was not. Asking the engine
# directly, with a print inside G_SetSkin, showed that G_SetSkin is never called
# in this fixture at all: nothing sets a skin, so nothing can set the wrong one,
# and the white on screen is the baked shader that was never overridden.
#
# So the frame proved nothing about mCustomSkin, and a lane whose message names
# a defect it cannot see is worse than no lane. What it did find is bigger and
# is now the open question: the character-model path does not run here. The
# fixture keeps its colours - green skin, red alternate, white baked - because
# they are what will make the answer visible once it does.
# The packing lane reads the ROUGHNESS DEBUG VIEW rather than the shaded
# picture, so it asks for it here rather than leaving it to whoever runs the
# lane. See the check further down for why the shaded picture is unusable.
if [ "${JKX_SMOKE_PHYS:-0}" = "1" ]; then
    # Three is "roughness", which is what the packing check reads. It is a
    # variable rather than a constant because the OTHER views are the diagnosis
    # of the black-world-surface defect and each of them is one run:
    #
    #   JKX_SMOKE_PHYS_VIEW=0   the shaded picture, where the squares are black
    #   JKX_SMOKE_PHYS_VIEW=1   diffuse - zero if metalness reads as one
    #   JKX_SMOKE_PHYS_VIEW=18  nl - zero if the light vector never arrives
    #
    # "debugview" with no argument prints the whole list.
    SET_STEP+=( +set r_debugView "${JKX_SMOKE_PHYS_VIEW:-3}" )
fi

if [ "${JKX_SMOKE_PBR:-0}" = "1" ] || [ "${JKX_SMOKE_PHYS:-0}" = "1" ]; then
    SET_STEP+=( +set r_normalMapping 1 +set r_specularMapping 1 )
else
    SET_STEP+=( +set r_normalMapping 0 +set r_specularMapping 0 )
fi

# Which character, and which skin on him.
#
# The three parts are pinned to model_default here because that is the only skin
# the generated .glm has. JKX_SMOKE_CHAR_SKIN takes either one name for all three
# or three separated by "|", and the word "retail" leaves all three unset so the
# engine's own defaults apply - head_a1, torso_a1, lower_a1, which is what a
# player actually starts with and which is the input that produced the wrong
# custom skin. Those three names build the "|head|torso|legs" form, and a model
# that has no such parts fails to register, which is the whole case.
CHAR_STEP=( +set g_char_model "${JKX_SMOKE_CHAR:-jkx}" )
CHAR_SKIN="${JKX_SMOKE_CHAR_SKIN:-model_default}"
if [ "$CHAR_SKIN" != "retail" ]; then
    case "$CHAR_SKIN" in
        *"|"*)
            CHAR_HEAD="${CHAR_SKIN%%|*}"
            CHAR_REST="${CHAR_SKIN#*|}"
            CHAR_TORSO="${CHAR_REST%%|*}"
            CHAR_LEGS="${CHAR_REST#*|}"
            ;;
        *)
            CHAR_HEAD="$CHAR_SKIN"; CHAR_TORSO="$CHAR_SKIN"; CHAR_LEGS="$CHAR_SKIN"
            ;;
    esac
    CHAR_STEP+=( +set g_char_skin_head "$CHAR_HEAD"
                 +set g_char_skin_torso "$CHAR_TORSO"
                 +set g_char_skin_legs "$CHAR_LEGS" )
fi

# Something in front of the engine: a debugger, a tracer, a profiler.
#
# JKX_SMOKE_WRAP holds a command that the engine is passed to. It exists because
# a lane that reproduces a crash is only half of what a crash needs - the other
# half is the stack, and the whole point of getting a hardware crash onto this
# bench is to stop reading it out of screenshots.
#
#   JKX_SMOKE_WRAP="gdb -batch -ex run -ex bt -ex 'info registers' --args"
#
# The words are split on spaces deliberately, so the value is a command and its
# flags rather than a shell line; anything that needs quoting goes in a script.
WRAP=()
if [ -n "${JKX_SMOKE_WRAP:-}" ]; then
    read -r -a WRAP <<< "$JKX_SMOKE_WRAP"
fi

set +e
( cd "$RUN" && \
  DISPLAY="$DISPLAY_NUM" \
  XDG_RUNTIME_DIR="$RUN/xdg" \
  VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}" \
  timeout -k 10 "${JKX_SMOKE_TIMEOUT:-600}" "${WRAP[@]}" "./$(basename "$ENGINE")" \
      +set fs_basepath "$RUN" +set fs_homepath "$RUN/home" \
      "${SOUND_STEP[@]}" +set com_errorDialog 0 +set con_notifytime 0 \
      +set cg_hudFiles ui/jkx_hud.txt "${CHAR_STEP[@]}" \
      +set helpUsObi 1 +set r_drawfog 0 \
      "${SET_STEP[@]}" \
      +wait 60 +screenshot_tga jkx_smoke +wait 20 \
      "${MENU_RESTART_STEP[@]}" \
      "${CONSOLE_STEP[@]}" \
      +wait 20 +map "${JKX_SMOKE_MAP:-jkx_smoke}" +wait 12 +screenshot_tga jkx_wiping \
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

# The vertical-sync lane. See the INMAP_STEP block above.
#
# It counts the line the cheap path prints. The claim being made is "the
# renderer was NOT restarted", and a claim like that needs something positive to
# point at rather than an absence - vid_restart never prints this one.
#
# Counting "----- R_Init -----" was tried as the negative half and is not a
# usable observable: single player rebuilds the renderer for the menu, for each
# map, and once more for the map this fixture deliberately rejects, so a normal
# run prints it four times with no vertical sync involved at all. Measured
# before assuming.
# The resize lane. See the INMAP_STEP block above.
#
# tga_size.py prints the dimensions and fails on a mismatch; the point is that
# both frames are checked, so a resize that goes one way and never comes back
# fails as loudly as one that never happened.
resize_checks() {
    local w h

    read -r w h <<< "$( python3 "$HERE/tga_size.py" \
        "$RUN/home/base/screenshots/jkx_resized.tga" 2>/dev/null )"

    if [ "$w" != "800" ] || [ "$h" != "600" ]; then
        report "the window did not resize: the frame is ${w}x${h} and should be \
800x600. r_mode is no longer latched, so nothing was waiting for a vid_restart - \
either WIN_Resize refused the mode or the swapchain was not rebuilt against it"
    fi

    read -r w h <<< "$( python3 "$HERE/tga_size.py" \
        "$RUN/home/base/screenshots/jkx_resized_back.tga" 2>/dev/null )"

    if [ "$w" != "1280" ] || [ "$h" != "720" ]; then
        report "the window did not resize back: the frame is ${w}x${h} and should \
be 1280x720"
    fi
}

vsync_checks() {
    local taken

    taken=$( grep -c 'rebuilding the swapchain only' "$RUN/run.log" || true )

    if [ "$taken" -lt 2 ]; then
        report "r_swapInterval took the swapchain path $taken time(s) out of two; \
on Vulkan the present mode is fixed when the swapchain is built, so a change \
that does not rebuild it changes nothing for the player"
    fi
}

forbid() {
    if grep -q -- "$1" "$RUN/run.log"; then
        report "present in the log and should not be: $1"
    fi
}

# The character-skin handle offset, and this lane asserts that it HAPPENS.
#
# That reads backwards until you know what it is for. The offset is a defect and
# the two halves of the fix are what deal with it; what this lane is for is the
# CONDITION - a loading screen that has taken four handles before the map asks
# for its first - because without it index and handle agree by accident and the
# fix underneath is being exercised against a case that never arises. Every lane
# in this bench was in exactly that position until now.
#
# So: the diagnostic must fire, the numbers must differ by four - the same four a
# log from a real installation reported - and the renumber pass must then act on
# both skins. If a future change makes the offset go away here, this lane says so
# loudly, because an offset that has quietly stopped happening means the fix is
# no longer being tested rather than that the defect is gone.
if [ "${JKX_SMOKE_RESIZE:-0}" = "1" ]; then
    require 'Wrote screenshots/jkx_resized.tga'
    require 'Wrote screenshots/jkx_resized_back.tga'
    resize_checks
fi

if [ "${JKX_SMOKE_VSYNC:-0}" = "1" ]; then
    require 'Wrote screenshots/jkx_vsync_on.tga'
    require 'Wrote screenshots/jkx_vsync_off.tga'
    vsync_checks
fi

if [ "${JKX_SMOKE_SKINSHIFT:-0}" = "1" ]; then
    require 'skin 1 is handle 5'
    require 'skin 2 is handle 6'
    require '2 model skin(s) renumbered from configstring indexes to renderer handles'
fi

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
#
# The count is the second half of that, and it is here because the position on
# its own was satisfied by the wrong thing for months. The floor was wound
# backwards, so it was culled and never drawn once; what the centroid check kept
# finding was a white shape of about six and a half thousand pixels near the
# bottom of the frame that is not the floor at all. Changing the floor's shader
# left the frame byte for byte identical, which is what finally settled it.
#
# A drawn floor is a quarter of a million pixels. A hundred thousand is well
# under that and far above anything else white in the frame, so the check can no
# longer be answered by something standing in the right place.
if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ] && [ -f "$RUN/home/base/screenshots/jkx_inmap.tga" ]; then
    if [ "${JKX_SMOKE_LIGHTMAP:-0}" != "1" ]; then
        if ! python3 "$HERE/tga_colour_where.py" \
            "$RUN/home/base/screenshots/jkx_inmap.tga" \
            "255,255,255@0.3,0.75,0.7,1.0"; then
            report "the map's floor is not where it should be in jkx_inmap.tga"
        fi
        if ! python3 "$HERE/tga_has_colour.py" \
            "$RUN/home/base/screenshots/jkx_inmap.tga" \
            "255,255,255:100000"; then
            report "too little of jkx_inmap.tga is floor; a culled floor leaves \
just enough white in the right place to pass the position check"
        fi
    else
        # The lightmap lane's whole point, in one number.
        #
        # The page is a flat 32 and the floor's shader draws the page and
        # nothing else, so what reaches the screen is the engine's own scaling
        # of a lightmap and nothing but. R_ColorShiftLightingBytes shifts left
        # by ( r_mapOverBrightBits - tr.overbrightBits ) and the shader then
        # multiplies by ( 1 << tr.overbrightBits ), so the two exponents cancel
        # and the answer is 32 * 2^r_mapOverBrightBits: 32, 64, 128, 255 for the
        # four settings, measured. The default here is 2.
        #
        # An equality, not a range. This is the check that says the world is lit
        # by its lightmap rather than by a white texture standing in for one -
        # and the difference between those two is the difference between 128 and
        # 255, which no tolerance worth having would swallow.
        if ! python3 "$HERE/tga_has_colour.py" \
            "$RUN/home/base/screenshots/jkx_inmap.tga" \
            "128,128,128:100000"; then
            report "the lightmapped floor is not at the brightness its page and \
r_mapOverBrightBits say it should be"
        fi
        if ! python3 "$HERE/tga_colour_where.py" \
            "$RUN/home/base/screenshots/jkx_inmap.tga" \
            "128,128,128@0.3,0.75,0.7,1.0"; then
            report "the lightmapped floor is not where the floor should be"
        fi
    fi
fi

# A Ghoul2 model in a menu, wearing the materials baked into it.
#
# jkx_smoke.tga is the first screenshot of every run and it is a picture of the
# fixture's main menu; in this lane that menu has a model in it. The model's own
# shader is pure blue and nothing else in the fixture draws blue, so the check
# is an equality on a colour rather than a judgement about a picture.
#
# What it does NOT measure is written out at length in ui/jkx_model.menu, and it
# matters: this lane was built to catch the untextured lightsaber hilt and it
# does not. Putting the old renderer test back leaves the frame identical to the
# pixel, so mCustomSkin is not -1 by the time this model is drawn. The lane
# earns its place on what it does cover - Item_Model_Paint and the asset_model
# path, neither of which had ever executed here - and the hilt is still waiting
# on a fixture sabers.cfg.
#
# Two thousand pixels: the model fills a 240 by 240 box in a 640 by 480 space,
# which is a great deal more than that at any window size this bench uses, and
# far more than any stray blend could produce.
if [ "${JKX_SMOKE_MENUMODEL:-0}" = "1" ]; then
    if ! python3 "$HERE/tga_has_colour.py" \
        "$RUN/home/base/screenshots/jkx_smoke.tga" "0,0,255:2000"; then
        report "the menu's model is not wearing its own materials; a model with \
no custom skin is being given the default shader instead"
    fi
fi

# What the menu's lighting actually put on the screen.
#
# This lane exists because of a report from hardware that the bench could not
# even represent: both models in the menu look burnt out. Every model this
# fixture drew was rgbGen const, so the menu's lighting had never reached a
# pixel here - the lane above photographs a model whose colour is a constant and
# would look identical under any light at all, including none.
#
# jkx/menu_lit is white and lit, so the pixel IS the lighting sum, and the
# histogram of its greys is that sum read back off the screen. What to look for:
# a tail is shading, a wall at 255 is a sum that went over one before it was
# written.
#
# The numbers came first, and here they are, all three from the same lane on the
# same binary with one change between them:
#
#   122   before anything was fixed. Exactly the ambient value and nothing else
#         on 24948 pixels - the whole model, one level, no shading at all. The
#         light direction was the zero vector and the shader divided by its
#         length;
#   251   with the two divisions guarded and tr.sunDirection given a value
#         before the first map load. Lit, and four short of the ceiling;
#   235   with the sum of ambient and directed scaled to fit the range it is
#         written into. Predicted 255/272 of 251 before the run, which is 235.3.
#
# So the gate is the third number, exactly, and it is exact on purpose: each of
# the three defects has its own value, so a failure says WHICH one came back
# rather than that something moved. 122 is the light direction gone again, 251
# is the sum clamp gone, 255 is a model burnt to flat white - the thing that was
# reported from hardware.
#
# The histogram is printed either way, because the gate answers one question and
# the shape of the distribution answers the next one.
if [ "${JKX_SMOKE_MENULIGHT:-0}" = "1" ]; then
    python3 "$HERE/tga_grey_levels.py" \
        "$RUN/home/base/screenshots/jkx_smoke.tga" || true

    if ! python3 "$HERE/tga_has_colour.py" \
        "$RUN/home/base/screenshots/jkx_smoke.tga" "235,235,235:2000"; then
        report "the menu model is not at the lighting value it should be; see \
the three numbers above this check in smoke_headless.sh for which defect each \
value means"
    fi
fi

# Texture coordinates over time.
#
# Five squares, five colours, one tcMod each, photographed twice with two
# seconds of engine time between the shots. What the counts have to do:
#
#   ref      NO tcMod at all, and it must hold still. This is the noise floor,
#            and it is in the lane rather than in someone's memory: if the
#            control moves, the whole frame is drifting and not one of the other
#            four numbers below means anything. Twice on this project an effect
#            was announced that turned out to be its own noise
#   scroll   tcMod scroll along u, the axis the texture is split on. Measured by
#            WHERE its colour is, like the rotation and for the same reason: a
#            count of coverage is fragile against a periodic motion, and this
#            one passed at fifty-six and a hundred and thirteen locally and then
#            came back at ONE under CI load. A scrolling edge translates, so its
#            centroid translates with it and cannot alias back. Eight thousandths
#            of the frame, monotonic across the three shots, gated at four
#   rotate   tcMod rotate, and this one is measured by WHERE its colour is
#            rather than how much of it there is. Rotation is about the middle
#            of the texture, so any region bounded by a line through the middle
#            covers about half the square at every angle and its count hardly
#            moves - which is how this check went red in CI at a spread of two.
#            Its texture is a corner block instead, off centre, and what orbits
#            is the centroid. It shifts eight to ten thousandths of the frame
#            and is gated at four: the signal is small but MONOTONIC - the
#            centre walks one way across the three shots - which is what a
#            count of a centred region never was
#   stretch  tcMod stretch
#   scale    tcMod scale, which does NOT animate. It changes the picture once
#            and then holds still, so it is checked the other way round: equal
#            between the two shots, and different from the red control in the
#            same shot - two texels across instead of one is half as much colour.
#            It is allowed four pixels of movement and the control is allowed
#            none: the control is what says the frame is not drifting, and a
#            control with a tolerance says nothing. Measured, the static square
#            wobbles by one pixel on an edge and the control by exactly zero
#
# No colour here is red-dominant, and that is not taste: the seam check further
# down reads the red-dominant pixels of the frame, because the fixture's light
# grid is red. The first run of this lane painted its control square pure red
# and was duly reported as a shading step across a model seam.
#
# --min-pixels is what stops "it held still" being satisfied by a square that
# is not drawn at all. Zero and zero are equal too.
#
# The floors on the moving three are about a third of what two consecutive runs
# measured - scroll 58 and 56, rotate 8 and 13, stretch 93 and 58 - which leaves
# room for the clock landing differently and none for a tcMod that is not
# running. The rotating square is the smallest of the three because rotation is
# nearly invariant on a straight-split texture; its texture is split on the
# diagonal for that reason and it still moves least.
if [ "${JKX_SMOKE_TCMOD:-0}" = "1" ]; then
    first="$RUN/home/base/screenshots/jkx_inmap.tga"
    later="$RUN/home/base/screenshots/jkx_tc_later.tga"
    later2="$RUN/home/base/screenshots/jkx_tc_later2.tga"

    if [ ! -f "$first" ] || [ ! -f "$later" ] || [ ! -f "$later2" ]; then
        report "the texture-coordinate squares were not photographed three \
times, so nothing here was checked"
    elif ! python3 "$HERE/tga_colour_change.py" "$first" "$later" "$later2" \
            --min-pixels 500 \
            --same 0,255,128 \
            --move 0,128,255:4 \
            --move 128,0,255:4 \
            --differ 0,200,100:20 \
            --same 100,0,200:4 \
            --unlike 100,0,200=0,255,128; then
        report "a tcMod did not do what it says; the line above names the \
colour, and the table above this check says which keyword owns it"
    fi
fi

# Geometry that moves.
#
# Same three shots as the texture-coordinate lane and the same control
# argument, with one difference that matters: deformVertexes move translates a
# surface RIGIDLY, so its pixel count is the same number wherever it has gone.
# Counting it says nothing. Where its centre is says everything, which is what
# --move measures.
if [ "${JKX_SMOKE_DEFORM:-0}" != "0" ]; then
    first="$RUN/home/base/screenshots/jkx_inmap.tga"
    later="$RUN/home/base/screenshots/jkx_tc_later.tga"
    later2="$RUN/home/base/screenshots/jkx_tc_later2.tga"

    if [ ! -f "$first" ] || [ ! -f "$later" ] || [ ! -f "$later2" ]; then
        report "the deform squares were not photographed three times, so \
nothing here was checked"
    elif ! python3 "$HERE/tga_colour_change.py" "$first" "$later" "$later2" \
            --min-pixels 500 \
            --same 0,255,102 \
            --differ 0,102,255:100 \
            --move 102,0,255:20; then
        report "a deformVertexes did not do what it says; the line above names \
the colour and jkx_smoke.shader says which keyword owns it"
    fi

    # Reported and not gated, because it is the open defect rather than a
    # regression to guard: on the vertex-buffer path the bulge square is not
    # drawn at all. See stage_smokedeform in tools/ci/local.sh.
    python3 "$HERE/tga_colour_change.py" "$first" "$later" "$later2" \
        --differ 0,204,102:20 || true
fi

# The physical map, written six ways, read off the roughness debug view.
#
# Six packings of the same three quantities in six channel orders, each carrying
# a DIFFERENT roughness, and each must come back as the exact byte the shader's
# arithmetic predicts. mix( 0.01, 1.0, v/255 ) * 255, rounded:
#
#   rmo    file R  20    ->  22
#   rmos   file R  60    ->  62
#   moxr   file A 100    -> 102
#   mosr   file A 140    -> 141
#   orm    file G 180    -> 181
#   orms   file G 220    -> 220
#
# Six different values rather than six identical ones and a control: six exact
# numbers cannot come out right by accident, and a renderer that ignored the
# texture would collapse all six onto one level. The control is built into the
# spread.
#
# Read off the DEBUG VIEW rather than the shaded picture, and the second reason
# is the important one. The first is that the debug view is the value itself, so
# the check is an equality against arithmetic rather than against a shade
# somebody picked. The second is that the shaded picture cannot be used at all
# right now: a physically-based WORLD surface comes out black on this renderer,
# which is a separate open defect written up at stage_smokephys in
# tools/ci/local.sh. Reading the texture rather than the lighting is what lets
# the six packings be checked while that stands.
#
# Twelve hundred pixels is about two thirds of a square, measured at 1936 to
# 1980.
if [ "${JKX_SMOKE_PHYS:-0}" = "1" ] && [ "${JKX_SMOKE_PHYS_VIEW:-3}" = "0" ]; then
    # THE SHADED PICTURE, which used to be black and is the defect this lane was
    # written around rather than for.
    #
    # A physically-based world surface came out rgb(0,0,0) on this renderer for
    # months, and five debug views appeared to contradict each other about why:
    # NL zero, ambient rgb(185,63,63), diffuse white, and the sum black. One of
    # them was lying. Fs is not zero when it reads black - it is NaN, which
    # writes to an unsigned normalised target as zero - and reflectance = Fd + Fs
    # carried the NaN into the first term of the lighting, where adding the
    # ambient term to it changed nothing. isnan(Fs) painted the squares pure red
    # on a probe and settled it in one run.
    #
    # The source was the reciprocal in V_SmithJointApprox, whose denominator goes
    # to zero on any surface facing away from the light. See the long note there.
    #
    # 185, 63, 63 is not a shade somebody liked: it is ambientColor times the
    # albedo, measured on the term itself, and it is what the sum has to be when
    # NL is zero and the specular term is honestly zero rather than poisoned.
    if ! python3 "$HERE/tga_grey_levels.py" \
        "$RUN/home/base/screenshots/jkx_inmap.tga" --any |
        grep -q 'rgb(185, 63, 63) x[0-9]\{4,\}'; then
        report "the shaded physical surfaces are not ambient times albedo; \
black here means the NaN in the specular term is back - see the note above"
    fi
elif [ "${JKX_SMOKE_PHYS:-0}" = "1" ]; then
    if ! python3 "$HERE/tga_grey_levels.py" \
        "$RUN/home/base/screenshots/jkx_inmap.tga" \
        --expect 22:1200 --expect 62:1200 --expect 102:1200 \
        --expect 141:1200 --expect 181:1200 --expect 220:1200; then
        report "a physical map packing does not unpack to the value it carries; \
the level that is missing names the packing - see the table above this check"
    fi

    # THE SHADED PICTURE, which used to be black and is the defect this lane was
    # written around rather than for.
    #
    # A physically-based world surface came out rgb(0,0,0) on this renderer for
    # months, and five debug views appeared to contradict each other about why:
    # NL zero, ambient rgb(185,63,63), diffuse white, and the sum black. The
    # answer was that one of them was lying. Fs is not zero when it reads black -
    # it is NaN, which writes to an unsigned normalised target as zero - and
    # reflectance = Fd + Fs carried the NaN into the first term of the lighting,
    # where adding the ambient term to it changed nothing. isnan(Fs) painted the
    # squares pure red on a probe and settled it in one run.
    #
    # The source was the reciprocal in V_SmithJointApprox, whose denominator goes
    # to zero on any surface facing away from the light. See the long note there.
    #
    # 185, 63, 63 is not a shade somebody liked: it is ambientColor times the
    # albedo, measured on the term itself, and it is what the sum has to be when
    # NL is zero and the specular term is honestly zero rather than poisoned.

fi

# Blending, and the order surfaces are drawn in.
#
# Four squares in one frame: an opaque backdrop and three translucent ones in
# front of it, one per blendFunc. All four are grey, so the histogram of grey
# levels IS the four composites, and each of them is arithmetic:
#
#   backdrop   the constant colour, whatever byte the engine turns it into
#   add        dst + src           backdrop + a quarter
#   blend      src*a + dst*(1-a)   white at a quarter alpha over the backdrop
#   filter     dst * src           half the backdrop
#
# It is also a sorting check without being written as one, and that is worth
# saying: all three of those numbers are wrong if the backdrop is drawn AFTER
# the squares - they would blend against the sky and the floor instead, and the
# values would be nowhere near. A translucent surface that ends up in front of
# something it should be behind is the defect this lane exists to catch, and it
# shows up as a different number rather than as a shrug.
# What it measured, first time out, and every one of the four was predicted
# before the run rather than read off it:
#
#   backdrop   rgbGen const 0.4        0.4 * 255 = 102          102, 73862 px
#   add        102 + (0.25 * 255)      102 + 63  = 165          165,  4550 px
#   blend      255*a + 102*(1-a), a = 63/255     = 139.8        140,  4278 px
#   filter     102 * (127/255)                   =  50.8         51,  4620 px
#
# The floors below are about two thirds of the measured counts. These are static
# squares in a settled frame, so the count does not drift the way the map-prop
# lane's early shot did - but a floor is a floor and a surface that half
# disappears should fail rather than squeak through.
#
# What each failure means, which is the reason for gating the LEVEL and not just
# "something is there": a missing level is that blendFunc not being applied, and
# a level that is present at the wrong value is the blend being computed against
# the wrong thing - the sky or the floor instead of the backdrop, which is what
# a sorting defect looks like from here.
if [ "${JKX_SMOKE_TRANSPARENCY:-0}" = "1" ]; then
    if ! python3 "$HERE/tga_grey_levels.py" \
        "$RUN/home/base/screenshots/jkx_inmap.tga" \
        --expect 102:40000 --expect 165:3000 \
        --expect 140:3000 --expect 51:3000; then
        report "a blended surface is not the colour the blend arithmetic says \
it should be; see the table above this check in smoke_headless.sh for which \
level belongs to which blendFunc"
    fi
fi

# The map's furniture, and whether it is there when the level appears.
#
# Two frames of the same scene, thirty frames apart and two hundred frames
# apart, and the whole assertion is that they agree. A prop that is in the
# settled frame and not in the early one is a prop that arrived after the level
# did - which is the shape of what was reported from hardware, a tree in the
# first mission that is not there and then is.
#
# An equality on the count rather than presence in both, because "arrived late"
# is not the only way to be wrong and the others are cheaper to catch here than
# anywhere else: a prop drawn at the wrong LOD, or drawn and then re-registered
# under a different handle, changes the count without ever being absent.
if [ "${JKX_SMOKE_MAPENT:-0}" = "1" ]; then
    early="$RUN/home/base/screenshots/jkx_prop_early.tga"
    late="$RUN/home/base/screenshots/jkx_inmap.tga"

    if [ ! -f "$early" ] || [ ! -f "$late" ]; then
        report "the map's prop was never photographed, so nothing here was checked"
    else
        # Cyan, and nothing else in this fixture is. Five thousand is well under
        # the five thousand seven hundred odd it measures and far above any
        # stray blend at its edge, so this fails on a missing prop rather than
        # on antialiasing.
        for shot in "$early" "$late"; do
            if ! python3 "$HERE/tga_has_colour.py" "$shot" "0,255,255:5000"; then
                report "the map's own model entity is not in $(basename "$shot")"
            fi
        done

        count() {
            python3 "$HERE/tga_has_colour.py" "$1" "0,255,255:1" 2>/dev/null \
                | sed -n 's/.*x\([0-9]*\)$/\1/p'
        }
        a="$(count "$early")"
        b="$(count "$late")"
        if [ -n "$a" ] && [ -n "$b" ] && [ "$a" != "$b" ]; then
            report "the map's prop is $a pixel(s) sixty frames in and $b at two \
hundred; it is still settling after the level has appeared"
        fi
    fi
fi

# The character wearing something, after being asked for a skin that does not
# exist.
#
# jkx/skin_body is rgbGen const ( 0 1 0 ), so it is exactly 0,255,0 whatever the
# light grid is doing - which is why the fixture's skins are written that way and
# why this can be an equality rather than a tolerance. The model beside it names
# no shader of its own, so if the fallback to model_default.skin is not there
# these pixels are not green, they are the default shader.
#
# Green anywhere in the middle of the frame is the whole assertion. Where exactly
# a man stands in it depends on the third-person camera, and this check is about
# what he is wearing.
if [ "${JKX_SMOKE_SKINFALL:-0}" = "1" ]; then
    if [ ! -f "$RUN/home/base/screenshots/jkx_char_front.tga" ]; then
        report "the character was never photographed, so nothing here was checked"
    elif ! python3 "$HERE/tga_colour_where.py" \
        "$RUN/home/base/screenshots/jkx_char_front.tga" \
        "0,255,0@0.25,0.0,0.75,1.0"; then
        report "a skin that would not register left the model with no textures - the fallback to model_default.skin is missing"
    fi
fi

# A shading seam, closed.
#
# This is the first check on this bench that can see a lighting change at all.
# Every other model here is rgbGen const, so no normal, light or shading term
# could move a pixel of one; the seam model is lit through the light grid and
# its colour is a function of its normals. See make_test_seam.py.
#
# What is measured is the biggest jump between two neighbouring pixels inside the
# model - not a colour, because both sides of a seam are the same material lit
# differently. Measured, with the model held still and everything else identical:
#
#     r_weldModelNormals 0    biggest step 94
#     r_weldModelNormals 1    biggest step  1
#
# so the gate sits at twenty: far above what a welded model produces and far
# below an unwelded one. It is a real A/B rather than a guess at a tolerance, and
# it is the negative control for the weld as well - turn the cvar off and this
# stage fails.
#
# WITH NORMAL MAPPING ON THE NUMBER IS DIFFERENT, and the reason is a defect
# rather than noise, so it gets its own limit rather than a widened one.
#
# Measured across the fix for the NaN in the specular term, same model, same
# camera, same binary otherwise:
#
#   r_normalMapping 0                     37035 pairs, biggest step  1
#   r_normalMapping 1, specular poisoned  37035 pairs, biggest step  1
#   r_normalMapping 1, specular honest    42983 pairs, biggest step 35
#
# The middle row is the point. While Fs was NaN the specular term wrote black,
# those pixels fell outside the range this check measures, and the seam was
# invisible because half the shading was missing. Fixing the NaN did not create
# a seam; it stopped hiding one.
#
# What the remaining step is: welding closed the NORMAL discontinuity, and the
# specular term also depends on the TANGENT, which is still discontinuous across
# a UV seam - the basis flips and the reflection vector with it. That is open
# defect 7 in the notes, the half described there as "the pattern, not the
# lighting", and it now has a number: thirty-five.
#
# Forty rather than twenty here, so the lane fails on a regression from
# thirty-five without failing on thirty-five itself. When the tangent seam is
# fixed this comes back to twenty and the two limits become one again.
SEAM_MAX_STEP=20
if [ "${JKX_SMOKE_PBR:-0}" = "1" ] || [ "${JKX_SMOKE_PHYS:-0}" = "1" ]; then
    SEAM_MAX_STEP=40
fi

if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ] && [ "${JKX_SMOKE_FOG:-0}" != "1" ] \
   && [ -f "$RUN/home/base/screenshots/jkx_seam.tga" ]; then
    if ! python3 "$HERE/tga_max_step.py" \
        "$RUN/home/base/screenshots/jkx_seam.tga" \
        --dominant r --min 40 --max-step "$SEAM_MAX_STEP"; then
        report "the model's shading has a hard step across a seam its normals should have closed"
    fi
fi

# The same model, drawn through the other path, lit at all.
#
# One frame instead of two decides which of two draw paths the engine uses
# (tr_mesh.cpp): the vertex buffer for a single frame, the batch for more. They
# arrive at a colour by different routes and only one of them was ever exercised
# here.
#
# The check is on RANGE rather than on a step, and that is the point: the strip's
# pairs point their normals around a circle, so a model that is lit has light and
# dark parts, and a model whose shading ignores its normals is one flat colour -
# which has no steps in it either, so a step check alone would pass it.
#
# Measured: a hundred and twenty-seven shades down this path. The threshold is
# thirty, which is far above the two or three units a flat surface produces and
# far below what a working one gives.
if [ "${JKX_SMOKE_PLAIN:-0}" != "1" ] && [ "${JKX_SMOKE_FOG:-0}" != "1" ] \
   && [ -f "$RUN/home/base/screenshots/jkx_seam1.tga" ]; then
    if ! python3 "$HERE/tga_max_step.py" \
        "$RUN/home/base/screenshots/jkx_seam1.tga" \
        --dominant r --min 40 --min-range 30; then
        report "a model drawn through the vertex buffer is not lit by its normals"
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
#
# Except when JKX_SMOKE_MAP has replaced the map the run loads: the truncated
# map is part of the fixture's own second-map sequence, and a run pointed at a
# different map never reaches it. Not a failure, just a check that does not
# apply - the same way the picture assertions step aside.
if [ -z "${JKX_SMOKE_MAP:-}" ] &&
   ! grep -q -- 'shorter than a BSP header' "$RUN/run.log"; then
    report "the deliberately broken map was not loaded and rejected, so the hunk was never cleared"
fi

if grep -qE 'Segmentation fault|signal SIGSEGV|SIGABRT' "$RUN/run.log"; then
    report "the engine died on a signal"
fi

# Checked from the log rather than from the exit code, because UndefinedBehavior
# Sanitizer prints and carries on by default: a build that reports on every frame
# still exits zero. This is what makes running the sanitizer build worth
# anything - building it and never running it checks nothing.
# The `|| true` on the line that PRINTS is not decoration. This script runs
# under set -e with pipefail, the condition above is a wider pattern than the
# detail below it, and grep exits non-zero when it matches nothing - so a run
# that detected a problem and then could not name it KILLED THE SCRIPT at that
# line. Every check after it is skipped, the closing "OK:" never prints, and
# what comes out looks like an ordinary failure with a truncated log.
#
# It is the same defect in both blocks, and it went unnoticed because a
# mismatch between the two patterns had never happened. It happens now: see
# the validation block below.
if grep -qE 'runtime error:|AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer' "$RUN/run.log"; then
    report "a sanitizer had something to say:"
    grep -E 'runtime error:|ERROR: (Address|Leak)Sanitizer' "$RUN/run.log" | head -10 || true
fi

if [ "$VALIDATION" = "1" ]; then
    if grep -qE 'VUID-|Validation Error|Validation Warning' "$RUN/run.log"; then
        report "the validation layer had something to say:"

        # The summary by name first, because a repeated VUID is one defect and a
        # count of fifteen thousand is one number.
        grep -oE 'VUID-[A-Za-z0-9-]+' "$RUN/run.log" | sort | uniq -c | sort -rn | head -10 || true

        # And then the messages that carry no VUID at all, which is what this
        # printed nothing for: the condition above accepts a bare "Validation
        # Error", the summary above it only extracts VUID- names, and a run whose
        # only complaint was unnamed announced that the layer had something to
        # say and then said nothing - and, under set -e, stopped.
        #
        # Not a hypothetical. With r_cubeMapping 1 every message is of that kind:
        # UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout, no VUID number,
        # twelve of them, and the run died at the line above without printing one.
        if ! grep -qE 'VUID-' "$RUN/run.log"; then
            grep -E 'Validation Error|Validation Warning' "$RUN/run.log" \
                | cut -c1-200 | sort -u | head -10 || true
        fi
    else
        # Said out loud, because until now a clean run under the layer and a run
        # where the layer never attached printed the same thing: nothing.
        echo "  (validation layer on, no complaints)"
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

# The fall, out of the trace, and the jump after it.
#
# The fall is the measurement: how long it took, in simulated milliseconds, to
# drop a distance the room fixes. Nothing is pressed during it, so the only
# thing that can change the answer is the size of the integration step - which
# is the whole question. The first airborne run of frames after the trace opens
# is the fall; everything after it belongs to the jump.
#
# The jump only gates, and only on "he left the ground". How high is not gated
# and must not be: it is measured through force jump, which keeps assigning the
# vertical velocity for as long as the key is held, and the key is held for a
# number of frames rather than milliseconds. See stage_move in tools/ci/local.sh.
if [ "${JKX_SMOKE_MOVE:-0}" = "1" ]; then
    MOVE_STATS="$(awk '/movetrace /{
            ground = ""
            for ( i = 1; i <= NF; i++ ) {
                if ( substr( $i, 1, 5 ) == "msec=" ) { msec = substr( $i, 6 ) + 0 }
                if ( substr( $i, 1, 4 ) == "org=" ) { z = $(i + 2) + 0 }
                if ( substr( $i, 1, 4 ) == "vel=" ) { vz = $(i + 2) + 0 }
                if ( substr( $i, 1, 7 ) == "ground=" ) { ground = substr( $i, 8 ) + 0 }
            }
            n++
            # After the fall, so that the height he was dropped from is not
            # reported as how high he jumped.
            if ( fallDone && ( !apexSet || z > apex ) ) { apex = z; apexSet = 1 }
            if ( n == 1 || msec < lo ) { lo = msec }
            if ( n == 1 || msec > hi ) { hi = msec }
            total += msec

            # 1023 is ENTITYNUM_NONE: no ground under him.
            if ( !fallDone ) {
                if ( ground == 1023 ) { fallMs += msec; fallSteps++; impact = vz }
                else if ( fallSteps > 0 ) { fallDone = 1; restZ = z }
            }
        }
        END {
            if ( n ) {
                printf "%d %.4f %d %d %.1f %d %d %.1f %.4f\n",
                    n, apex, lo, hi, total / n,
                    fallMs, fallSteps, impact, restZ
            }
        }' "$RUN/run.log" )"

    if [ -z "$MOVE_STATS" ]; then
        report "the movement lane produced no trace at all"
    else
        set -- $MOVE_STATS
        MOVE_SAMPLES="$1"; MOVE_APEX="$2"; MOVE_LO="$3"; MOVE_HI="$4"; MOVE_MEAN="$5"
        MOVE_FALLMS="$6"; MOVE_FALLSTEPS="$7"; MOVE_IMPACT="$8"; MOVE_REST="$9"
        echo "  fall $MOVE_FALLMS ms over $MOVE_FALLSTEPS step(s), impact $MOVE_IMPACT, rest z=$MOVE_REST"
        echo "  jump apex z=$MOVE_APEX, step $MOVE_LO-$MOVE_HI ms, mean $MOVE_MEAN, $MOVE_SAMPLES frame(s)"
        if [ -n "${JKX_SMOKE_MOVE_OUT:-}" ]; then
            printf '%s\n' "$MOVE_STATS" > "$JKX_SMOKE_MOVE_OUT"
        fi
        if [ "$MOVE_SAMPLES" -lt 30 ]; then
            report "the movement lane got only $MOVE_SAMPLES server frame(s)"
        fi
        if [ "$MOVE_FALLSTEPS" -lt 10 ]; then
            report "the player did not fall: $MOVE_FALLSTEPS airborne step(s)"
        fi
        # He is dropped from an origin of 200 and the floor puts him at about
        # -39.9. A rest position anywhere else means the floor moved or he went
        # through it, and both have happened here.
        if ! awk -v z="$MOVE_REST" 'BEGIN { exit !( z < -39 && z > -41 ) }'; then
            report "the player came to rest at z=$MOVE_REST, not on the floor"
        fi
        # The spawn is at z = -40. A jump that did not happen never rises above
        # it.
        if ! awk -v a="$MOVE_APEX" 'BEGIN { exit !( a > -30 ) }'; then
            report "the player did not leave the ground: apex z=$MOVE_APEX"
        fi
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo
    echo "--- last 40 lines ---"
    tail -40 "$RUN/run.log"
    exit 1
fi

echo "OK: the engine drew frames on the Vulkan renderer and quit on its own"
