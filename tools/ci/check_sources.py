#!/usr/bin/env python3
"""Keep code/rd-vulkan/CMakeLists.txt honest about what is on disk.

That CMakeLists now has a real target behind it (BuildSPRdVulkan), but the
target is off by default because it does not compile yet, so an ordinary build
still never reads the list. It remains documentation for now - and undetected
documentation drifts. By the
time this check was written it had: five source files on disk that it never
mentioned, including three added by this project, and nineteen vendored Vulkan
headers that were deleted when the renderer moved to volk.

None of that broke a build, which is the point. It would have broken the first
build of phase 2, months from now, in the middle of much harder work.

The rule is one-directional where it has to be: every .cpp and .c under
code/rd-vulkan must be listed, and every path listed must exist. Headers are
not required to be listed - the list carries them for IDE grouping, not for
correctness - but a listed header that no longer exists is still an error.
"""

import re
import sys
from pathlib import Path

CMAKE = Path("code/rd-vulkan/CMakeLists.txt")
ROOT = Path("code/rd-vulkan")

# Third-party and generated trees. shaders/ is built by tools/shadergen and
# checked by its own gate; utils/ is vendored stb and mikktspace, listed
# separately and deliberately not tracked file by file.
SKIP_DIRS = {"shaders"}

SOURCE_SUFFIXES = {".cpp", ".c"}


def listed_paths(text: str) -> set[str]:
    """Every "${SPDir}/rd-vulkan/<path>" the file mentions.

    MPDir was the prefix while the list was still the multiplayer fork's,
    copied verbatim and built by nothing here. It is SPDir now that the target
    exists in this tree.
    """
    return set(re.findall(r'\$\{SPDir\}/rd-vulkan/([^"]+)"', text))


def disk_sources() -> set[str]:
    found = set()
    for path in ROOT.rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        rel = path.relative_to(ROOT)
        if SKIP_DIRS & set(rel.parts):
            continue
        found.add(rel.as_posix())
    return found


def main() -> int:
    if not CMAKE.is_file():
        print(f"not found: {CMAKE}", file=sys.stderr)
        print("run this from the top of the repository", file=sys.stderr)
        return 2

    text = CMAKE.read_text(encoding="utf-8")
    listed = listed_paths(text)
    sources = disk_sources()

    missing = sorted(sources - listed)
    stale = sorted(p for p in listed if not (ROOT / p).exists())

    for path in missing:
        print(f"error: {path} is in code/rd-vulkan but not in its CMakeLists.txt")
    for path in stale:
        print(f"error: CMakeLists.txt lists {path}, which does not exist")

    if missing or stale:
        print()
        print("The renderer's CMakeLists is not compiled yet, so nothing else would")
        print("have told you. Add the new sources to SPRDVulkanFiles, and drop")
        print("the entries for files that are gone.")
        return 1

    print(f"checked {len(sources)} source(s) against {CMAKE}")
    print("OK: the renderer source list matches the directory")
    return 0


if __name__ == "__main__":
    sys.exit(main())
