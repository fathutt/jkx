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
#   policy      the ascii, layering and source-list gates, and actionlint
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
#   smokesan    the same run against the sanitizer build. Building sanitizers
#               and never running them checks nothing: the first time this was
#               run it reported two misaligned accesses in the zone allocator,
#               on the first allocation the engine makes
#
# What it cannot cover: MSVC itself - its dialect and its linker. That used to
# be read as "Windows", which is a much larger thing; the windows stage compiles
# the platform's code here, so what is left to fail remotely is only what is
# specific to Microsoft's compiler.
#
# Usage:
#   tools/ci/local.sh [stage ...]        default: all of them, in this order

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_ROOT="${JKX_LOCAL_BUILD_ROOT:-/tmp/jkx-local}"
JOBS="${JOBS:-$(nproc)}"

STAGES=( "$@" )
if [ "${#STAGES[@]}" -eq 0 ]; then
    STAGES=( policy release debug windows sanitizers tests smoke smokesan )
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
    python3 "$ROOT/tools/ci/check_sources.py" "$ROOT" &&
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
    python3 "$ROOT/tools/fontgen/build_fonts.py" --check
}

stage_smoke() {
    bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
}

# Leak detection is off: the engine frees its zone in one go at exit and reports
# what it freed, which is a different accounting from the one LeakSanitizer does,
# and the noise would bury the errors worth reading. The validation layer is off
# too - one slow thing at a time, and the release run above already ran it.
stage_smokesan() {
    JKX_SMOKE_DISPLAY="${JKX_SMOKE_DISPLAY:-:98}" \
    JKX_SMOKE_NO_VALIDATION=1 \
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
