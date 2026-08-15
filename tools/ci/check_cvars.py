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

And a third way, which cost the most: the same cvar, the same name, a different
DEFAULT in each game. cg_fovAspectAdjust was "1" in Jedi Academy and "0" in
Jedi Outcast, so on a wide screen Outcast took cg_fov as the horizontal angle
whatever shape the window was and had twenty-six degrees of vertical left. The
weapon in the hands, the player in third person and the frame every cutscene
was composed for were all enormous and too close, and it read like a dozen
separate defects. g_subtitles was the same shape on the same evening.

That class survives for years because each game is looked at on its own, and
the number that is wrong is in the other one's file - so nobody comparing them
is ever looking at both.

So this asks three questions:

  1. Is any cvar registered under two spellings that differ only in case?
  2. Is any C variable assigned from a registration under one name in one game
     and a different name in the other?
  3. Is any cvar registered with one default in one game and another in the
     other, without a written reason?

None can be answered by a compiler, because both spellings compile and both
names and defaults are just strings.

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
    r'(?:\s*,\s*"([^"]*)")?'
)

# And the other shape, which is the one that mattered.
#
# The cgame does not call Cvar_Get per setting; it fills a table of
# { &variable, "name", "default", flags } and registers the lot in a loop. Every
# cg_ cvar in both games is declared that way - including cg_fovAspectAdjust,
# the one whose per-game default cost an evening - so a checker that only reads
# call sites is a checker that cannot see the half of the tree the defect was
# in. Written down because the first version of this file had exactly that hole
# and passed.
REGISTER_TABLE = re.compile(
    r'\{\s*&\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,'
)

# Cvars the two games are ALLOWED to disagree about, each with the reason.
#
# A ratchet, and it may only shrink. Every entry here is a claim that the two
# games genuinely want different behaviour - not that nobody has looked yet -
# and the reason is what the next person reads instead of guessing.
DIFFERENT_ON_PURPOSE = {
    # The two games do not even share a vocabulary for this one. Outcast reads
    # it as "male" or "female" and uses it to pick Kyle's or Jan's voice
    # directory (cg_players.cpp); Academy's is a single letter, "m" or "f", and
    # feeds a player character the user builds in the menu. The same string in
    # the other game selects nothing.
    "sex":
        "different value vocabulary per game: m/f in Academy, male/female in "
        "Outcast, and they index different things",

    # Each game's own head-up display file, by name.
    "cg_hudFiles":
        "names the game's own hud definition, which is a different file",

    # Academy is played over the shoulder and Outcast down the barrel. That is
    # what the two games are, not a setting somebody forgot to line up.
    "cg_thirdPerson":
        "Academy starts in third person and Outcast in first, by design",

    # The same name, two vocabularies. Academy reads it as a level - 1 sounds,
    # 2 effects, 3 marks, 4 always (cg_players.cpp) - and Outcast reads it as a
    # boolean and nothing else (cg_event.cpp), so 1 there IS on.
    "cg_footsteps":
        "a level in Academy, a boolean in Outcast; 1 and 3 both mean on",
}


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
    # game -> name -> set of default strings it was registered with
    defaults: dict[str, dict[str, set[str]]] = {}
    # name -> where it was registered, for the message
    where_registered: dict[str, set[str]] = {}
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

        found = list(REGISTER.finditer(text)) + list(REGISTER_TABLE.finditer(text))

        for match in found:
            variable, name, default = match.group(1), match.group(2), match.group(3)
            line = text.count("\n", 0, match.start()) + 1
            spellings.setdefault(name, []).append(f"{rel}:{line}")
            count += 1
            if game and variable:
                by_variable.setdefault((game, variable), set()).add(name)
            if game and default is not None:
                defaults.setdefault(game, {}).setdefault(name, set()).add(default)
                where_registered.setdefault(name, set()).add(f"{rel}:{line}")

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

    # 3. one name, a different default in each game
    #
    # By NAME rather than by variable, because the two games spell the variable
    # differently often enough and the name is what a player types and what a
    # config carries.
    disagree = []
    for name in sorted(set(defaults.get("jka", {})) & set(defaults.get("jk2", {}))):
        a = defaults["jka"][name]
        b = defaults["jk2"][name]
        if a == b or name in DIFFERENT_ON_PURPOSE:
            continue
        disagree.append((name, sorted(a), sorted(b)))

    if disagree:
        failed = True
        print("error: the same cvar registered with a different default per game:")
        for name, a, b in disagree:
            print(f"  {name}: jka {a}, jk2 {b}")
            for where in sorted(where_registered.get(name, [])):
                print(f"      {where}")
        print()
        print("One of the two is the value everybody actually gets, and which one")
        print("that is depends on which game they started. If the difference is")
        print("deliberate, put the name in DIFFERENT_ON_PURPOSE with the reason;")
        print("that list may only shrink.")
        print()

    if failed:
        return 1

    print(f"checked {count} cvar registration(s), "
          f"{len(spellings)} distinct name(s)")
    print("OK: no cvar registered twice under one meaning, "
          "and none with two defaults")
    return 0


if __name__ == "__main__":
    sys.exit(main())
