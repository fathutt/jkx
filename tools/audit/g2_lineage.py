#!/usr/bin/env python3
"""Pull one function's body out of three trees, so the three can be compared.

The Ghoul2 sources in this repository came from multiplayer - measured, not
guessed: against Jedi Academy's two upstreams every one of G2_API, G2_bones,
G2_bolts, G2_surfaces and G2_misc is closer to codemp than to code. The gamecode
calling them is single-player's.

That seam has already produced two live defects. G2_Set_Bone_Angles_Index
refused the BONE_ANGLES_POSTMULT the single-player gamecode passes on every
player spawn, because multiplayer cannot hand a model pointer across the virtual
machine boundary and single-player can. G2API_GetBoltMatrix turned every bolt
matrix in the game ninety degrees, because multiplayer's gamecode expects that
and single-player's does not.

Neither was found by looking. This is the tool for looking.

    g2_lineage.py --list                 the functions the SP gamecode calls
    g2_lineage.py --report OUT.md        one section per function, three bodies
    g2_lineage.py --name G2API_AddBolt   just that one, to stdout

It needs the two upstream trees; point JKX_OPENJK at a checkout of JACoders/OpenJK
(default /home/claude/openjk). It reads them and never writes to them.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
UPSTREAM = os.environ.get("JKX_OPENJK", "/home/claude/openjk")

# Where each tree keeps the Ghoul2 implementation.
OURS = [
    os.path.join(ROOT, "code/ghoul2/G2_API.cpp"),
    os.path.join(ROOT, "code/ghoul2/G2_bones.cpp"),
    os.path.join(ROOT, "code/ghoul2/G2_bolts.cpp"),
    os.path.join(ROOT, "code/ghoul2/G2_surfaces.cpp"),
    os.path.join(ROOT, "code/rd-vulkan/G2_misc.cpp"),
    os.path.join(ROOT, "code/rd-vulkan/tr_ghoul2.cpp"),
]
SP = [os.path.join(UPSTREAM, "code/rd-vanilla", f) for f in
      ("G2_API.cpp", "G2_bones.cpp", "G2_bolts.cpp", "G2_surfaces.cpp",
       "G2_misc.cpp", "tr_ghoul2.cpp")]
MP = [os.path.join(UPSTREAM, "codemp/rd-vanilla", f) for f in
      ("G2_API.cpp", "G2_bones.cpp", "G2_bolts.cpp", "G2_surfaces.cpp",
       "G2_misc.cpp", "tr_ghoul2.cpp")]

# Where the single-player gamecode asks for things.
CALLERS = ["code/game", "code/cgame", "code/ui", "code/server",
           "games/jk2/game", "games/jk2/cgame"]

CALL = re.compile(r"\b(?:gi|cgi|ui|re)\.(G2API_[A-Za-z_0-9]+)")


def called_functions():
    names = set()
    for rel in CALLERS:
        base = os.path.join(ROOT, rel)
        for dirpath, _, filenames in os.walk(base):
            for fn in filenames:
                if not fn.endswith((".cpp", ".h")):
                    continue
                with open(os.path.join(dirpath, fn), encoding="utf-8",
                          errors="replace") as f:
                    names.update(CALL.findall(f.read()))
    return sorted(names)


def extract(path, name):
    """The definition of one function, by brace counting.

    Not a parser. It looks for a line that both mentions the name followed by an
    open parenthesis and is not a call - no leading dot, no semicolon at the end
    - then follows braces to the close. Good enough for these files, which are
    flat C-with-classes, and it says so when it finds nothing.
    """
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")
    except OSError:
        return None

    start = None
    pattern = re.compile(r"(^|[\s*&])" + re.escape(name) + r"\s*\(")
    for i, line in enumerate(lines):
        if not pattern.search(line):
            continue
        stripped = line.strip()
        if stripped.startswith(("//", "*", "return", "}")):
            continue
        if "." + name in line or "->" + name in line:
            continue
        # A declaration ends in a semicolon on this line or the next few.
        head = " ".join(lines[i:i + 4])
        body = head.split(name, 1)[1]
        close = body.find(")")
        if close == -1:
            continue
        after = body[close + 1:].lstrip()
        if after.startswith(";"):
            continue
        start = i
        break

    if start is None:
        return None

    out = []
    depth = 0
    seen = False
    for line in lines[start:]:
        out.append(line)
        depth += line.count("{") - line.count("}")
        if "{" in line:
            seen = True
        if seen and depth <= 0:
            break
        if len(out) > 400:
            break
    return "\n".join(out)


def find_in(paths, name):
    for p in paths:
        body = extract(p, name)
        if body is not None:
            return os.path.relpath(p, os.path.dirname(p) + "/../.."), body
    return None, None


def normalize(text):
    if text is None:
        return None
    out = []
    for line in text.split("\n"):
        line = re.sub(r"//.*", "", line)
        line = re.sub(r"\s+", "", line)
        if line:
            out.append(line)
    return "\n".join(out)


def classify(ours, sp, mp):
    n_ours, n_sp, n_mp = normalize(ours), normalize(sp), normalize(mp)
    if n_ours is None:
        return "missing here"
    if n_sp is None and n_mp is None:
        return "not in either upstream"
    if n_sp is None:
        return "multiplayer only"
    if n_mp is None:
        return "single-player only"
    if n_sp == n_mp:
        return "upstreams agree"
    if n_ours == n_sp:
        return "we match SP"
    if n_ours == n_mp:
        return "WE MATCH MP, and the upstreams differ"
    return "we match neither, and the upstreams differ"


def main(argv):
    names = called_functions()

    if "--list" in argv:
        for n in names:
            print(n)
        return 0

    if "--name" in argv:
        names = [argv[argv.index("--name") + 1]]

    out = sys.stdout
    outpath = None
    if "--report" in argv:
        outpath = argv[argv.index("--report") + 1]
        out = open(outpath, "w", encoding="utf-8")

    counts = {}
    for name in names:
        _, ours = find_in(OURS, name)
        _, sp = find_in(SP, name)
        _, mp = find_in(MP, name)
        verdict = classify(ours, sp, mp)
        counts[verdict] = counts.get(verdict, 0) + 1

        print("\n## %s\n\n%s\n" % (name, verdict), file=out)
        for label, body in (("ours", ours), ("upstream SP", sp),
                            ("upstream MP", mp)):
            print("### %s\n" % label, file=out)
            print("```c\n%s\n```\n" % (body or "(not found)"), file=out)

    if outpath:
        out.close()

    for verdict, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        print("%4d  %s" % (n, verdict), file=sys.stderr)
    print("%4d  total" % len(names), file=sys.stderr)
    if outpath:
        print("report: %s" % outpath, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
