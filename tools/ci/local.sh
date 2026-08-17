#!/usr/bin/env bash
# Run what CI runs, here, before pushing.
#
# This exists because the alternative was discovered the expensive way: a fix
# pushed, fifteen minutes of waiting, a failure in a configuration nobody built
# locally, another fix. Release built here and Debug only in CI is how the
# ragdoll debug callbacks - dead code in every build anyone had run - stayed
# broken across three pushes.
#
# What it covers:
#   policy      the ascii, layering, interface, source-list, string, cvar and
#               per-game-branch gates, the MSVC dialect traps, and actionlint
#   release     the whole tree, Release
#   debug       the whole tree, Debug: different code is compiled, and it is
#               the configuration nobody looks at until it fails
#   windows     a MinGW-w64 cross-build. Not MSVC, but it compiles every
#               #ifdef _WIN32 branch in the tree against real Windows headers -
#               a whole platform's worth of code whose first compiler used to be
#               fifteen minutes away in CI
#   sanitizers  Debug with asan and ubsan, which is what the CI job builds
#   tests       the unit tests, including the font atlas generator's, which
#               redraws its own output and compares it against the bitmap it
#               was built from
#   smoke       the engine drawing frames on the Vulkan renderer, headless,
#               under the validation layer
#   smokewide   the same at 32:9, where the interface's arithmetic is checked
#               against the picture: at 4:3 the fitted frame is the whole window
#               and a wrong mapping looks exactly like a right one
#   smokejk2    the same run as jkx_jk2, which is the same engine built with
#               -DJK2_MODE against games/jk2/game. Not a duplicate of the run
#               above: the string packages, the whole of games/jk2/cgame and every
#               JK2_MODE branch in shared code are only reached here, and the
#               first time it was run it found a new[]/delete mismatch that
#               corrupted the heap on every JK2 shutdown. It reaches the map,
#               which took a generated skeleton: JK2 hard-codes "kyle" as the
#               player model and errors on a missing animation set
#   smokesave   the same run plus a savegame round trip: save in the map, load
#               it back, and check the frame that comes out. Until the Ghoul2
#               serialisers were ported from single-player, saving wrote no
#               chunk and loading dereferenced a null pointer
#   smokesan    the same run against the sanitizer build. Building sanitizers
#               and never running them checks nothing: the first time this was
#               run it reported two misaligned accesses in the zone allocator,
#               on the first allocation the engine makes
#   fog         the fixture with a fog volume in it, drawn with the fog pass off
#               and on from one standing position. RB_FogPass had never run in a
#               headless test; the fog is in its own lane because a global fog
#               repaints the clear colour and moves every other colour check
#   prepass     the fixture drawn twice, with the depth pre-pass off and on, and
#               the two sets of frames compared. It is a change that must not
#               change the picture, and "the map still loads" cannot tell that
#               from a wall that has gone missing behind the camera
#
# What it cannot cover: MSVC itself - its dialect and its linker. That used to
# be read as "Windows", which is a much larger thing; the windows stage compiles
# the platform's code here, so what is left to fail remotely is only what is
# specific to Microsoft's compiler.
#
# That remainder is not left to chance either. Every time MSVC rejects a shape
# that every compiler here accepts, the shape goes into check_msvc.py and the
# policy stage catches the next one in two seconds instead of twenty minutes.
#
# Usage:
#   tools/ci/local.sh [stage ...]        default: all of them, in this order

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_ROOT="${JKX_LOCAL_BUILD_ROOT:-/tmp/jkx-local}"
JOBS="${JOBS:-$(nproc)}"

STAGES=( "$@" )
if [ "${#STAGES[@]}" -eq 0 ]; then
    STAGES=( policy release debug windows sanitizers tests smoke smokewide smokejk2 smokesave smokeskin smokelightmap smokemapent smokemenumodel smokemenulight smokepbrchar smokevidrestart smokecubemap smoketransparency smoketcmod smokedeform smokephys smokepak move smokesan prepass fog noassets )
fi

failed=()
run() {
    local name="$1"; shift
    printf '\n=== %s ===\n' "$name"
    if "$@"; then
        printf '  ok\n'
    else
        printf '  FAILED\n'
        failed+=( "$name" )
    fi
}

configure() {
    local dir="$1"; shift
    cmake -S "$ROOT" -B "$dir" -G Ninja "$@" >/dev/null
}

stage_policy() {
    python3 "$ROOT/tools/ci/check_ascii.py" "$ROOT" &&
    python3 "$ROOT/tools/ci/check_layering.py" "$ROOT" &&
    python3 "$ROOT/tools/ci/check_interface.py" "$ROOT" &&
    python3 "$ROOT/tools/ci/check_sources.py" "$ROOT" &&
    python3 "$ROOT/tools/ci/check_strings.py" "$ROOT" &&
    python3 "$ROOT/tools/ci/check_cvars.py" "$ROOT" &&

    # Every Vulkan object the renderer creates, against something that destroys
    # it. An object alive at vkDestroyDevice is a defect no compiler sees and a
    # software rasteriser does not complain about; the hardware does.
    python3 "$ROOT/tools/ci/check_vk_objects.py" "$ROOT" || return 1
    python3 "$ROOT/tools/ci/check_jk2mode.py" "$ROOT" &&
    python3 "$ROOT/tools/ci/check_msvc.py" "$ROOT" &&
    python3 "$ROOT/tools/ci/check_commits.py" &&
    stage_workflows
}

# An invalid workflow file fails with no log to read, so it is worth catching
# here. Skipped rather than failed when actionlint is not installed: this script
# should not need the network.
#
# The skip is loud on purpose. It read as "(not checked)" once and got skimmed
# past, and CI then went red on two shellcheck findings in workflow shell that
# nothing local had looked at - which is the exact failure this stage exists to
# prevent. If it says SKIPPED, the workflows are unchecked and CI is the first
# thing that will look at them.
stage_workflows() {
    local lint
    lint="$(command -v actionlint || true)"
    if [ -z "$lint" ]; then
        echo "  SKIPPED: actionlint is not installed, so nothing here has read"
        echo "           the workflow files. CI will, and it can go red on shell"
        echo "           inside them. Install it with:"
        echo "             curl -sSfL https://github.com/rhysd/actionlint/releases/download/v1.7.7/actionlint_1.7.7_linux_amd64.tar.gz | tar xz actionlint"
        return 0
    fi
    # actionlint without shellcheck is not a smaller check, it is a different
    # one: the YAML is read and the shell inside "run:" is not. That is the half
    # this stage exists for, and it degrades in silence - actionlint says nothing
    # about the tool it could not find and exits zero.
    #
    # It has now cost two red builds. The second was a run: block whose folded
    # scalar had a whitespace-only line left in the middle of it, so the command
    # was cut in two and the second half began with -DJKX_VK_TRACE. shellcheck
    # calls that SC2215, "this flag is used as a command name"; local actionlint
    # called it nothing at all and the stage passed.
    if ! command -v shellcheck >/dev/null; then
        echo "  WARNING: shellcheck is not installed, so actionlint has read the"
        echo "           YAML and skipped every run: block in it. That is where"
        echo "           the last two workflow failures were. Install it with:"
        echo "             apt-get install -y shellcheck"
    fi
    ( cd "$ROOT" && "$lint" -no-color )
}

stage_release() {
    configure "$BUILD_ROOT/release" -DCMAKE_BUILD_TYPE=Release &&
    cmake --build "$BUILD_ROOT/release" --parallel "$JOBS"
}

stage_debug() {
    configure "$BUILD_ROOT/debug" -DCMAKE_BUILD_TYPE=Debug &&
    cmake --build "$BUILD_ROOT/debug" --parallel "$JOBS"
}

stage_windows() {
    bash "$ROOT/tools/ci/win_cross.sh" "$BUILD_ROOT/win"
}

stage_sanitizers() {
    configure "$BUILD_ROOT/san" -DCMAKE_BUILD_TYPE=Debug \
        -DJKX_ENABLE_ASAN=ON -DJKX_ENABLE_UBSAN=ON &&
    cmake --build "$BUILD_ROOT/san" --parallel "$JOBS"
}

stage_tests() {
    python3 "$ROOT/tools/verify/selftest.py" &&
    python3 "$ROOT/tools/fontgen/selftest.py" &&
    python3 "$ROOT/tools/fontgen/build_fonts.py" --check &&
    python3 "$ROOT/tools/verify/make_test_bsp.py" --check &&
    python3 "$ROOT/tools/verify/make_test_material.py" --check &&
    python3 "$ROOT/tools/verify/make_test_glm.py" --check &&
    python3 "$ROOT/tools/verify/make_test_gla.py" --check &&
    python3 "$ROOT/tools/verify/make_test_seam.py" --check &&
    python3 "$ROOT/tools/verify/glm_strip_shaders.py" --check &&
    stage_tests_cxx
}

# The C++ checks that need no renderer. Compiled here rather than through CMake
# because they depend on one header apiece and nothing else, and a test that
# takes two seconds to build is a test people run.
stage_tests_cxx() {
    local out="$BUILD_ROOT/tests"
    mkdir -p "$out" || return 1

    c++ -O2 -Wall -Werror -o "$out/sky_projection_test" \
        "$ROOT/tests/sky_projection_test.cpp" || return 1
    "$out/sky_projection_test" || return 1

    # The Outcast-to-Academy bone remap and, more to the point, the decision
    # about when to apply it. Neither half shows up in a frame until a character
    # is already folded up, and the bench draws no Outcast model at all.
    c++ -O2 -Wall -Werror -o "$out/ghoul2_bonemap_test" \
        "$ROOT/tests/ghoul2_bonemap_test.cpp" || return 1
    "$out/ghoul2_bonemap_test" || return 1

    # The Targa reader, against forty thousand malformed files. Built with the
    # sanitizers on and only here: the point of the mutation loop is that a read
    # or a write one byte outside a buffer is a fault, and without asan it is a
    # value nobody notices. Two seconds.
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$out/image_tga_test" \
        "$ROOT/tests/image_tga_test.cpp" \
        "$ROOT/code/rd-vulkan/tr_image_tga_decode.cpp" || return 1
    "$out/image_tga_test" || return 1

    # The BSP lump table, against forty thousand malformed headers. A map is
    # read before anything is drawn, so it is the earliest place a stranger's
    # bytes reach - and every lump offset in one used to be trusted.
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$out/bsp_header_test" \
        "$ROOT/tests/bsp_header_test.cpp" \
        "$ROOT/code/qcommon/cm_bsp_check.cpp" || return 1
    "$out/bsp_header_test" || return 1

    # The three model headers. A .glm arrives in a pk3 and R_LoadMDXM allocates
    # and copies ofsEnd bytes out of a buffer whose real length nothing had
    # looked at.
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$out/mdx_header_test" \
        "$ROOT/tests/mdx_header_test.cpp" \
        "$ROOT/code/rd-common/mdx_check.cpp" || return 1
    "$out/mdx_header_test" || return 1

    # Welding the normals of coincident model vertexes, and - the half that
    # matters - not welding the ones that are deliberate edges. The threshold is
    # the feature: in the retail kyle the angle between coincident normals runs
    # the whole way to a hundred and eighty degrees.
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$out/mdx_weld_test" \
        "$ROOT/tests/mdx_weld_test.cpp" \
        "$ROOT/code/rd-common/mdx_weld.cpp" || return 1
    "$out/mdx_weld_test" || return 1

    # Which game is in a directory. The launcher's first question, driven
    # through a fake filesystem so that the shapes players actually have -
    # half-deleted installs, merged folders, a mod directory pointed at by
    # mistake - can be asked about at all.
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$out/install_scan_test" \
        "$ROOT/tests/install_scan_test.cpp" \
        "$ROOT/code/launcher/jkx_install_scan.cpp" || return 1
    "$out/install_scan_test" || return 1

    # Which half of the launcher's command line is a directory and which half is
    # the engine's. Four lines of code and nine cases, because a launcher is
    # reached by dragging a folder onto it - so its argument list is whatever a
    # file manager, a shortcut or a shell script handed over, including an empty
    # word from an unset variable.
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$out/launcher_args_test" \
        "$ROOT/tests/launcher_args_test.cpp" \
        "$ROOT/code/launcher/jkx_launcher_args.cpp" || return 1
    "$out/launcher_args_test" || return 1

    # And where a game might be, against a machine that does not exist: a
    # registry, a set of directories and a libraryfolders.vdf all written by the
    # test. None of that can be reached from here, which is exactly why the
    # search takes it through callbacks.
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$out/install_find_test" \
        "$ROOT/tests/install_find_test.cpp" \
        "$ROOT/code/launcher/jkx_install_find.cpp" || return 1
    "$out/install_find_test" || return 1

    # The vector type, against the array it replaces. Layout first - it is
    # checked by writing through one view and reading through the other, not by
    # asking the type about itself - then every operator against the same
    # arithmetic spelled out the old way.
    c++ -O2 -std=c++17 -Wall -Wextra -Werror \
        -o "$out/vec3_test" "$ROOT/tests/vec3_test.cpp" || return 1
    "$out/vec3_test" || return 1

    # The second pass over a model file: every LOD, every surface, every bone.
    # The header test above covers the top-level arrays; this covers what is
    # inside them, which is where both formats nest and where every offset is a
    # number out of a stranger's pk3.
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$out/mdx_deep_test" \
        "$ROOT/tests/mdx_deep_test.cpp" \
        "$ROOT/code/rd-common/mdx_check.cpp" || return 1
    "$out/mdx_deep_test" || return 1

    # The savegame's run-length coding, round-tripped and then attacked. A .sav
    # is a file players send each other and every chunk of one goes through it.
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$out/rle_test" \
        "$ROOT/tests/rle_test.cpp" \
        "$ROOT/code/qcommon/jkx_rle.cpp" || return 1
    "$out/rle_test" || return 1

    # One zip handle, two readers. A pack keeps a single unzFile open and hands
    # it to every caller that did not ask for its own, and a zip handle carries
    # one position - so the second file opened from a pack used to take the
    # first one's place with nothing returning an error.
    #
    # minizip is third-party and is compiled without our warning set: it is not
    # our code to keep clean, and -Wextra -Werror on it stops the build on
    # unused callback parameters that are part of zlib's own interface.
    cc -O1 -g -w -I "$ROOT/third_party/minizip/include" \
        -I "$ROOT/third_party/minizip/include/minizip" \
        -c "$ROOT/third_party/minizip/unzip.c" -o "$out/unzip.o" || return 1
    cc -O1 -g -w -I "$ROOT/third_party/minizip/include" \
        -I "$ROOT/third_party/minizip/include/minizip" \
        -c "$ROOT/third_party/minizip/ioapi.c" -o "$out/ioapi.o" || return 1
    c++ -O1 -g -std=c++17 -Wall -Wextra -Werror \
        -I "$ROOT/third_party/minizip/include" \
        -o "$out/pk3_share_test" \
        "$ROOT/tests/pk3_share_test.cpp" "$out/unzip.o" "$out/ioapi.o" -lz || return 1
    "$out/pk3_share_test" "$out/pk3_share_test.pk3" || return 1

    # The sound codec, against a real compressed file. The headless bench never
    # plays one, so without this the decoder is unverified.
    c++ -O2 -std=c++20 -Wall -Werror -DARCH_STRING='"x86_64"' -DJKX_ENGINE \
        -I "$ROOT/code" -I "$ROOT/code/client" -I "$ROOT/shared" \
        -I "$ROOT/third_party" \
        -o "$out/snd_codec_test" \
        "$ROOT/tests/snd_codec_test.cpp" "$ROOT/code/client/snd_codec.cpp" \
        "$ROOT/third_party/stb/stb_vorbis.c" || return 1
    "$out/snd_codec_test" "$ROOT/tools/verify/fixtures" || return 1
}

# Every smoke lane below this line runs under the Vulkan validation layer, and
# until today only this one did.
#
# JKX_SMOKE_NO_VALIDATION=1 sat in fourteen of the fifteen lanes. It got there
# by copying: the first lane that needed it needed it for a real reason, and
# every lane written afterwards started from that lane's four lines. What it
# cost is not theoretical. The layer named the two worst crashes of the last two
# days in one line each - the physically-based path binding a descriptor set it
# had not filled, and the sky drawing with set zero unbound - and in both cases
# the lane that would have caught it first had the layer switched off.
#
# Before switching them on, all fifteen were surveyed with it on, and the result
# is the reason this is a small change rather than a project: THIRTEEN OF THEM
# WERE ALREADY CLEAN. Not one validation message between them. The two that are
# not:
#
#   smokevidrestart   13 x VUID-vkCmdDrawIndexed-None-02721
#                     10 x VUID-vkCmdDrawIndexed-None-04007
#                      1 x VUID-vkCmdDrawIndexed-viewType-07752
#                     the missing sky after a restart, which is what that lane
#                     is currently for. It stays out of the stage list.
#
#   smokesan          not surveyed and still off - the sanitizers and the layer
#                     are both slow and this bench has two cores.
#
# The cost is time, and it was measured rather than feared: with the layer on a
# lane takes between forty-four and ninety seconds, which is inside every
# timeout as they stand. No lane needed its timeout raised.
stage_smoke() {
    bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The depth pre-pass, checked the only way it can honestly be checked: it is a
# change that is supposed to draw exactly the same picture. Filling the depth
# buffer with the opaque geometry before shading it lets the hardware discard
# occluded fragments, and every way of getting it wrong - a vertex transformed
# differently in the two passes, a surface that writes depth and then does not
# write colour - shows up as geometry that disappears. Comparing frames catches
# that; a run that reaches the end of the map does not.
#
# The tolerance is sixteen pixels, and all three numbers behind it were
# measured rather than chosen.
#
# The floor: six pairs of identical runs differ by at most one pixel, on the
# edge of the fixture's floor where the horizon crosses a pixel boundary.
# Getting it that low was most of the work - the sky moved, the view settled
# onto the floor over real time rather than over frames, the third-person camera
# trailed, a console cursor blinked. JKX_SMOKE_PLAIN in smoke_headless.sh lists
# what had to go.
#
# The tail: that distribution is not bounded at one. A tolerance of two failed
# once at four pixels, in the same place, which is a flaky gate - and a gate
# that fails at random teaches people to ignore it, which is worse than not
# having it.
#
# The scale of a real defect: the failures this stage exists to catch are a
# surface that vanishes because its two passes disagree about depth. That is
# hundreds to thousands of contiguous pixels. Sixteen sits an order of magnitude
# above the noise and an order of magnitude below the smallest thing worth
# catching, which is the whole of the argument for it.
#
# Multisampling is off here, and jkx_sky.tga is out of the comparison. Both are
# scope rather than convenience, and the second one cost three wrong guesses,
# which is worth writing down because the wrong guesses were all plausible.
#
# The moment the fixture's floor became visible - it had been culled and never
# drawn, see make_test_bsp.py's drawindexes() - this lane went red on the sky
# frame, and only on the sky frame. Every other frame in the lane compares at
# zero.
#
#   Guess one: multisampling. The 1134 differing pixels were a quarter of the
#   way from black to white and never more, which is one sample of four. Turning
#   multisampling off took it to two pixels and looked like the answer. It was
#   not: it came back at 52.
#
#   Guess two: the screen wipe. The shot is taken after r_dissolveFreeze is set
#   back to -1, and how far a wipe gets in a given number of frames is a
#   question about real time. The wait went from twenty frames to two hundred.
#   Identical failure, same pixels.
#
#   Then the measurement, which should have come first. Dumping the differing
#   pixels: single pixels scattered along the line where the white floor meets
#   the black sky, white in one run and black in the other. And the control that
#   settles it - r_depthPrepass 0 against r_depthPrepass 0, the SAME setting on
#   both sides - differs by NINETY pixels on that frame, which is more than the
#   A/B comparison it was failing on.
#
# So this frame is not measuring the depth pre-pass. It is measuring where the
# floor's silhouette lands, which moves sub-pixel between runs and has done ever
# since there was a floor to have a silhouette. The pin in smoke_headless.sh -
# setviewpos before the shot - used to be enough when the frame was sky against
# nothing; it is not enough now.
#
# Skipping it keeps the other eight frames exact, which is what the lane is for.
# Widening the tolerance instead would have put it above ninety, and the
# defects this stage exists to catch - a surface that vanishes because its two
# passes disagree about depth - start at a few hundred contiguous pixels. That
# is not a margin, that is an overlap.
#
# What would earn the frame back: making the silhouette deterministic, which is
# a fixture question and is written down as one rather than left as a skip.

stage_prepass() {
    local a b rc
    a="$(mktemp -d)"
    b="$(mktemp -d)"
    rc=0

    prepass_run() {
        JKX_SMOKE_SET="r_depthPrepass=$1 r_ext_multisample=0" \
        JKX_SMOKE_PLAIN=1 \
        JKX_SMOKE_DISPLAY="$2" \
        JKX_SMOKE_SHOT_DIR="$3" \
            bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release" >/dev/null
    }

    prepass_run 0 "${JKX_SMOKE_PREPASS_DISPLAY_A:-:94}" "$a" || rc=1
    prepass_run 1 "${JKX_SMOKE_PREPASS_DISPLAY_B:-:93}" "$b" || rc=1

    if [ "$rc" -eq 0 ]; then
        python3 "$ROOT/tools/verify/ab_frames.py" "$a" "$b" --max-pixels 16 \
            --skip jkx_sky.tga || rc=1
    else
        echo "  one of the runs failed on its own terms; see it alone first"
    fi

    rm -rf "$a" "$b"
    return "$rc"
}

# The fog, in its own lane because a global fog repaints the clear colour and
# would move every colour check in the shared fixture. RB_FogPass had never run
# in a headless test at all: the generated map carried no fogs and the retail
# maps are not in this repository, so a second blended pass over every fogged
# surface, its shader permutation and its texture coordinate generation went
# unexecuted.
#
# The check is two frames from one standing position seconds apart, differing by
# r_drawfog alone - so the floor is the same floor, unfogged white in one and
# fog-coloured in the other.
stage_fog() {
    JKX_SMOKE_FOG=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_FOG_DISPLAY:-:91}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The engine with no material definitions at all - which is what an installation
# with its game data in the wrong place looks like. It was a three-word fatal
# error until now, so this stage is asserting that starting and explaining is
# possible, not that the picture is right. Sound is on here for the same reason:
# the fixture has no sound/sound.txt either, and that used to be ERR_FATAL.
stage_noassets() {
    JKX_SMOKE_NO_SHADERS=1 \
    JKX_SMOKE_SOUND=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_NOASSETS_DISPLAY:-:90}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

stage_smokewide() {
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_WIDE_DISPLAY:-:97}" \
    JKX_SMOKE_SCREEN=2560x720 \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The other game. jkx_jk2 is code/ built with -DJK2_MODE plus games/jk2/game, so
# this is a second configuration of the same engine rather than a second copy of
# the test - and half the project had nothing looking at it until this stage
# existed.
stage_smokejk2() {
    JKX_SMOKE_GAME=jk2 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_JK2_DISPLAY:-:96}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The savegame round trip. Its own stage because the run is long enough without
# it: the level counter's second map and a save-load round trip in one pass push
# a software rasteriser past the timeout.
#
# This stage used to say "validation is off, and that is an exception rather
# than a preference: four device objects survive to vkDestroyDevice on this
# path". They do not any more. The whole lane runs clean under the layer, and
# the note is left here rather than deleted because the sequence is worth
# keeping: the objects were named by a lane that had the layer switched off, and
# what closed them was three unrelated fixes elsewhere. Nobody went looking.
stage_smokesave() {
    JKX_SMOKE_SAVELOAD=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_SAVE_DISPLAY:-:95}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The player falls, and then jumps.
#
# The fall is the measurement and the jump is the gate. A fall has no input in
# it: the player is dropped and gravity is integrated until he lands, so the
# speed he arrives at is a property of the integration alone. On a busy machine
# a whole fall takes ten steps and arrives seven per cent slow; with
# pmove_fixed on, the loaded runs land on the idle numbers. The figures are in
# smoke_headless.sh beside the code that produces them.
#
# Nothing here is gated on either number yet, and that is deliberate: the fix
# they measure is off by default, so a gate would be asserting the defect. What
# is gated is that he fell at all, that he came to rest ON the floor rather than
# through it, and that the jump left the ground - which is what caught the
# fixture having no collision in the first place.
#
# The jump's height is not gated and should not be until the input is held for
# a stated number of milliseconds rather than three frames: force jump keeps
# assigning the vertical velocity while the key is down, so the input itself
# changes with the frame rate. Five identical runs gave 12.54, 10.45, 12.63,
# 10.63, 10.45.
stage_move() {
    JKX_SMOKE_MOVE=1 \
    JKX_SMOKE_PLAIN=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_MOVE_DISPLAY:-:87}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The same fixture, out of a pk3.
#
# Every run before this one read loose files, so the archive half of the
# filesystem had never executed here at all: FS_LoadZipFile, the per-pack hash
# table, the pak branch of FS_FOpenFileRead, unzReadCurrentFile in FS_Read and
# the pk3 case of FS_Seek. That is how retail assets arrive and how every
# downloaded mod arrives, and none of it was covered.
#
# make_pk3.py deletes what it packs, so there is no loose copy to fall back to
# and no way for this lane to pass by reading the directory instead. Sound is on
# because the sound cache is one of the few callers that opens a file, keeps the
# handle and reads it in pieces.
#
# What this still does not reach, so that the next person does not have to
# re-derive it: the pk3 case of FS_Seek, which needs a caller that seeks
# backwards in an archived file, and the archive path under the sanitizers - the
# san lane above reads loose files.
stage_smokepak() {
    JKX_SMOKE_PK3=1 \
    JKX_SMOKE_SOUND=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_PAK_DISPLAY:-:89}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# Leak detection is off: the engine frees its zone in one go at exit and reports
# what it freed, which is a different accounting from the one LeakSanitizer does,
# and the noise would bury the errors worth reading. The validation layer is off
# too - one slow thing at a time, and the release run above already ran it.
#
# Sound is on here and nowhere else in the fast stages. Every run before this one
# passed s_initsound 0, so the mixer, the codecs and the ambient set code had
# never executed under a sanitizer at all - which is the cheapest place to look
# at a subsystem the bench has been walking past.
# A character wearing a skin that does not exist.
#
# This lane exists because of a sentence in the gamecode that was wrong and had
# been wrong since 2003: "it still loads the default skin's tga's because
# they're referenced in the .glm". A retail model references nothing - every one
# of the eighty-two surfaces in kyle/model.glm has an empty shader name - so a
# skin that fails to register does not leave a model with its own textures, it
# leaves a model with none, drawn through the default shader.
#
# The Academy gamecode reaches that every time g_char_model names a model whose
# skins are not in three parts while g_char_skin_* still hold their defaults.
# Reproduced with the retail files on the bench, and then made reproducible
# without them: the lane strips the shader names out of the fixture's own model
# and asks for a three-part skin it does not have. Green means the fallback to
# model_default.skin happened; anything else means it did not.
#
# A character wearing a skin that does not exist, and the two numbering spaces
# staying in step while it happens.
#
# This lane has now found two separate defects and it is worth saying what each
# was, because the second one only became visible after the first was fixed.
#
#   A .glm names no shaders of its own - all eighty-two surfaces of the retail
#   kyle carry an empty shader name - so a skin that fails to register leaves a
#   model with no materials at all rather than with its own. The gamecode falls
#   back to model_default.skin now.
#
#   And RE_RegisterSkin took a handle before it read the file, so a failed
#   registration still consumed one. The gamecode stores a CONFIGSTRING INDEX in
#   mCustomSkin and the renderer resolves it as a HANDLE; those agree only
#   because both sides count the same skins in the same order, and one failure
#   put them off by one for everything after it. That is the shape of the bug a
#   person playing reported as "models with no textures", and it is why the
#   coincidence held for twenty years and then did not.
#
# The lane makes a registration fail on purpose, which is exactly the input that
# desynchronises them, so it measures both. Mutation tested twice: against the
# build before the fallback the colour is absent, and against the build before
# the handle was released it is absent again, for the second reason.
stage_smokeskin() {
    JKX_SMOKE_SKINFALL=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_SKIN_DISPLAY:-:88}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The world lit by a real lightmap, which had never happened here.
#
# Every surface in the fixture map was LIGHTMAP_BY_VERTEX with an empty lightmap
# lump, so R_LoadLightmaps, the lightmap atlas, the atlas texture coordinates
# and every lightmapped shader permutation were outside this bench entirely -
# and that is how every real map in both games is lit.
#
# It also makes the map's brightness a number. The ordinary floor is white by
# design, which is what makes the pixel checks exact and what makes "is the map
# brighter" unanswerable; the lightmap page is a flat 32 and its shader draws
# the page and nothing else, so the floor comes out at exactly
# 32 * 2^r_mapOverBrightBits. That question arrived from a player report about
# maps being too dark and the bench could not answer it at the time.
#
# Mutation tested: with the page at 200 instead of 32 the lane fails, and with
# r_mapOverBrightBits at anything but 2 it fails.
stage_smokelightmap() {
    JKX_SMOKE_LIGHTMAP=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_LIGHTMAP_DISPLAY:-:92}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# A model the MAP owns, which is a path this bench had never taken.
#
# Everything drawn here until now was put on screen by the console - testmodel -
# or was the player himself. A map's prop is a different route in every respect
# that matters: G_ModelIndex gives it a CONFIGSTRING index at spawn, and cgame
# turns that index into a renderer handle in two separate places, the loop in
# CG_RegisterGraphics behind the loading screen and CG_ConfigStringModified
# afterwards. Which of the two a prop goes through is the difference between a
# level that is furnished when it appears and one where the furniture arrives a
# moment later in front of the player, which is what was reported from hardware.
#
# The lane photographs the same scene at thirty frames and at two hundred and
# requires them to agree. What it says today is a negative and worth having:
# the plain configstring path is NOT the pop-in - the prop is drawn in the same
# frame as the floor and the two counts are equal. Mutation tested by building
# the map without the prop.
stage_smokemapent() {
    JKX_SMOKE_MAPENT=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_MAPENT_DISPLAY:-:86}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# A Ghoul2 model drawn by the menu system.
#
# Item_Model_Paint, its own refdef, its own light and ItemParse_asset_model_go
# had never executed on this bench. The menu system itself always has - the
# first screenshot of every run is the fixture's main menu - but every item in
# it was a coloured rectangle until now.
#
# It is honest about what it is not: it was written to measure the untextured
# lightsaber hilt and it does not, which the mutation test showed. See
# tools/verify/fixtures/base/ui/jkx_model.menu.
stage_smokemenumodel() {
    JKX_SMOKE_MENUMODEL=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_MENUMODEL_DISPLAY:-:85}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The same model, LIT rather than painted.
#
# The lane above draws it in a constant colour, which is what makes its check
# exact and also what makes it blind: a constant colour looks the same under any
# light, including none at all. So the menu's lighting - the no-world branch of
# R_SetupEntityLighting, which is the one every model a player sees before he
# has loaded anything goes through - had never reached a pixel on this bench.
#
# It was reported from hardware as "both models in the menu are burnt out", and
# this lane is what turned that into three numbers. jkx/menu_lit is white and
# lit, so the pixel IS the lighting sum read back off the screen, and the gate
# is an equality on it. What each value means is written above the check in
# smoke_headless.sh.
stage_smokemenulight() {
    JKX_SMOKE_MENULIGHT=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_MENULIGHT_DISPLAY:-:82}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The physically-based renderer, actually drawing something.
#
# This bench had a PBR lane for weeks and it never issued a single PBR draw. A
# shader permutation is generated from what a material asks for; no material in
# the fixture asked for a normal map, so the permutation that reads descriptor
# set five was never generated and the code that binds set five never ran. It
# was wrong in three separate ways, and all three were found the day a retail
# model with real maps was put in front of it:
#
#   the DrawItem path bound sets zero to four and pushed nothing into five, so
#   any Ghoul2 mesh with a normal map ran a pipeline that reads an unbound set;
#
#   the push checked an image's view and not its sampler, and a combined image
#   sampler needs both;
#
#   and the five images were remembered across map loads as pointers, which
#   R_Init frees underneath.
#
# The lane needs no retail data: two flat maps from make_test_material.py and
# the jkx/pbr_body material that names them, on the fixture's own character.
# Mutation tested against each of the three - the lane segfaults without any one
# of them.
stage_smokepbrchar() {
    JKX_SMOKE_PBRCHAR=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_PBRCHAR_DISPLAY:-:84}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# Tearing the renderer down with a map loaded and building it again.
#
# This lane crashed from the day it was written and was kept out of the stage
# list for it. What it was: both sky draw items were the only ones in the
# renderer that did not ask for their uniform descriptor set, so they recorded
# an empty range and RB_BindDescriptorSets bound nothing for them. In an
# ordinary run the cubemap sky pipeline does not exist and the draw is skipped;
# a renderer rebuilt by vid_restart has it, and the draw goes out with set zero
# unbound. VUID-vkCmdDrawIndexed-None-08600, and a segmentation fault in a
# lavapipe rasteriser thread.
#
# Mutation tested: put either sky item back to the old value and the lane
# segfaults again.
#
# Then it reported the next defect rather than crashing, which is what a lane is
# for: after a restart the sky came back as three faces that were "the wrong one,
# or the wrong way up", and the validation layer counted the reason.
#
#   before          15300 x VUID-vkCmdDrawIndexed-None-04007
#                   15300 x VUID-vkCmdDrawIndexed-None-02721
#                    1530 x VUID-vkCmdDrawIndexed-viewType-07752
#                   plus three faces reported wrong by the fixture
#
#   after                0, and the run ends on its own OK
#
# R_SkyCubePipeline remembered its pipeline INDEX in a file static, and file
# statics survive RE_Shutdown and R_Init while the pipeline table does not. The
# remembered number still looked valid after a restart and pointed at whatever
# pipeline had landed in that slot the second time round - a skeletal one, hence
# a bone vertex binding nothing bound, and a two-dimensional sampler handed the
# sky's cube view. Now the index is checked against the definition it is
# supposed to describe, which costs one memcmp and cannot go stale.
#
# IN the stage list as of this change. It was out of it from the day it was
# written, through two separate defects, and both of them were real.
stage_smokevidrestart() {
    JKX_SMOKE_VIDRESTART=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_VIDRESTART_DISPLAY:-:83}" \
    JKX_SMOKE_TIMEOUT=900 \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# Cubemaps, which had never been generated on this bench.
#
# r_cubeMapping defaults to zero, so every run so far has skipped
# R_LoadCubemapEntities, R_AssignCubemapsToWorldSurfaces, the convolve command,
# vk_generate_cubemaps and the whole prefilter - two render passes, two
# framebuffers, two pipelines and their offscreen images, created and destroyed
# on a path nothing here took. The fixture needs no new data for it: the entity
# list the loader falls back to includes info_player_start, which the generated
# map already has.
#
# It found something on the first run, and closing it took two attempts and a
# reverted one in between - which is worth keeping, because the failed attempt
# is what made the right answer visible.
#
# Twelve validation errors, six per filter and one per cube face:
#
#   UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout
#   ... expects VkImage to be in layout SHADER_READ_ONLY_OPTIMAL - instead,
#   current layout is COLOR_ATTACHMENT_OPTIMAL
#
# None of them carries a VUID number, and that is how the bench's own reporting
# defect was found: the block that prints validation findings tested a wider
# pattern than it printed and died under set -e when the two disagreed.
#
# THE FIRST ATTEMPT, reverted. The barrier at the top of vk_generate_cubemaps
# names SHADER_READ_ONLY_OPTIMAL as both the old and the new layout, which
# transitions nothing and asserts something. That reads like the wrong end, so
# it was corrected to say COLOR_ATTACHMENT_OPTIMAL as the old layout - and the
# count got worse, thirty-six VUID-...-commandBuffer-recording. The reason is a
# second finding worth having: vk_record_image_layout_transition has a case for
# COLOR_ATTACHMENT_OPTIMAL as a NEW layout and none for it as an old one, so the
# helper can put an image into being a colour attachment and cannot take one
# out, and the default arm is Com_Error( ERR_DROP ) - which drops the frame with
# the command buffer half recorded.
#
# Correcting both ends together cleared the original twelve and produced
# twenty-four of a new shape: half expecting SHADER_READ_ONLY and finding
# COLOR_ATTACHMENT, half the exact reverse. That symmetry is the message. It is
# not one end being wrong, it is the two ends disagreeing - and it says the
# barrier at the top was RIGHT all along.
#
# THE FIX, one deleted line. vk_generate_cubemaps ended by transitioning the sky
# cubemap to COLOR_ATTACHMENT_OPTIMAL. Nothing wants it there outside a render
# pass: the capture pass that fills it declares initialLayout = finalLayout =
# SHADER_READ_ONLY_OPTIMAL and moves it to the attachment layout internally
# through its own attachment reference. So the pass handed the image back
# read-only and this function then put it somewhere no one expected. Removing
# that transition takes the run to zero validation messages.
#
# Mutation tested: put the line back and the twelve come back.
#
# In the stage list as of this change. Twenty-four stages. It matters past
# cubemaps - the convolve is the machinery image-based lighting runs on, and the
# reflection term glass needs comes out of it (claude/Glass.md).
# Blending, which had never been measured here.
#
# The fixture drew opaque squares and one fogged pass, so blendFunc, its
# pipeline state and the ORDER surfaces are drawn in were exercised only by
# whatever the interface happens to do - which is 2D, unsorted, and says nothing
# about a translucent surface in a world.
#
# Four squares in one frame: an opaque backdrop and three translucent ones in
# front of it, one per blendFunc. All four are grey, so the histogram of grey
# levels IS the four composites, and the gate is an equality on each level.
# Predicted before the first run and measured after it, all four exact:
#
#   backdrop  rgbGen const 0.4                102, 73862 px
#   add       dst + src                       165,  4550 px
#   blend     src*a + dst*(1-a), a = 0.25     140,  4278 px
#   filter    dst * src                        51,  4620 px
#
# It is a sorting check as well, without being written as one: all three of
# those numbers are wrong if the backdrop is drawn after the squares, because
# they would be blending against the sky and the floor instead. A translucent
# surface that lands in front of something it should be behind shows up here as
# a different number rather than as a shrug.
#
# Mutation tested: turn the additive material opaque and the lane reports level
# 165 at zero pixels and names the blendFunc.
stage_smoketransparency() {
    JKX_SMOKE_TRANSPARENCY=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_TRANSPARENCY_DISPLAY:-:80}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# Texture coordinates over time, which no lane had ever exercised.
#
# Not one tcMod had executed here, and the reason is the shape this bench keeps
# finding: every texture the fixture drew was FLAT, and moving texture
# coordinates across a flat colour changes nothing whatever the keyword does.
#
# Five squares, five colours, one tcMod each, photographed three times. The
# reference square has no tcMod and is the noise floor - built into the lane
# rather than left to somebody remembering to run A against A - and it measures
# exactly zero movement across all three frames in every run so far.
#
# Three frames and slow rates, both of which were paid for. A tcMod is periodic;
# the same scroll measured a hundred pixels of movement in one run and one in
# the next, at the same rate, on the same binary, because the engine's clock
# does not advance in step with the frame counter. Picking an interval that does
# not divide the period does not work. Making the PERIOD long compared with the
# whole sampling window does.
#
# The static one is checked the other way round: tcMod scale changes the picture
# once and then holds still, so it is compared against the reference square in
# the SAME frame - 902 pixels against 1511, two texels across instead of one.
# Geometry that moves, and the path on which it did not.
#
# deformVertexes had never run on this bench. Nothing in the fixture asked for
# one, so RB_DeformTessGeometry and every branch inside it went unexecuted - in a
# renderer where a deform is what makes a flag flap, a plant sway and a force
# effect bulge.
#
# The lane found that move and bulge did nothing, and the experiment that
# located it was one flag on the model generator: the same four materials on a
# TWO-frame MD3 instead of a one-frame one. Two frames go down the batch path
# and one goes down the vertex buffer, and the difference was total:
#
#              vertex buffer       batch
#   wave       spread 23           spread 481
#   bulge      spread 0            spread 1353
#   move       centre unmoved      centre moved 95 thousandths
#   control    0                   0
#
# ShaderRequiresCPUDeforms answered "the processor will do it" for everything
# that was not Ghoul2 - and for a surface drawn from a vertex buffer that means
# nothing does it, because RB_DeformTessGeometry writes tess.xyz and the buffer
# is what gets drawn. Every deformVertexes on every single-frame MD3 in the game
# was silent. The GLSL had been ready the whole time: gen_vert.tmpl calls
# DeformPosition under USE_VBO_MDV exactly as it does for Ghoul2.
#
# After the fix, on the vertex-buffer path: wave 468, move 97 thousandths,
# control still exactly zero.
#
# BULGE IS STILL OPEN and is reported rather than gated, which is why this lane
# can be in the stage list at all: the three that work are guarded and the one
# that does not is printed every run.
#
# It is LOCATED, and by four probes in the shader rather than by reasoning - the
# reasoning was wrong twice before the probes:
#
#   return pos                                     drawn, 2016 px
#   return pos + normal * 4.0                      drawn, 2464 px
#   return pos + normal * scale(.., 0.0, ..) * 4   drawn AND animating, 392
#   return pos + normal * st.x * 4.0               NOT DRAWN
#
# So it is in_tex_coord0 in the VERTEX shader on this path, and it is not a
# value but a poison: the arithmetic comes out non-finite and the triangle is
# discarded. The bulge case is the only one of the five deforms that touches st,
# which is why it alone fails.
#
# Setting the height or the width to zero in the fixture did not narrow it down
# and cost two runs. The reason is worth keeping: NAN TIMES ZERO IS NAN. A
# literal 0.0 in the shader removes the term; a zero uniform multiplied by a
# poisoned attribute does not. A deliberate zero is only a control if it removes
# the operand rather than scaling it.
#
# Next: the deform branch is the only place gen_vert reads in_tex_coord0 under
# USE_VBO_MDV, so the vertex input state of THAT permutation is where to look.
# The validation layer says nothing, so the attribute is bound - it is the data
# that is wrong.
# The physical map, written six ways.
#
# A physically-based material carries roughness, metalness and occlusion, and
# the industry never agreed which channel holds which, so this renderer takes
# six spellings and turns them all into one order with a Vulkan image view
# swizzle - textureMapTypes[] in vk_local.h. ONE of the six was ever named by a
# fixture asset, rmoMap, so five of those swizzles had never been applied to a
# texel, let alone reached a pixel.
#
# Six squares, six packings, a DIFFERENT roughness in each, and each must come
# back as the exact byte the shader arithmetic predicts - mix(0.01, 1.0, v/255)
# times 255:
#
#   rmo    file R  20  ->  22        mosr   file A 140  -> 141
#   rmos   file R  60  ->  62        orm    file G 180  -> 181
#   moxr   file A 100  -> 102        orms   file G 220  -> 220
#
# All six measured exactly, first time out. So the five unused swizzles are
# right, which is a negative result worth having: this could as easily have been
# five defects.
#
# Six different values rather than six identical ones and a control square: six
# exact numbers cannot come out right by accident, and a renderer that ignored
# the texture would collapse all six onto one level. The control is built into
# the spread rather than standing beside it.
#
# It reads the ROUGHNESS DEBUG VIEW, and the second of the two reasons is the
# one that matters. The first is that the debug view is the value itself, so the
# check is an equality against arithmetic rather than against a shade of grey
# somebody chose. The second is that the shaded picture cannot be used at all:
#
#   A PHYSICALLY-BASED WORLD SURFACE COMES OUT BLACK ON THIS RENDERER.
#
# Measured on this fixture, same map, the only difference being two cvars:
#
#   r_normalMapping 1     11704 pixels of rgb(0, 0, 0)
#   r_normalMapping 0     11704 pixels of rgb(255, 108, 108)
#
# The same count either way, and that is what says they are DRAWN rather than
# skipped - not the vanishing draw the sky and the bulge turned out to be. They
# are shaded to nothing. To see it again: run this lane with r_debugView 0.
#
# Where to look, from the evidence. The validation layer is clean, so descriptor
# set five is bound and this is not the defect fixed last week. Black with the
# sets bound means a factor is zero, and there are two candidates worth putting
# side by side with the debug views this renderer already has: NL, which the
# guard from the menu fix will make zero if the light vector never arrives, and
# the diffuse term, which goes to zero if metalness reads as one. debugview
# takes a name - nl, diffuse, ambient, lightcolor - and each is one run.
#
# It matters well past this fixture: every material in this test has the shape a
# retail PBR texture pack uses to paint a map, and the one PBR lane that passes
# today draws a Ghoul2 character, which is a different path.
stage_smokephys() {
    JKX_SMOKE_PHYS=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_PHYS_DISPLAY:-:77}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

stage_smokedeform() {
    JKX_SMOKE_DEFORM=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_DEFORM_DISPLAY:-:78}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

stage_smoketcmod() {
    JKX_SMOKE_TCMOD=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_TCMOD_DISPLAY:-:79}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

stage_smokecubemap() {
    JKX_SMOKE_SET="r_cubeMapping=1" \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_CUBEMAP_DISPLAY:-:81}" \
    JKX_SMOKE_TIMEOUT=900 \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The one lane that still switches the validation layer off, and the only reason
# is arithmetic: this bench has two cores, the sanitizers already multiply the
# run, and the layer multiplies it again. Every other lane runs under it, so a
# Vulkan usage error here would be caught by its release twin anyway - what this
# lane is for is the memory, not the API.
stage_smokesan() {
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_DISPLAY:-:98}" \
    JKX_SMOKE_NO_VALIDATION=1 \
    JKX_SMOKE_SOUND=1 \
    ASAN_OPTIONS=detect_leaks=0 \
    UBSAN_OPTIONS=print_stacktrace=1 \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/san"
}

for stage in "${STAGES[@]}"; do
    if ! declare -F "stage_$stage" >/dev/null; then
        echo "no such stage: $stage" >&2
        exit 2
    fi
    run "$stage" "stage_$stage"
done

printf '\n'
if [ "${#failed[@]}" -ne 0 ]; then
    printf 'FAILED: %s\n' "${failed[*]}"
    exit 1
fi
printf 'all stages passed (MSVC itself is not among them - see the note at the top)\n'
