#!/usr/bin/env bash
# Compile the renderer a second time in its single-player shape.
#
# Phase 2 moves the renderer from multiplayer types to single-player ones one
# piece at a time, and the only build that exists meanwhile is the multiplayer
# one inside the harness. Everything guarded by #ifdef RF_ALPHA_FADE or
# #ifndef JKX_SP_TYPES is therefore dead code in every build we can make - it
# compiles for the first time at the end of phase 2, months from now, all at
# once, which is the worst possible moment to find out it does not.
#
# So: take the compile commands the harness build just used, add the
# single-player-only macros, and run them again. The objects are thrown away;
# only the exit status matters. It costs one extra compile of the renderer and
# it has already caught a misplaced #endif that swallowed a #define the
# cylinder surface needed - a failure that would otherwise have surfaced in
# phase 2 with no obvious cause.
#
# This is not a substitute for building against the real single-player headers.
# It cannot see anything about struct layout, only about the shape of the code
# the preprocessor selects. That is still most of what goes wrong.
#
# Usage:
#   tools/devkit/check_sp_shape.sh <path-to-harness-build-dir>

set -euo pipefail

BUILD="${1:-}"
if [ -z "$BUILD" ] || [ ! -f "$BUILD/build.ninja" ]; then
    echo "usage: $0 <harness build dir> (a configured Ninja build)" >&2
    exit 2
fi

# Single-player-only flags. These are the macros the guarded code tests; the
# values are the ones in code/rd-common/tr_types.h and are what make
# JKX_SP_TYPES come out true in tr_sp_compat.h.
SP_DEFINES=(
    -DRF_ALPHA_FADE=0x00800
    -DRF_CAP_FRAMES=0x00400
    -DRF_G2MINLOD=0x100000
)

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

failed=0
checked=0

while IFS= read -r cmd; do
    case "$cmd" in
        *" -c "*rd-vulkan*) ;;
        *) continue ;;
    esac

    # Replace the object path so the real build is not clobbered, and drop the
    # dependency file: regenerating it here would make the next incremental
    # build think these objects are up to date when they are not.
    # -MD, -MT and -MF travel as a set; leaving any one of them behind makes
    # the compiler ask for the others.
    patched="$(printf '%s\n' "$cmd" \
        | sed -E "s#-MD ##; s#-MT [^ ]+ ##; s#-MF [^ ]+ ##; s#-o [^ ]+#-o $TMP/out.o#")"

    if ! eval "$patched ${SP_DEFINES[*]}" >"$TMP/err" 2>&1; then
        echo "--- single-player shape does not compile:"
        printf '%s\n' "$cmd" | sed -E 's#.* -c ##; s# .*##'
        grep -E "error" "$TMP/err" | head -8 || true
        failed=$((failed + 1))
    fi
    checked=$((checked + 1))
done < <(ninja -C "$BUILD" -t commands rd-vulkan_x86_64)

if [ "$checked" -eq 0 ]; then
    echo "no renderer compile commands found in $BUILD" >&2
    exit 2
fi

if [ "$failed" -ne 0 ]; then
    echo
    echo "$failed of $checked translation unit(s) fail in the single-player shape."
    exit 1
fi

echo "OK: $checked translation unit(s) compile in the single-player shape too"
