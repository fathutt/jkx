#!/usr/bin/env python3
"""Keep the CMake source lists honest about what is on disk.

An unbuilt source file is worse than a deleted one. It reads as live code, it
turns up in searches, it gets edited - and none of that reaches a binary, so
nothing ever says so. This check exists because the renderer's list had drifted
that way: five sources on disk it never mentioned, including three added by this
project, and nineteen vendored Vulkan headers deleted when the renderer moved to
volk.

The renderer was checked from the start. The gamecode was not, and it had
drifted the same way: code/game/g_vehicleLoad.cpp - a 435-line predecessor of
the 1715-line bg_vehicleLoad.cpp that is actually built - plus three files in
codeJK2/icarus, all four in no source list and included by nothing. Deleting a
file that nothing compiles cannot change a binary, which is exactly why nobody
noticed them for years.

The rule is one-directional where it has to be: every .cpp and .c under a
watched directory must be listed, and every path listed must exist. Headers are
not required to be listed - the lists carry them for IDE grouping, not for
correctness - but a listed header that no longer exists is still an error.
"""

import re
import sys
from pathlib import Path


class Watched:
    """One CMakeLists, one directory, and the variable its paths start with."""

    def __init__(self, cmake: str, root: str, prefix: str, skip=()):
        self.cmake = Path(cmake)
        self.root = Path(root)
        self.prefix = prefix
        self.skip = set(skip)

    def listed(self, text: str) -> set[str]:
        pattern = r'\$\{%s\}/%s/([^"]+)"' % (
            re.escape(self.prefix[2:-1]), re.escape(self.root.name))
        return set(re.findall(pattern, text))

    def on_disk(self) -> set[str]:
        found = set()
        for path in self.root.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            rel = path.relative_to(self.root)
            if self.skip & set(rel.parts):
                continue
            found.add(rel.as_posix())
        return found


SOURCE_SUFFIXES = {".cpp", ".c"}

# shaders/ is built by tools/shadergen and checked by its own gate.
WATCHED = [
    Watched("code/rd-vulkan/CMakeLists.txt", "code/rd-vulkan", "${SPDir}",
            skip={"shaders"}),
    Watched("code/game/CMakeLists.txt", "code/game", "${SPDir}"),
    Watched("code/game/CMakeLists.txt", "code/cgame", "${SPDir}"),
    Watched("code/game/CMakeLists.txt", "code/icarus", "${SPDir}"),
    Watched("codeJK2/game/CMakeLists.txt", "codeJK2/game", "${JK2SPDir}"),
    Watched("codeJK2/game/CMakeLists.txt", "codeJK2/cgame", "${JK2SPDir}"),
    Watched("codeJK2/game/CMakeLists.txt", "codeJK2/icarus", "${JK2SPDir}"),
]


def main() -> int:
    failed = 0
    checked = 0

    for watch in WATCHED:
        if not watch.cmake.is_file():
            print(f"not found: {watch.cmake}", file=sys.stderr)
            print("run this from the top of the repository", file=sys.stderr)
            return 2

        text = watch.cmake.read_text(encoding="utf-8")
        listed = watch.listed(text)
        sources = watch.on_disk()

        missing = sorted(sources - listed)
        stale = sorted(p for p in listed if not (watch.root / p).exists())

        for path in missing:
            print(f"error: {watch.root}/{path} is on disk but in no source list")
            failed += 1
        for path in stale:
            print(f"error: {watch.cmake} lists {watch.root}/{path}, "
                  f"which does not exist")
            failed += 1

        checked += len(sources)

    if failed:
        print()
        print("A source file that is in no list is not compiled, so nothing else")
        print("would have told you. Either add it to the list or delete it -")
        print("leaving it on disk is the option that costs the next person time.")
        return 1

    print(f"checked {checked} source(s) against {len(WATCHED)} source list(s)")
    print("OK: every source on disk is built, and every path listed exists")
    return 0


if __name__ == "__main__":
    sys.exit(main())
