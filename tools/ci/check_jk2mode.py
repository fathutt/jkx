#!/usr/bin/env python3
"""Ratchet the JK2_MODE conditionals, and ban them where they cannot work.

One engine, built twice: jkx_jk2 is the same code as jkx_jka with -DJK2_MODE
and the Outcast gamecode. Every difference between the two games that is not
data is a preprocessor branch, and there are enough of them that "the two games
are one engine" is more of an intention than a description. This does not
untangle them. It stops them multiplying, and it makes one class of them an
error rather than a count.

BANNED: code/rd-vulkan.

The renderer is compiled ONCE and linked into both engines - RDVulkanDefines is
SharedDefines plus three names, and JK2_MODE is appended to EngineDefines, which
the renderer never sees. So a #ifdef JK2_MODE there is not a branch, it is dead
code that never compiles in either game, and #ifndef JK2_MODE is a branch that
always fires in both. Neither says anything at the point of use, and both read
exactly like a working per-game branch.

There are none today, which is why this is a refusal and not a baseline: the
first one to be written is the one to catch, because after it compiles cleanly
and behaves wrongly in only one game, it is a week of somebody's life. The
renderer not knowing which game it is drawing is a property worth keeping -
see tr_ghoul2_bonemap.h for what it cost the last time something in there
decided it knew.

RATCHETED: everywhere else, as a total.

The total is what the ratchet holds, not the per-file counts, and that is
deliberate. The way this number is supposed to come down is by moving a
difference out of the preprocessor and into somewhere it can be read - a table,
a per-game file, a virtual - and a per-file ratchet would fail on the file
receiving it. shared/win32/product.h is what that looks like when it has been
done: five diverging strings behind one #ifdef instead of five.

The per-file counts in the baseline are the record of where they are, so that
--update produces a diff worth reading rather than one number changing.

What is counted: conditional directives whose expression names JK2_MODE - #if,
#ifdef, #ifndef and #elif. Not #else and #endif, which belong to a directive
already counted, and not mentions inside comments or strings.

Usage:
    tools/ci/check_jk2mode.py [--update] [root]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Compiled once, linked into both engines: a JK2_MODE branch here is not a
# branch. See the docstring.
BANNED_DIRS = ("code/rd-vulkan/",)

SKIP = ("third_party/", "build")

SUFFIXES = {".cpp", ".c", ".h", ".hpp", ".inl"}
BASELINE = Path(__file__).with_name("jk2mode-baseline.txt")

# A conditional directive naming JK2_MODE. The directive has to be the first
# thing on the line bar whitespace, which is what keeps the word out of the
# count when it appears in a comment or in a message.
DIRECTIVE = re.compile(
    r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif)\b[^\n]*\bJK2_MODE\b",
    re.MULTILINE,
)

# A line comment or a block comment. Stripped before counting so that a
# commented-out branch is not one - it is not compiled and it cannot be wrong.
COMMENTS = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)


def sources(root: Path):
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        rel = str(path.resolve().relative_to(root.resolve()))
        if rel.startswith(SKIP):
            continue
        yield rel, path


def strip_comments(text: str) -> str:
    # Replaced by newlines rather than removed, so that line numbers survive.
    return COMMENTS.sub(lambda m: "\n" * m.group(0).count("\n"), text)


def scan(root: Path):
    """-> (banned hits as (file, line), counts as {file: n})"""
    banned: list[tuple[str, int]] = []
    counts: dict[str, int] = {}

    for rel, path in sources(root):
        text = path.read_text(errors="replace")
        if "JK2_MODE" not in text:
            continue

        text = strip_comments(text)
        hits = list(DIRECTIVE.finditer(text))
        if not hits:
            continue

        if rel.startswith(BANNED_DIRS):
            for match in hits:
                banned.append((rel, text.count("\n", 0, match.start()) + 1))
        else:
            counts[rel] = len(hits)

    return banned, counts


def read_baseline() -> dict[str, int]:
    if not BASELINE.is_file():
        return {}
    out: dict[str, int] = {}
    for raw in BASELINE.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        count, path = line.split(None, 1)
        out[path.strip()] = int(count)
    return out


def write_baseline(counts: dict[str, int]) -> None:
    lines = [
        "# JK2_MODE conditional directives per file, from",
        "# tools/ci/check_jk2mode.py. The TOTAL may fall and may not rise;",
        "# a single file's count may move either way, because the way this",
        "# number comes down is by moving a difference somewhere it can be read.",
        "# Regenerate with: tools/ci/check_jk2mode.py --update",
        "",
        f"# total: {sum(counts.values())}",
        "",
    ]
    lines += [f"{counts[p]:4d} {p}" for p in sorted(counts)]
    BASELINE.write_text("\n".join(lines) + "\n")


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    root = Path(args[0] if args else ".")

    if not (root / "code" / "qcommon" / "q_shared.h").is_file():
        print("run this from the top of the repository", file=sys.stderr)
        return 2

    banned, counts = scan(root)

    if "--update" in sys.argv:
        if banned:
            print("refusing to update: the renderer has JK2_MODE branches, and")
            print("those are not a number to be recorded. See below.")
        else:
            write_baseline(counts)
            print(f"baseline written: {len(counts)} file(s), "
                  f"{sum(counts.values())} directive(s)")
            return 0

    failed = False

    if banned:
        failed = True
        print("error: JK2_MODE in the renderer, which is compiled once for both games:")
        for rel, line in banned:
            print(f"  {rel}:{line}")
        print()
        print("RDVulkanDefines never contains JK2_MODE, so this branch does not")
        print("select between the games - the #ifdef half is compiled into neither")
        print("and the #ifndef half into both. Whatever differs has to reach the")
        print("renderer as data: a field, a cvar, an argument. The renderer does not")
        print("know which game it is drawing, and that is worth keeping.")
        print()

    baseline = read_baseline()
    total = sum(counts.values())
    ceiling = sum(baseline.values())

    if baseline and total > ceiling:
        failed = True
        grew = sorted(p for p, n in counts.items() if n > baseline.get(p, 0))
        print(f"error: JK2_MODE directives grew: {ceiling} -> {total}")
        for path in grew:
            print(f"  {path}: {baseline.get(path, 0)} -> {counts[path]}")
        print()
        print("The baseline is a ratchet. A new per-game branch is a decision:")
        print("either express the difference as data - see shared/win32/product.h,")
        print("which holds five of them behind one directive - or say in the commit")
        print("message why the preprocessor is the only place it can live.")
        print()

    if failed:
        return 1

    print(f"{total} JK2_MODE directive(s) in {len(counts)} file(s), "
          f"none in the renderer")
    if baseline and total < ceiling:
        print(f"{ceiling - total} fewer than the baseline; run --update to lock it in")
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
