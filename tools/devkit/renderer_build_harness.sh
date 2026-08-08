#!/usr/bin/env bash
# Compile-check the Vulkan renderer before it is wired into the SP engine.
#
# code/rd-vulkan/ is the single source of truth, but it cannot build inside this
# tree yet: it still expects the multiplayer headers it was written against, and
# reconciling those with the single-player ones is phase 2. Until then this
# script copies the sources into a checkout of the fork they came from, which
# does build, and compiles there.
#
# The point is to keep phase 1 verifiable. Doing volk, VMA and the rest blind,
# then discovering the damage months later during the port, is the failure mode
# this avoids.
#
# Usage:
#   tools/devkit/renderer_build_harness.sh [path-to-eternaljk] [-- extra make args]
#
# The harness checkout is disposable; nothing is ever copied back from it.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HARNESS="${1:-${JKX_RENDER_HARNESS:-$HOME/eternaljk}}"

if [ ! -d "$HARNESS/codemp/rd-vulkan" ]; then
    echo "harness checkout not found at: $HARNESS" >&2
    echo "clone JKSunny/EternalJK, check out origin/pbr, and pass the path" >&2
    exit 2
fi

SRC="$REPO_ROOT/code/rd-vulkan"
DST="$HARNESS/codemp/rd-vulkan"

echo "syncing $SRC -> $DST"

# Sources only. The harness keeps its own generated SPIR-V, its own vendored
# Vulkan headers and its own CMakeLists, none of which we carry any more.
count=0
while IFS= read -r -d '' file; do
    rel="${file#"$SRC"/}"
    case "$rel" in
        shaders/*|CMakeLists.txt) continue ;;
    esac
    mkdir -p "$DST/$(dirname "$rel")"
    cp "$file" "$DST/$rel"
    count=$((count + 1))
done < <(find "$SRC" \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.c' \) -print0)

echo "synced $count file(s)"

# Third-party headers the modernised renderer now expects.
if [ -d "$REPO_ROOT/third_party" ]; then
    mkdir -p "$HARNESS/third_party"
    cp -r "$REPO_ROOT/third_party/." "$HARNESS/third_party/"
    echo "synced third_party/"
fi

# The harness CMakeLists is the fork's, so teach it about the sources and
# include paths the modernised renderer needs. Idempotent.
HARNESS_CMAKE="$DST/CMakeLists.txt"
if ! grep -q "JKX harness additions" "$HARNESS_CMAKE"; then
    cat >> "$HARNESS_CMAKE" <<'EOF'

# --- JKX harness additions -------------------------------------------------
target_include_directories(${MPVulkanRenderer} PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/volk"
    "${CMAKE_SOURCE_DIR}/third_party/vma")
target_sources(${MPVulkanRenderer} PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/volk/volk.c"
    # Files added since the fork; the harness CMakeLists lists sources by hand.
    "${MPDir}/rd-vulkan/vk_allocator.cpp"
    "${MPDir}/rd-vulkan/vk_pipeline_cache.cpp"
    "${MPDir}/rd-vulkan/vk_shader_pak.cpp")
find_package(Vulkan REQUIRED)
target_link_libraries(${MPVulkanRenderer} Vulkan::Headers)
target_compile_definitions(${MPVulkanRenderer} PRIVATE VK_NO_PROTOTYPES)
# JKX is C++20; the fork still appends -std=c++11 to CMAKE_CXX_FLAGS, and a
# target-level standard would lose to a flag that appears later on the command
# line, so override the flag itself.
string(REPLACE "-std=c++11" "-std=c++20" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
target_compile_features(${MPVulkanRenderer} PRIVATE cxx_std_20)
# ---------------------------------------------------------------------------
EOF
    echo "patched harness CMakeLists"
fi

BUILD="$HARNESS/build-harness"
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
    cmake -S "$HARNESS" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
        -DBuildMPRdVulkan=ON -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF \
        -DBuildMPEngine=OFF -DBuildMPGame=OFF -DBuildMPCGame=OFF -DBuildMPUI=OFF \
        -DBuildDiscordRichPresence=OFF \
        -DUseInternalSDL2=OFF -DUseInternalZlib=OFF -DUseInternalPNG=OFF -DUseInternalJPEG=OFF \
        >/dev/null
fi

shift || true
[ "${1:-}" = "--" ] && shift || true

echo "building rd-vulkan"
cmake --build "$BUILD" --target "rd-vulkan_x86_64" --parallel "${JOBS:-2}" "$@"
