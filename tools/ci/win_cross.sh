#!/usr/bin/env bash
# Cross-compile for Windows with MinGW-w64.
#
# The note at the top of local.sh used to say that MSVC is the one thing that
# can only fail remotely. That was true of MSVC and it was being read as true of
# Windows, which is a different and much larger thing: every #ifdef _WIN32
# branch in the tree - the window creation, the crash handler that walks the
# stack through dbghelp, the filesystem, the DLL loading, the console - had
# never been compiled by anything here. A whole platform's worth of code, and
# the first compiler to look at it was fifteen minutes away in CI.
#
# This is not MSVC and does not pretend to be. What it compiles is those
# branches, against real Windows headers, and it links them into a real
# jkx.exe. What is left for the remote job is what is genuinely
# MSVC-specific: its own dialect quirks and its own linker. That is a much
# smaller thing to be unable to check.
#
# It also catches what MSVC cannot. GNU ld walks a static library list once,
# left to right, and the Microsoft linker resolves across all of them at once -
# so a missing dependency between two bundled libraries is invisible on the
# platform that uses them and obvious here. That is exactly how libpng calling
# deflate without saying it depends on zlib was found.
#
# What you need:
#   apt install g++-mingw-w64-x86-64
#   the SDL2 MinGW development package, extracted somewhere:
#     https://github.com/libsdl-org/SDL/releases -> SDL2-devel-<ver>-mingw.tar.gz
#   and JKX_MINGW_SDL2 pointing at its x86_64-w64-mingw32 directory.
#
# Usage:
#   tools/ci/win_cross.sh [build dir]

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-${JKX_LOCAL_BUILD_ROOT:-/tmp/jkx-local}/win}"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null; then
    echo "  SKIPPED: no MinGW-w64. The Windows code paths are unchecked here and"
    echo "           CI is the first thing that will compile them."
    echo "           apt install g++-mingw-w64-x86-64"
    exit 0
fi

SDL2="${JKX_MINGW_SDL2:-}"
if [ -z "$SDL2" ]; then
    for guess in /opt/SDL2-mingw/x86_64-w64-mingw32 /usr/local/SDL2-mingw/x86_64-w64-mingw32 \
                 /tmp/SDL2-*/x86_64-w64-mingw32; do
        [ -d "$guess" ] && SDL2="$guess" && break
    done
fi

if [ ! -d "${SDL2:-/nonexistent}" ]; then
    echo "  SKIPPED: no SDL2 MinGW development package. Set JKX_MINGW_SDL2 to the"
    echo "           x86_64-w64-mingw32 directory inside SDL2-devel-<ver>-mingw.tar.gz"
    exit 0
fi

TOOLCHAIN="$BUILD/mingw-toolchain.cmake"
mkdir -p "$BUILD"
cat > "$TOOLCHAIN" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 $SDL2)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

# The shader pak is a file, not a binary: whichever build makes it, it is the
# same one, and glslc is not a cross-compiler. The Linux stages build it.
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DUseInternalLibs=ON \
    -DJKX_BUILD_SHADERS=OFF \
    -DSDL2_DIR="$SDL2/lib/cmake/SDL2" >/dev/null || exit 1

cmake --build "$BUILD" --parallel "${JOBS:-$(nproc)}" || exit 1

# A build that produces no binaries is a build that decided there was nothing to
# do, and it should not report success.
missing=0
for want in jkx.x86_64.exe jkagamex86_64.dll jk2gamex86_64.dll; do
    if [ ! -f "$BUILD/$want" ]; then
        echo "  built nothing called $want"
        missing=1
    fi
done
[ "$missing" -ne 0 ] && exit 1

echo "  Windows binaries: $(cd "$BUILD" && ls *.exe *.dll | tr '\n' ' ')"
exit 0
