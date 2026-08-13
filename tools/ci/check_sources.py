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
games/jk2/icarus, all four in no source list and included by nothing. Deleting a
file that nothing compiles cannot change a binary, which is exactly why nobody
noticed them for years.

code/qcommon was added last and found the third instance immediately:
hstring.cpp and hstring.h, 14 KB of a string-interning class that nothing
compiles and nothing includes. The one every caller actually uses is
code/Rufl/hstring.h, which is what all four source lists name.

The rule is one-directional where it has to be: every .cpp and .c under a
watched directory must be listed, and every path listed must exist. Headers are
not required to be listed - the lists carry them for IDE grouping, not for
correctness - but a listed header that no longer exists is still an error.
"""

import re
import sys
from pathlib import Path


class Watched:
    """One directory, and every CMakeLists that is allowed to build out of it.

    Usually that is one file. code/qcommon is the exception and the reason this
    takes a list: its sources are split four ways - the engine builds most of
    them, the renderer builds matcomp.cpp, and both game libraries build
    tri_coll_test.cpp - so asking any single list about that directory would
    report the other three lists' files as unbuilt.
    """

    def __init__(self, cmake, root: str, prefix: str, skip=()):
        self.cmakes = [Path(c) for c in ([cmake] if isinstance(cmake, str) else cmake)]
        self.cmake = self.cmakes[0]
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
    Watched("code/rd-vulkan/CMakeLists.txt", "code/rd-vulkan", "${CodeDir}",
            skip={"shaders"}),
    Watched("games/jka/game/CMakeLists.txt", "games/jka/game", "${JKADir}"),
    Watched("games/jka/game/CMakeLists.txt", "games/jka/cgame", "${JKADir}"),
    Watched("games/jka/game/CMakeLists.txt", "code/icarus", "${CodeDir}"),
    Watched("games/jk2/game/CMakeLists.txt", "games/jk2/game", "${JK2Dir}"),
    Watched("games/jk2/game/CMakeLists.txt", "games/jk2/cgame", "${JK2Dir}"),
    Watched("games/jk2/game/CMakeLists.txt", "games/jk2/icarus", "${JK2Dir}"),
    Watched(["code/CMakeLists.txt", "code/rd-vulkan/CMakeLists.txt",
             "games/jka/game/CMakeLists.txt", "games/jk2/game/CMakeLists.txt"],
            "code/qcommon", "${CodeDir}"),
]


def main() -> int:
    failed = 0
    checked = 0

    for watch in WATCHED:
        for cmake in watch.cmakes:
            if not cmake.is_file():
                print(f"not found: {cmake}", file=sys.stderr)
                print("run this from the top of the repository", file=sys.stderr)
                return 2

        listed = set()
        for cmake in watch.cmakes:
            listed |= watch.listed(cmake.read_text(encoding="utf-8"))
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
