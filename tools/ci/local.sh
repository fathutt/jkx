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
    STAGES=( policy release debug windows sanitizers tests smoke smokewide smokejk2 smokesave smokesan prepass fog noassets )
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
    python3 "$ROOT/tools/verify/make_test_glm.py" --check &&
    python3 "$ROOT/tools/verify/make_test_gla.py" --check &&
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

stage_prepass() {
    local a b rc
    a="$(mktemp -d)"
    b="$(mktemp -d)"
    rc=0

    prepass_run() {
        JKX_SMOKE_SET="r_depthPrepass=$1" \
        JKX_SMOKE_PLAIN=1 \
        JKX_SMOKE_NO_VALIDATION=1 \
        JKX_SMOKE_DISPLAY="$2" \
        JKX_SMOKE_SHOT_DIR="$3" \
            bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release" >/dev/null
    }

    prepass_run 0 "${JKX_SMOKE_PREPASS_DISPLAY_A:-:94}" "$a" || rc=1
    prepass_run 1 "${JKX_SMOKE_PREPASS_DISPLAY_B:-:93}" "$b" || rc=1

    if [ "$rc" -eq 0 ]; then
        python3 "$ROOT/tools/verify/ab_frames.py" "$a" "$b" --max-pixels 16 || rc=1
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
    JKX_SMOKE_NO_VALIDATION=1 \
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
    JKX_SMOKE_NO_VALIDATION=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_NOASSETS_DISPLAY:-:90}" \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

stage_smokewide() {
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_WIDE_DISPLAY:-:97}" \
    JKX_SMOKE_SCREEN=2560x720 \
    JKX_SMOKE_NO_VALIDATION=1 \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The other game. jkx_jk2 is code/ built with -DJK2_MODE plus games/jk2/game, so
# this is a second configuration of the same engine rather than a second copy of
# the test - and half the project had nothing looking at it until this stage
# existed.
stage_smokejk2() {
    JKX_SMOKE_GAME=jk2 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_JK2_DISPLAY:-:96}" \
    JKX_SMOKE_NO_VALIDATION=1 \
        bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# The savegame round trip. Its own stage because the run is long enough without
# it: the level counter's second map and a save-load round trip in one pass push
# a software rasteriser past the timeout.
#
# Validation is off, and that is still an exception rather than a preference:
# four device objects survive to vkDestroyDevice on this path. Three separate
# causes have been found and fixed under it and this one is not yet among them -
# backlog section 21 names the two creations exactly. Turn this back on when it
# is closed.
stage_smokesave() {
    JKX_SMOKE_SAVELOAD=1 \
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_SAVE_DISPLAY:-:95}" \
    JKX_SMOKE_NO_VALIDATION=1 \
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
