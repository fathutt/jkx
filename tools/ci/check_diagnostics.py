#!/usr/bin/env python3
"""A warning nobody can see is not a warning.

vk_debug() compiles to nothing unless the build defines _DEBUG or
JKX_VK_TRACE, which no release build does. It is the renderer's trace channel
and that is a fine thing to have - but 125 messages beginning WARNING or ERROR
had been written through it, 120 of them in tr_shader.cpp, and every one was a
message for the person who wrote the .shader file. In a release build they did
not exist: a typo in a material silently got GL_ONE and drew wrong.

This is the third time this project has paid for the same shape. The ICARUS
script diagnostics were silent in both games because the default of
g_ICARUSDebug sat below WL_ERROR. The census line was deliberately put on the
ordinary printing path because "diagnostics nobody turns on are not
diagnostics". And the two warnings this gate was written to prevent were added
by the same person who had written that sentence down, four hours earlier, in
the audit that found the other 125.

So it is a gate rather than a habit.

The rule: a diagnostic addressed to the user does not go out through a channel
the user does not have. Anything else vk_debug says is left alone.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Files that may call vk_debug at all. Everything here is the renderer's own
# trace: object creation, pipeline selection, frame boundaries.
SEARCH = ["code", "games", "shared"]

# What counts as addressed to the user rather than to us.
ADDRESSED = re.compile(
    r'vk_debug\s*\(\s*(?:S_COLOR_\w+\s+)?"\s*(WARNING|ERROR|FAILED)\b',
    re.IGNORECASE)

# Written on one line in this tree, but a continuation would hide from a
# line-at-a-time reader, so the file is read whole and matched across newlines.
CALL = re.compile(r'vk_debug\s*\(\s*(?:S_COLOR_\w+\s+)?"(.*?)"', re.DOTALL)


def main() -> int:
    bad = []
    checked = 0

    for top in SEARCH:
        for path in sorted((ROOT / top).rglob("*.cpp")):
            text = path.read_text(encoding="utf-8", errors="replace")
            if "vk_debug" not in text:
                continue
            checked += 1
            for m in CALL.finditer(text):
                first = m.group(1).lstrip()
                word = first.split(":")[0].split()[0] if first.split() else ""
                if word.upper() in ("WARNING", "ERROR", "FAILED"):
                    line = text.count("\n", 0, m.start()) + 1
                    bad.append((path.relative_to(ROOT), line, first[:60]))

    if bad:
        print("Diagnostics that the user cannot see:\n")
        for path, line, msg in bad:
            print(f"  {path}:{line}: {msg}")
        print(f"\n{len(bad)} message(s) addressed to the user go out through "
              "vk_debug, which\ncompiles to nothing in a release build. Use "
              "CL_RefPrintf( PRINT_WARNING, ... )\nfor anything the person "
              "editing the asset is meant to read.")
        return 1

    print(f"checked {checked} file(s) that call vk_debug")
    print("OK: no user-facing diagnostic hides in the trace channel")
    return 0


if __name__ == "__main__":
    sys.exit(main())
