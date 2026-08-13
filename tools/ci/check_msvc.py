#!/usr/bin/env python3
"""Dialect traps that only Microsoft's compiler falls into.

tools/ci/local.sh builds this tree four times, and one of those is a real
Windows build through MinGW-w64. What that still cannot check is MSVC itself:
its dialect and its linker. That gap is not hypothetical - it has already cost
a red CI run and a round trip:

    tr_engine_api.h(42,62): error C2144: syntax error: 'int' should be
                            preceded by ';'

on a line that GCC, Clang and MinGW all compiled without a word. The cause was
NORETURN written after the parameter list. Under GCC the macro expands to
__attribute__((noreturn)), which is a suffix as much as a prefix; under MSVC it
expands to __declspec(noreturn), which is a declaration specifier and nothing
else. Put it at the end and MSVC drops it with warning C4091 and then loses the
thread of the declaration entirely - the error lands on the next token and
reads like it is about something else.

So this file is the local stand-in for the compiler we do not have. Each rule
here is a shape that builds fine everywhere we can build and breaks on the one
platform we cannot, and each one earns its place by having actually happened.
Nothing here is a style rule; a shape only belongs here if MSVC rejects it.
"""

import os
import re
import sys
from pathlib import Path

# Our code. lib/ is vendored and third_party/ is not ours to lint - both are
# built by MSVC in CI already and neither is where our mistakes land.
ROOTS = ["code", "games/jk2", "shared", "tests"]
SKIP_DIRS = {"lib", "third_party", "spirv"}

SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".inl"}

# NORETURN or NORETURN_PTR after a closing parenthesis, on the tail of a
# declaration. The parenthesis is what makes it a suffix: before the name the
# macro is preceded by a return type, never by ")".
TRAILING_NORETURN = re.compile(r"\)\s*(?:const\s+)?NORETURN(?:_PTR)?\b")

# A declaration writing NORETURN and never a "#define". Used only to report how
# many were looked at, so the check cannot pass by finding nothing.
ANY_NORETURN = re.compile(r"\bNORETURN(?:_PTR)?\b")


def sources():
    for root in ROOTS:
        base = Path(root)
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in SUFFIXES:
                continue
            if SKIP_DIRS & set(path.parts):
                continue
            yield path


def main() -> int:
    # An optional root, so this can be run from anywhere. The other checks take
    # the same argument.
    if len(sys.argv) > 1:
        os.chdir(sys.argv[1])

    if not Path("code").is_dir():
        print("run this from the top of the repository", file=sys.stderr)
        return 2

    findings = []
    seen = 0

    for path in sources():
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for number, line in enumerate(text.splitlines(), 1):
            if line.lstrip().startswith("#define"):
                continue
            if ANY_NORETURN.search(line):
                seen += 1
            if TRAILING_NORETURN.search(line):
                findings.append((path, number, line.strip()))

    for path, number, line in findings:
        print(f"error: {path}:{number}: NORETURN after the parameter list")
        print(f"       {line}")

    if findings:
        print()
        print("MSVC expands NORETURN to __declspec(noreturn), which is only valid")
        print("as a declaration specifier. Trailing, it is dropped with warning")
        print("C4091 and the declaration stops parsing - the error MSVC prints is")
        print("about the token after it and does not name the macro at all.")
        print()
        print("Write it before the function name, the way the rest of the tree")
        print("does:  void NORETURN QDECL Com_Error( int code, const char *fmt, ... );")
        print()
        print("Nothing that builds here will catch this: GCC, Clang and the MinGW")
        print("cross-build all accept the trailing form.")
        return 1

    print(f"checked {seen} NORETURN declaration(s)")
    print("OK: no MSVC-only declaration shapes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
