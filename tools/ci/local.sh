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
#   sanitizers  Debug with asan and ubsan, which is what the CI job builds
#   tests       the unit tests
#   smoke       the engine starting on the Vulkan renderer, headless
#
# What it cannot cover: MSVC. There is no Windows compiler here, so the Windows
# jobs remain the one thing that can only fail remotely - which is a reason to
# keep the Windows-only surface small, not a reason to skip the rest.
#
# Usage:
#   tools/ci/local.sh [stage ...]        default: all of them, in this order

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_ROOT="${JKX_LOCAL_BUILD_ROOT:-/tmp/jkx-local}"
JOBS="${JOBS:-$(nproc)}"

STAGES=( "$@" )
if [ "${#STAGES[@]}" -eq 0 ]; then
    STAGES=( policy release debug sanitizers tests smoke )
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
stage_workflows() {
    local lint
    lint="$(command -v actionlint || true)"
    if [ -z "$lint" ]; then
        echo "  (actionlint not installed, workflows not checked)"
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

stage_sanitizers() {
    configure "$BUILD_ROOT/san" -DCMAKE_BUILD_TYPE=Debug \
        -DJKX_ENABLE_ASAN=ON -DJKX_ENABLE_UBSAN=ON &&
    cmake --build "$BUILD_ROOT/san" --parallel "$JOBS"
}

stage_tests() {
    python3 "$ROOT/tools/verify/selftest.py"
}

stage_smoke() {
    bash "$ROOT/tools/verify/smoke_headless.sh" "$BUILD_ROOT/release"
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
printf 'all stages passed (MSVC is not among them - see the note at the top)\n'
