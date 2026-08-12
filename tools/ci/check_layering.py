#!/usr/bin/env python3
"""Enforce section 6.1 of docs/CODING-STANDARDS.md: layer dependencies.

    game/  ->  render/  ->  engine/  ->  platform/
      \\-------------------> engine/

The renderer must not include game headers. The engine must not include
renderer or game headers. Removing the DLL boundary removes the compiler's
help here, so this check replaces it.

Layers are expressed as directory globs because the tree still carries the
historical OpenJK layout; update LAYERS as the phase-2 reorganisation lands.

Usage:
    tools/ci/check_layering.py [--list] [root]
Exit code 1 on the first violation set.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Layer name -> path prefixes that belong to it. Order matters only for output.
LAYERS: dict[str, tuple[str, ...]] = {
    "platform": ("shared/sys", "shared/sdl"),
    "engine": ("code/qcommon", "code/server", "code/client", "shared/qcommon"),
    "render": ("code/rd-vulkan", "code/rd-common"),
    "game": ("code/game", "code/cgame", "code/ui", "code/icarus", "codeJK2"),
}

# What each layer is allowed to include from (itself always allowed).
ALLOWED: dict[str, set[str]] = {
    "platform": set(),
    "engine": {"platform"},
    "render": {"engine", "platform"},
    "game": {"render", "engine", "platform"},
}

# Known violations we inherit and are paying down. Every entry must name the
# phase that removes it, so this list can only shrink.
KNOWN_DEBT: set[tuple[str, str]] = {
    # renderer owns Ghoul2 today; moves to engine/g2 in phase 2.3
    ("render", "game"),
}

# The tree starts with 44 inherited violations (see the project's OpenJK audit roadmap
# section 6.5). Gating on zero from day one would just mean disabling the check,
# so this is a ratchet: the baseline may shrink, never grow.
BASELINE_FILE = Path(__file__).with_name("layering-baseline.txt")

INCLUDE_RE = re.compile(rb'^\s*#\s*include\s*[<"]([^">]+)[">]', re.MULTILINE)
SUFFIXES = {".c", ".cpp", ".cc", ".h", ".hpp", ".inl"}


def layer_of(path: Path, root: Path) -> str | None:
    try:
        rel = path.relative_to(root).as_posix()
    except ValueError:
        return None
    for layer, prefixes in LAYERS.items():
        if any(rel.startswith(p) for p in prefixes):
            return layer
    return None


def resolve(include: str, source: Path, root: Path) -> Path | None:
    """Resolve an #include the way the compiler would, best effort."""
    candidate = (source.parent / include).resolve()
    if candidate.is_file():
        return candidate
    for base in (root, root / "code", root / "shared"):
        candidate = (base / include).resolve()
        if candidate.is_file():
            return candidate
    return None


def violation_key(entry: str) -> tuple[str, str]:
    """A violation is a file and an include, not a position in a file.

    Entries are formatted "path:line: src -> dst  (#include "x")". Keying on
    the whole string would make every edit above an inherited violation look
    like a new one.
    """
    path = entry.split(":", 1)[0]
    include = entry.split("#include", 1)[1].strip() if "#include" in entry else entry
    return (path, include)


def main(argv: list[str]) -> int:
    args = [a for a in argv[1:] if not a.startswith("--")]
    root = Path(args[0] if args else ".").resolve()

    if "--list" in argv:
        for layer, prefixes in LAYERS.items():
            allowed = ", ".join(sorted(ALLOWED[layer])) or "(nothing)"
            print(f"{layer:9s} <- {allowed}\n           dirs: {', '.join(prefixes)}")
        return 0

    violations: list[str] = []
    debt_hits = 0
    checked = 0

    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SUFFIXES:
            continue
        src_layer = layer_of(path, root)
        if src_layer is None:
            continue
        checked += 1
        data = path.read_bytes()
        for match in INCLUDE_RE.finditer(data):
            include = match.group(1).decode("ascii", "replace")
            target = resolve(include, path, root)
            if target is None:
                continue
            dst_layer = layer_of(target, root)
            if dst_layer is None or dst_layer == src_layer:
                continue
            if dst_layer in ALLOWED[src_layer]:
                continue
            if (src_layer, dst_layer) in KNOWN_DEBT:
                debt_hits += 1
                continue
            line = data[: match.start()].count(b"\n") + 1
            rel = path.relative_to(root).as_posix()
            violations.append(f"{rel}:{line}: {src_layer} -> {dst_layer}  (#include \"{include}\")")

    print(f"checked {checked} file(s) across {len(LAYERS)} layer(s)")
    if debt_hits:
        print(f"note: {debt_hits} include(s) matched KNOWN_DEBT and were allowed")

    current = set(violations)

    # Identity is the file and what it includes - never the line number. Adding
    # a field to a struct in q_shared.h shifted three inherited violations six
    # lines down and the ratchet reported them as new, which is a gate crying
    # wolf at the exact moment someone is least able to tell. The line is still
    # printed, because it is useful; it just does not decide whether two entries
    # are the same violation.

    if "--write-baseline" in argv:
        BASELINE_FILE.write_text(
            "# Inherited layer violations. This file may shrink, never grow.\n"
            "# Regenerate only when removing entries: tools/ci/check_layering.py --write-baseline\n"
            + "".join(f"{v}\n" for v in sorted(current))
        )
        print(f"wrote baseline: {len(current)} entry(ies) -> {BASELINE_FILE}")
        return 0

    baseline: set[str] = set()
    if BASELINE_FILE.is_file():
        baseline = {
            line.strip()
            for line in BASELINE_FILE.read_text().splitlines()
            if line.strip() and not line.startswith("#")
        }

    current_by_key = {violation_key(v): v for v in current}
    baseline_keys = {violation_key(v) for v in baseline}

    added = sorted(current_by_key[k] for k in current_by_key.keys() - baseline_keys)
    fixed = sorted(v for v in baseline if violation_key(v) not in current_by_key)

    if fixed:
        print(f"\ngood: {len(fixed)} baseline violation(s) no longer present.")
        print("Run tools/ci/check_layering.py --write-baseline to lock the improvement in.")

    if added:
        print(f"\nFAILED: {len(added)} NEW layering violation(s)")
        for v in added[:40]:
            print("  " + v)
        if len(added) > 40:
            print(f"  ... and {len(added) - 40} more")
        print("\nSee docs/CODING-STANDARDS.md section 6.1.")
        return 1

    print(f"OK: no new layering violations ({len(baseline)} inherited, being paid down)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
