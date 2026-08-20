#!/usr/bin/env python3
"""Refuse JK2_MODE. There is one engine now, and it has no such define.

This used to be a ratchet over a baseline of 189 conditional directives. The
engine was built twice - the same sources with -DJK2_MODE and the Outcast
gamecode - and every difference between the two games that was not data was a
preprocessor branch. The count came down to zero, and a ratchet at zero is a
ban with extra machinery, so this is the ban.

What replaced it: com_game, a CVAR_INIT cvar read once into a boolean, and
Com_IsOutcast() at every site that used to be a directive. The engine is one
binary; the gamecode is still two modules, and each one calls Com_SetOutcast()
for itself as it starts.

Why it stays as a check rather than being deleted with the define: JK2_MODE is
the obvious name to reach for the next time somebody meets a difference between
the games, and a #ifdef that is defined nowhere fails silently. The #ifdef half
compiles into nothing and the #ifndef half into everything, in both games, and
it reads exactly like a working branch. That is the failure this catches.

The renderer had its own reason and it has not gone away: code/rd-vulkan is
compiled once and knows nothing about which game it is drawing. It is not
mentioned separately any more only because the ban is now everywhere.

What is counted: conditional directives whose expression names JK2_MODE - #if,
#ifdef, #ifndef and #elif. Not #else and #endif, which belong to a directive
already counted, and not mentions inside comments or strings. The word is left
alone in prose on purpose: the history of how this was removed is worth keeping
where it happened.

Usage:
    tools/ci/check_jk2mode.py [root]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SKIP = ("third_party/", "build")

SUFFIXES = {".cpp", ".c", ".h", ".hpp", ".inl"}

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
    hits: list[tuple[str, int]] = []

    for rel, path in sources(root):
        text = path.read_text(errors="replace")
        if "JK2_MODE" not in text:
            continue

        text = strip_comments(text)
        for match in DIRECTIVE.finditer(text):
            hits.append((rel, text.count("\n", 0, match.start()) + 1))

    return hits


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    root = Path(args[0] if args else ".")

    if not (root / "code" / "qcommon" / "q_shared.h").is_file():
        print("run this from the top of the repository", file=sys.stderr)
        return 2

    hits = scan(root)

    if hits:
        print("error: JK2_MODE is not defined anywhere in this build:")
        for rel, line in hits:
            print(f"  {rel}:{line}")
        print()
        print("There is one engine binary and it runs both games. Which one it is")
        print("is com_game, on the command line, read once at startup - ask")
        print("Com_IsOutcast(). A #ifdef on a define nobody sets does not select")
        print("between the games: the #ifdef half compiles into neither and the")
        print("#ifndef half into both, in a build that is otherwise clean.")
        print()
        print("If the difference is a number or a layout rather than a statement,")
        print("it does not belong behind a runtime test either - see")
        print("code/qcommon/jkx_game_limits.h, where the wider of each pair won.")
        return 1

    print("no JK2_MODE directives; one engine, one build")
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
