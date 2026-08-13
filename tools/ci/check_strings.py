#!/usr/bin/env python3
"""Refuse string functions that cannot be written safely, and ratchet the rest.

Three groups, treated differently because they are different defects.

Banned outright. strcpy, strcat, vsprintf and sprintf take no destination size
at all, so a correct call is one where the author checked something the compiler
cannot see. There are none left in our code; every one that used to be here was
either a measured copy - the same length that was allocated - or a real overflow
reachable from game data. Adding one back is a decision, not an oversight, so
this refuses it rather than counting it.

sprintf moved into that group when the last of its sixty-one call sites went.
It was ratcheted rather than banned on the grounds that a format usually has
output that fits - which is a claim about values, not about types, and therefore
not one a reader can check. What replaced it is Com_sprintf with the size taken
from the destination's type; see q_shared.h. Every one of the sixty-one had an
array for a destination, which the compiler confirmed by accepting the overload,
so not one of them needed the judgement the ratchet existed to defer.

Ratcheted. strncpy and strncat are bounded-in-principle: they take a size but do
not terminate. Both still appear in quantity and both need a per-site judgement,
so they are counted per file in the baseline beside this script. A count may fall
and may not rise, and a file that reaches zero drops out of the baseline
entirely.

Not looked at. third_party/ is vendored upstream source, and
code/rd-vulkan carries a renderer we want to keep diffable against EternalJK.
Rewriting either would cost more than it buys.

Usage:
    tools/ci/check_strings.py [--update] [root]

--update rewrites the baseline from what is on disk. Use it after a sweep, and
read the diff: every line it moves is a call site that stopped being a question.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

BANNED = ("strcpy", "strcat", "vsprintf", "sprintf")
RATCHETED = ("strncpy", "strncat")

# Directories whose string handling is not ours to change.
SKIP = ("third_party/", "code/mp3code/", "code/rd-vulkan/", "build")

SUFFIXES = {".cpp", ".c", ".h", ".hpp"}
BASELINE = Path(__file__).with_name("strings-baseline.txt")

# A call, not a mention: the name followed by an open bracket, with nothing in
# front of it that would make it part of a longer identifier or a member access.
# That last part is what keeps Q_strncpyz, Com_sprintf and str.copy out of the
# count while catching a bare ::strcpy.
CALL = re.compile(
    r"(?<![\w.])(?<!->)(?<!Q_)(?<!Com_)(?:std::)?(" +
    "|".join(BANNED + RATCHETED) + r")\s*\("
)


def sources(root: Path):
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        rel = str(path.resolve().relative_to(root.resolve()))
        if rel.startswith(SKIP):
            continue
        yield rel, path


def scan(root: Path):
    """-> (banned hits as (file, line, name), ratcheted counts as {file: n})"""
    banned: list[tuple[str, int, str]] = []
    counts: dict[str, int] = {}
    for rel, path in sources(root):
        text = path.read_text(errors="replace")
        if not any(name in text for name in BANNED + RATCHETED):
            continue
        for match in CALL.finditer(text):
            name = match.group(1)
            if name in BANNED:
                line = text.count("\n", 0, match.start()) + 1
                banned.append((rel, line, name))
            else:
                counts[rel] = counts.get(rel, 0) + 1
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
        "# strncpy and strncat call sites per file, from",
        "# tools/ci/check_strings.py. A count may fall and may not rise.",
        "# Regenerate with: tools/ci/check_strings.py --update",
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
        write_baseline(counts)
        print(f"baseline written: {len(counts)} file(s), {sum(counts.values())} call(s)")
        return 0

    failed = False

    if banned:
        failed = True
        print("error: string functions with no destination size:")
        for rel, line, name in banned:
            print(f"  {rel}:{line}: {name}")
        print()
        print("These take no bound, so the caller has to have proved the fit by hand.")
        print("Use Q_strncpyz, Q_strcat or Com_sprintf - all three take the size, and")
        print("the array forms take it from the type (q_string.h, q_shared.h). Where")
        print("the length really is known,")
        print("memcpy with the length that was allocated says so and this gate agrees.")
        print()

    baseline = read_baseline()
    grew = sorted(p for p, n in counts.items() if n > baseline.get(p, 0))
    if grew:
        failed = True
        print("error: strncpy/strncat call sites grew:")
        for path in grew:
            print(f"  {path}: {baseline.get(path, 0)} -> {counts[path]}")
        print()
        print("The baseline is a ratchet. Either use the bounded form -")
        print("Com_sprintf or Q_strncpyz - or say in the commit why the count moves.")
        print()

    if failed:
        return 1

    total = sum(counts.values())
    shrunk = sum(baseline.values()) - total
    print(f"no unbounded string calls; {total} ratcheted call(s) in {len(counts)} file(s)")
    if shrunk > 0:
        print(f"{shrunk} fewer than the baseline; run --update to lock it in")
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
