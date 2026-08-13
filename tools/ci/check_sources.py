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

A fourth instance had a different shape and this gate said "OK" straight past
it: a whole top-level directory, ui/, holding one 397-line menudef.h that was
the multiplayer menu definitions from Jedi Academy. No CMakeLists mentioned the
directory at all, so there was no list for it to be missing from. The two
#include "menudef.h" in code/ui both resolve beside their own source. The
per-directory check above cannot see this, because it only looks where it is
told to look - so there is now a second check that asks the opposite question:
is there a directory holding sources that no CMakeLists names anywhere.

It also refuses project files for any build system that is not CMake, which is
a different question asked for the same reason - see the note beside the list.

The rule is one-directional where it has to be: every .cpp and .c under a
watched directory must be listed, and every path listed must exist. Headers are
not required to be listed - the lists carry them for IDE grouping, not for
correctness - but a listed header that no longer exists is still an error.
"""

import re
import sys
from pathlib import Path, PurePosixPath


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

# Project files for a build system this tree does not use.
#
# One of these was found by reading, not by any check: games/jka/game/game.vcproj,
# 3906 lines of a Visual Studio 2008 project, referenced by nothing, still naming
# an output called jagamex86.dll. It survived every rename in this project
# because a rename sweeps sources and it is not one, and it survived the source
# list gate above because that gate only looks at .cpp and .c.
#
# The failure mode is the same as an unbuilt source and worse: someone opens it,
# it looks like how this is built, and it has not been since CMake arrived. This
# tree builds with CMake and only CMake, so anything on this list is either a
# second answer to that question or a leftover, and both are worth failing over.
FOREIGN_BUILD_FILES = {".vcproj", ".vcxproj", ".sln", ".dsp", ".dsw", ".pro"}
FOREIGN_BUILD_NAMES = {"Makefile.am", "configure.ac", "SConstruct", "meson.build"}
SKIP_DIRS = {"third_party", ".git"}

# shaders/ is built by tools/shadergen and checked by its own gate.
WATCHED = [
    Watched("code/rd-vulkan/CMakeLists.txt", "code/rd-vulkan", "${CodeDir}",
            skip={"shaders"}),
    Watched("games/jka/game/CMakeLists.txt", "games/jka/game", "${JKADir}"),
    Watched("games/jka/game/CMakeLists.txt", "games/jka/cgame", "${JKADir}"),
    Watched("games/jka/game/CMakeLists.txt", "games/jka/icarus", "${JKADir}"),
    Watched("games/jk2/game/CMakeLists.txt", "games/jk2/game", "${JK2Dir}"),
    Watched("games/jk2/game/CMakeLists.txt", "games/jk2/cgame", "${JK2Dir}"),
    Watched("games/jk2/game/CMakeLists.txt", "games/jk2/icarus", "${JK2Dir}"),
    Watched(["code/CMakeLists.txt", "code/rd-vulkan/CMakeLists.txt",
             "games/jka/game/CMakeLists.txt", "games/jk2/game/CMakeLists.txt"],
            "code/qcommon", "${CodeDir}"),
]


# Directories that hold sources for reasons other than being compiled into the
# engine or a game: the gates and the bench, the unit tests, and the shader
# sources that tools/shadergen owns.
NOT_BUILT_BY_CMAKE = ("tools/", "tests/", "assets/", "code/rd-vulkan/shaders/")

# The path variables the top-level CMakeLists defines, so a quoted "${CodeDir}/ui"
# can be turned back into a directory on disk.
DIR_VARS = {
    "CodeDir": "code",
    "JKADir": "games/jka",
    "JK2Dir": "games/jk2",
    "SharedDir": "shared",
    "ThirdPartyDir": "third_party",
}

QUOTED = re.compile(r'"([^"\n]+)"')


def cmake_named_dirs(root: Path) -> set[str]:
    """Every directory any CMakeLists names, as a repo-relative posix path."""
    named: set[str] = set()
    for cmake in root.rglob("CMakeLists.txt"):
        if SKIP_DIRS & set(cmake.parts) or "build" in cmake.parts:
            continue
        here = cmake.parent.relative_to(root).as_posix()
        text = cmake.read_text(encoding="utf-8", errors="replace")
        for quoted in QUOTED.findall(text):
            path = quoted
            for var, value in DIR_VARS.items():
                path = path.replace("${%s}" % var, value)
            if "${" in path:
                continue
            if not path.startswith(tuple(DIR_VARS.values())):
                # relative to the CMakeLists that names it
                path = f"{here}/{path}" if here != "." else path
            parts = PurePosixPath(path).parts
            for i in range(1, len(parts)):
                named.add("/".join(parts[:i]))
    return named


def unnamed_source_dirs(root: Path) -> list[str]:
    """Directories holding sources that no CMakeLists mentions at all."""
    named = cmake_named_dirs(root)
    orphans = set()
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES | {".h", ".hpp"}:
            continue
        parts = path.relative_to(root).parts
        if SKIP_DIRS & set(parts) or "build" in parts[0] or parts[0].startswith("."):
            continue
        directory = "/".join(parts[:-1])
        if not directory or directory.startswith(NOT_BUILT_BY_CMAKE):
            continue
        if directory not in named:
            orphans.add(directory)
    return sorted(orphans)


def foreign_build_files(root: Path) -> list[Path]:
    """Build files for anything that is not CMake, anywhere but the vendored trees."""
    found = []
    for path in root.rglob("*"):
        if not path.is_file() or SKIP_DIRS & set(path.parts):
            continue
        if path.suffix in FOREIGN_BUILD_FILES or path.name in FOREIGN_BUILD_NAMES:
            found.append(path)
    return sorted(found)


def main() -> int:
    failed = 0
    checked = 0

    foreign = foreign_build_files(Path("."))
    for path in foreign:
        print(f"error: {path} builds this tree with something that is not CMake")

    orphans = unnamed_source_dirs(Path("."))
    for directory in orphans:
        print(f"error: {directory}/ holds sources and no CMakeLists names it")

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

    if foreign:
        print()
        print("This tree builds with CMake. A project file for anything else is")
        print("either a second answer to how it is built or a leftover, and both")
        print("read as instructions to whoever opens one.")

    if failed:
        print()
        print("A source file that is in no list is not compiled, so nothing else")
        print("would have told you. Either add it to the list or delete it -")
        print("leaving it on disk is the option that costs the next person time.")

    if orphans:
        print()
        print("A directory no CMakeLists mentions is not half-built, it is not")
        print("built - and unlike a file missing from a list, there is no list it")
        print("is missing from, so the check above reports OK. Delete it, or name")
        print("it somewhere that compiles it.")

    if failed or foreign or orphans:
        return 1

    print(f"checked {checked} source(s) against {len(WATCHED)} source list(s)")
    print("OK: every source on disk is built, and every path listed exists")
    return 0


if __name__ == "__main__":
    sys.exit(main())
