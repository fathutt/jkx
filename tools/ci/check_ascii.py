#!/usr/bin/env python3
"""Enforce rule 1 of docs/CODING-STANDARDS.md.

Code and comments are Latin-only. Cyrillic (and any other non-ASCII) is
allowed in documentation only.

Rationale is in the standards document; the short version is that MSVC
without /utf-8 reads sources in the system code page, debuggers mangle
non-ASCII symbol names, and grep stops being predictable.

Usage:
    tools/ci/check_ascii.py [paths...]     # defaults to the whole repo
Exit code 1 on the first offending file set.
"""

from __future__ import annotations

import sys
import unicodedata
from pathlib import Path

# Extensions that count as "code" for the purposes of rule 1.
CODE_SUFFIXES = {
    ".c", ".cpp", ".cc", ".h", ".hpp", ".inl",
    ".glsl", ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese", ".tmpl",
    ".cmake", ".txt.cmake",
    ".py", ".sh", ".bat", ".ps1",
    ".yml", ".yaml", ".json",
}

# Directories never scanned: vendored code is not ours to reformat, and
# generated output is not in git anyway (standards 9.3).
SKIP_DIRS = {
    ".git", "build", "out", "third_party", "lib",
    "docs", "node_modules", "__pycache__",
}

# Individual files exempted with a reason. Keep this list short and justified.
EXEMPT = {
    # none yet
}


def is_code_file(path: Path) -> bool:
    if path.name == "CMakeLists.txt":
        return True
    return path.suffix.lower() in CODE_SUFFIXES


def describe(ch: str) -> str:
    try:
        name = unicodedata.name(ch)
    except ValueError:
        name = "<unnamed>"
    return f"U+{ord(ch):04X} {name}"


def scan(path: Path) -> list[tuple[int, int, str]]:
    """Return [(line, column, char)] for every non-ASCII character."""
    hits: list[tuple[int, int, str]] = []
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        hits.append((1, 1, "\ufeff"))  # BOM: standards require UTF-8 without BOM
        raw = raw[3:]
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        hits.append((0, exc.start, "?"))
        return hits
    for lineno, line in enumerate(text.splitlines(), start=1):
        for col, ch in enumerate(line, start=1):
            if ord(ch) > 0x7F:
                hits.append((lineno, col, ch))
    return hits


def main(argv: list[str]) -> int:
    roots = [Path(a) for a in argv[1:]] or [Path(".")]
    offenders = 0
    total_chars = 0

    files: list[Path] = []
    for root in roots:
        if root.is_file():
            files.append(root)
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            if str(path) in EXEMPT:
                continue
            if is_code_file(path):
                files.append(path)

    for path in sorted(files):
        hits = scan(path)
        if not hits:
            continue
        offenders += 1
        total_chars += len(hits)
        print(f"{path}: {len(hits)} non-ASCII character(s)")
        for lineno, col, ch in hits[:5]:
            print(f"    {path}:{lineno}:{col}: {describe(ch)}")
        if len(hits) > 5:
            print(f"    ... and {len(hits) - 5} more")

    print(f"\nchecked {len(files)} file(s)")
    if offenders:
        print(f"FAILED: {offenders} file(s), {total_chars} non-ASCII character(s)")
        print("Rule 1: code and comments are Latin-only. Cyrillic belongs in docs/.")
        return 1
    print("OK: no non-ASCII characters in code")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
