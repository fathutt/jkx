#!/usr/bin/env python3
"""Two ways a setting can be the same setting under two names, and both were here.

A cvar is looked up case-insensitively but stored under the spelling it was
first registered with, so com_buildScript in the engine and com_buildscript in
both games were one variable whose name in the written config depended on which
code ran first. Nothing failed; the config just sometimes said one and sometimes
the other.

Worse, and the reason this exists: the same C variable was registered under
g_saberMoreRealistic in Jedi Academy and g_saberRealisticCombat in Jedi Outcast.
One meaning, two names, so a config written for either game silently did nothing
in the other - and a player has no way to find that out except by noticing the
game does not change.

So this asks two questions:

  1. Is any cvar registered under two spellings that differ only in case?
  2. Is any C variable assigned from a registration under one name in one game
     and a different name in the other?

Neither can be answered by a compiler, because both spellings compile and both
names are just strings.

Usage:
    tools/ci/check_cvars.py [root]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Vendored trees and the renderer transplant are not ours to rename.
SKIP = ("third_party/", "lib/", "build")
SUFFIXES = {".cpp", ".c", ".h"}

# The shapes this tree registers a cvar with. The engine, the gamecode and the
# cgame each have their own spelling of the same call.
REGISTER = re.compile(
    r'\b(?:([A-Za-z_][A-Za-z0-9_]*)\s*=\s*)?'
    r'(?:gi\.cvar|cgi_Cvar_Get|trap_Cvar_Get|Cvar_Get)\s*\(\s*"([^"]+)"'
)


def sources(root: Path):
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        rel = str(path.resolve().relative_to(root.resolve()))
        if rel.startswith(SKIP):
            continue
        yield rel, path


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")

    if not (root / "code" / "qcommon" / "q_shared.h").is_file():
        print("run this from the top of the repository", file=sys.stderr)
        return 2

    # spelling -> where it was registered
    spellings: dict[str, list[str]] = {}
    # (game, variable) -> set of cvar names it was assigned from
    by_variable: dict[tuple[str, str], set[str]] = {}
    count = 0

    for rel, path in sources(root):
        text = path.read_text(errors="replace")
        if "cvar" not in text and "Cvar_Get" not in text:
            continue

        game = None
        if rel.startswith("games/jka"):
            game = "jka"
        elif rel.startswith("games/jk2"):
            game = "jk2"

        for match in REGISTER.finditer(text):
            variable, name = match.group(1), match.group(2)
            line = text.count("\n", 0, match.start()) + 1
            spellings.setdefault(name, []).append(f"{rel}:{line}")
            count += 1
            if game and variable:
                by_variable.setdefault((game, variable), set()).add(name)

    failed = False

    # 1. one cvar, two spellings
    folded: dict[str, set[str]] = {}
    for name in spellings:
        folded.setdefault(name.lower(), set()).add(name)

    clashes = sorted(k for k, v in folded.items() if len(v) > 1)
    if clashes:
        failed = True
        print("error: the same cvar registered under spellings that differ only in case:")
        for key in clashes:
            for name in sorted(folded[key]):
                print(f"  {name}")
                for where in spellings[name]:
                    print(f"      {where}")
        print()
        print("Lookup folds case but registration does not, so which spelling ends")
        print("up in the written config depends on which registration runs first.")
        print()

    # 2. one variable, a different name in each game
    jka = {var: names for (game, var), names in by_variable.items() if game == "jka"}
    jk2 = {var: names for (game, var), names in by_variable.items() if game == "jk2"}

    split = []
    for var in sorted(set(jka) & set(jk2)):
        if not (jka[var] & jk2[var]):
            split.append((var, sorted(jka[var]), sorted(jk2[var])))

    if split:
        failed = True
        print("error: the same variable registered under different names per game:")
        for var, a, b in split:
            print(f"  {var}: jka {a}, jk2 {b}")
        print()
        print("A config written for one game then does nothing in the other, and")
        print("says nothing about it. If the games really do need different")
        print("settings, they need different variables too.")
        print()

    if failed:
        return 1

    print(f"checked {count} cvar registration(s), "
          f"{len(spellings)} distinct name(s)")
    print("OK: no cvar registered twice under one meaning")
    return 0


if __name__ == "__main__":
    sys.exit(main())
