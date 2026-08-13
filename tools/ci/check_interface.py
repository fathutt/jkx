#!/usr/bin/env python3
"""Measure the engine-to-game interface, and keep it from growing back.

The engine is the lower layer. Every include that points from it up into the
gamecode is a place the two cannot be separated, and section 9 of the backlog -
one game's code per directory - is blocked until that set is small enough to
name. check_layering.py already refuses new violations; this asks the other
question, which is how much gamecode the engine can see through the ones it
still has.

That is not the number of include sites. A header nobody includes directly but
which arrives through two others is just as much a part of the interface, so
this walks includes transitively from every engine-to-game edge and adds up the
lines of everything reachable.

Measured that way the interface was 13,139 lines through 13 include sites when
this was first counted. Most of it came through two files that included the
whole gamecode to reach one header apiece.

The ceiling below is a ratchet, like the layering baseline: lower it when the
number drops, never raise it. Raising it is a decision about the shape of the
tree and belongs in a commit message, not in a passing build.

Usage:
    tools/ci/check_interface.py [--list] [root]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Lower this when it drops. See the note above before touching it.
#
# It has been raised once, from 933 to 983, and the fifty lines are
# code/api/anim_names.h. The engine's menu code resolves animation names out of
# .menu files and out of animation.cfg, with no game module loaded, so the table
# of names is a contract between the assets and everyone rather than a detail of
# a game. The alternative measured at the time was to move the whole vocabulary
# - the enums, the tables and the per-game animation_t - which would have put
# the ceiling near 8,800 and left it guarding nothing.
#
# The count also went from three include sites to six in the same commit,
# because code/ui joined ENGINE below. It was always compiled into both engines;
# the gate had simply been told it was gamecode.
CEILING = 983

ENGINE = ("code/qcommon", "code/server", "code/client", "shared", "code/ui")
# code/api is counted on the game side on purpose. It is the contract - the
# three headers both sides include - and moving it out of the gamecode is what
# let the layering gate stop calling those three includes violations. If it were
# also dropped from this count the number would fall to zero and the gate would
# be measuring nothing, which is bookkeeping rather than progress. The question
# here has not changed: how much of the game side can the engine see.
GAME = ("code/api", "games/jka", "games/jk2")

# shared/qcommon/safe/files.cpp includes game/g_shared.h under JKX_GAME_MODULE - that
# is, when it is compiled into the game library rather than into the engine. It is
# one file built twice, not the engine reaching upwards, and counting it would
# put the whole gamecode in the total and hide every real change underneath it.
EXEMPT = ("shared/qcommon/safe/files.cpp",)

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
SOURCE_SUFFIXES = {".cpp", ".c", ".h"}


def resolve(root: Path, include: str, origin: Path) -> Path | None:
    """Where an #include lands, by the include paths the build actually sets."""
    for base in (origin.parent, root, root / "code", root / "shared",
                 root / "games/jka", root / "games/jk2"):
        candidate = (base / include)
        if candidate.is_file():
            return candidate.resolve().relative_to(root.resolve())
    return None


def is_game(path: Path) -> bool:
    return str(path).startswith(GAME)


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    show = "--list" in sys.argv
    root = Path(args[0] if args else ".")

    if not (root / "code" / "qcommon" / "q_shared.h").is_file():
        print("run this from the top of the repository", file=sys.stderr)
        return 2

    edges: list[tuple[str, str]] = []
    for directory in ENGINE:
        for path in (root / directory).rglob("*"):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            rel = path.resolve().relative_to(root.resolve())
            if str(rel) in EXEMPT:
                continue
            for include in INCLUDE.findall(path.read_text(errors="replace")):
                target = resolve(root, include, path)
                if target and is_game(target):
                    edges.append((str(rel), str(target)))

    reachable: dict[str, int] = {}
    queue = [Path(target) for _, target in edges]
    while queue:
        path = queue.pop()
        if str(path) in reachable:
            continue
        text = (root / path).read_text(errors="replace")
        reachable[str(path)] = len(text.splitlines())
        for include in INCLUDE.findall(text):
            target = resolve(root, include, root / path)
            if target and is_game(target) and str(target) not in reachable:
                queue.append(target)

    total = sum(reachable.values())

    if show or total > CEILING:
        for source, target in sorted(set(edges)):
            print(f"  {source} -> {target}")
        print()
        for path in sorted(reachable, key=lambda p: -reachable[p]):
            print(f"  {reachable[path]:6d}  {path}")
        print()

    print(f"{len(set(edges))} include site(s) from the engine into the game, "
          f"{len(reachable)} header(s) reachable, {total} lines")

    if total > CEILING:
        print()
        print(f"error: the interface is {total} lines and the ceiling is {CEILING}.")
        print("Something the engine includes now reaches further into the gamecode")
        print("than it did. Either take the edge out or say why the ceiling moves.")
        return 1

    if total < CEILING:
        print(f"the ceiling is {CEILING}; lower it to {total} to lock this in")

    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
