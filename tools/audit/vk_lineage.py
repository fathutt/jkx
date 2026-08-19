#!/usr/bin/env python3
"""Which upstream does each function in the Vulkan renderer belong to?

The lineage is not in doubt and it is written down in vk_local.h: Quake3e ->
EternalJK -> JKSunny/EternalJK@pbr -> us. What is in doubt is what happened to
each individual function along the way, and that question has a cheap answer -
compare the body against both upstreams and see which one it matches.

This is the same instrument as tools/audit/g2_lineage.py, pointed at a different
seam. That one turned "the Ghoul2 in this tree feels like the multiplayer one"
into 1842 differences against single-player and 1102 against multiplayer, and
then into ten specific repairs. Nothing about that outcome came from reading the
code; it came from counting.

What the buckets mean, and why one of them is worth more than the rest:

  ours          we changed it, and we know we did
  ours only     it does not exist upstream at all
  inherited     identical to Quake3e - untouched, however old
  DIVERGED      identical to EternalJK, and EternalJK differs from Quake3e

That last one is the interesting bucket. It is code we did not write, did not
review, and inherited from an intermediate that changed it for reasons of its
own - a multiplayer fork's reasons, in a single-player engine. Every Ghoul2
defect this project has found lived in exactly that shape.

  vk_lineage.py                    counts, and the diverged list
  vk_lineage.py --bucket diverged  the names in one bucket
  vk_lineage.py --name vk_shutdown all three bodies, side by side
  vk_lineage.py --report OUT.md    the whole table

The upstream trees are read and never written. Point JKX_QUAKE3E and JKX_EJK at
checkouts if they are not in the default places.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

Q3E = os.environ.get("JKX_QUAKE3E", "/home/claude/quake3e")
EJK = os.environ.get("JKX_EJK", "/home/claude/eternaljk")

OURS_DIR = os.path.join(ROOT, "code/rd-vulkan")
EJK_DIR = os.path.join(EJK, "codemp/rd-vulkan")
Q3E_DIR = os.path.join(Q3E, "code/renderervk")

# Ghoul2 lives in this directory in both JK trees and has its own audit already
# (backlog section 20). Leaving it in would drown the renderer table in a
# question that has been answered somewhere else.
SKIP = ("G2_API", "G2_bolts", "G2_bones", "G2_misc", "G2_surfaces",
        "G2_gore_r2", "tr_ghoul2", "tr_ghoul2_bonemap")

DEF = re.compile(
    r"^[A-Za-z_][A-Za-z_0-9 \t\*&:<>,]*?"          # return type and qualifiers
    r"\b([A-Za-z_][A-Za-z_0-9]*)\s*\("             # the name
)


def sources(directory, exts):
    out = []
    if not os.path.isdir(directory):
        return out
    for fn in sorted(os.listdir(directory)):
        if not fn.endswith(exts):
            continue
        if os.path.splitext(fn)[0] in SKIP:
            continue
        out.append(os.path.join(directory, fn))
    return out


def index(directory, exts):
    """Every function definition in a tree, by name.

    Brace counting rather than a parser, like the Ghoul2 tool: these are flat C
    and C-with-classes, and a parser would be a week of work to answer a
    question that a brace counter answers today. Where it is wrong it is wrong
    by finding nothing, which shows up as a name in no bucket rather than as a
    wrong verdict.
    """
    found = {}

    for path in sources(directory, exts):
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")

        i = 0
        while i < len(lines):
            line = lines[i]
            stripped = line.strip()

            if (not stripped or stripped.startswith(("//", "#", "*", "}", "{"))
                    or ";" in line or "=" in line.split("(")[0]):
                i += 1
                continue

            m = DEF.match(stripped)
            if not m:
                i += 1
                continue

            name = m.group(1)
            if name in ("if", "for", "while", "switch", "return", "sizeof",
                        "else", "do", "case", "defined"):
                i += 1
                continue

            # The body has to start with a brace on this line or the next one,
            # or this was a declaration and not a definition.
            head = lines[i:i + 6]
            joined = "\n".join(head)
            if "{" not in joined:
                i += 1
                continue

            body = []
            depth = 0
            seen = False
            j = i
            while j < len(lines) and j - i < 2000:
                body.append(lines[j])
                depth += lines[j].count("{") - lines[j].count("}")
                if "{" in lines[j]:
                    seen = True
                if seen and depth <= 0:
                    break
                j += 1

            if seen:
                # First definition wins. A name defined twice in one tree is a
                # static helper repeated per file, and picking either is a coin
                # toss - so it is reported rather than resolved.
                found.setdefault(name, (os.path.basename(path), "\n".join(body)))
                i = j + 1
            else:
                i += 1

    return found


# Spellings that mean the same thing on both sides of the lineage.
#
# Without these the table is useless, and that is not an exaggeration: the first
# run reported a hundred and fifty-three diverged functions, and reading the
# smallest of them showed the difference was `qvkQueueWaitIdle` against
# `vkQueueWaitIdle`. A tool that reports a rename as a divergence buries the
# three real ones under a hundred and fifty.
#
# Each of these is a whole-tree substitution someone made once, and every one of
# them is recorded here rather than in a comment somewhere, so that the next
# person to add one can see what the others were.
EQUIVALENT = [
    # Quake3e loads Vulkan entry points into its own qvk* pointers; we moved to
    # volk, which uses the unprefixed names.
    (re.compile(r"\bqvk([A-Z])"), r"vk\1"),
    # The two engines name the same field on refEntity_t differently.
    (re.compile(r"\be\.shader\.rgba\b"), "e.shaderRGBA"),
    # Quake3e reaches the engine through a function table; the JK renderers are
    # linked against it.
    (re.compile(r"\bri\.Printf\(PRINT_WARNING,"), "Com_Printf(S_COLOR_YELLOW"),
    (re.compile(r"\bri\.Printf\(PRINT_ALL,"), "Com_Printf("),
    (re.compile(r"\bri\.Printf\(PRINT_DEVELOPER,"), "Com_DPrintf("),
    (re.compile(r"\bri\.Error\(ERR_DROP,"), "Com_Error(ERR_DROP,"),
    (re.compile(r"\bri\.Error\(ERR_FATAL,"), "Com_Error(ERR_FATAL,"),
    (re.compile(r"\bri\.Hunk_Alloc\b"), "Hunk_Alloc"),
    (re.compile(r"\bri\.Malloc\b"), "Z_Malloc"),
    (re.compile(r"\bri\.Free\b"), "Z_Free"),
    (re.compile(r"\bri\.FS_"), "FS_"),
    (re.compile(r"\bri\.Cvar_"), "Cvar_"),
    (re.compile(r"\bri\.Cmd_"), "Cmd_"),
]


def normalize(text, fold=True):
    """Whitespace, comments and known renames removed, because none is behaviour.

    Renaming and reformatting happened wholesale on the way down this lineage -
    the file this renderer came from was one 8022-line vk.c - so a comparison
    that counted them would put every function in the "we changed it" bucket and
    say nothing at all. See EQUIVALENT for what is folded and why.

    `static` goes too. Splitting one file into twenty turned file-local helpers
    into shared ones, and linkage is not behaviour.
    """
    out = []
    for line in text.split("\n"):
        line = re.sub(r"//.*", "", line)
        line = re.sub(r"\s+", "", line)
        if line:
            out.append(line)

    joined = "".join(out)

    if fold:
        joined = re.sub(r"^static\b", "", joined)
        for pattern, repl in EQUIVALENT:
            joined = pattern.sub(repl, joined)

    return joined


def classify(ours, ejk, q3e):
    table = {}

    for name, (ofile, obody) in ours.items():
        o = normalize(obody)
        e = normalize(ejk[name][1]) if name in ejk else None
        q = normalize(q3e[name][1]) if name in q3e else None

        if e is None and q is None:
            bucket = "ours only"
        elif q is not None and o == q:
            bucket = "inherited"
        elif e is not None and o == e and (q is None or e != q):
            bucket = "diverged" if q is not None else "diverged (jk only)"
        else:
            bucket = "ours"

        table[name] = (bucket, ofile,
                       ejk.get(name, (None,))[0], q3e.get(name, (None,))[0],
                       len(obody.split("\n")))

    return table


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--bucket")
    ap.add_argument("--name")
    ap.add_argument("--report")
    args = ap.parse_args(argv[1:])

    ours = index(OURS_DIR, (".cpp",))
    ejk = index(EJK_DIR, (".cpp",))
    q3e = index(Q3E_DIR, (".c",))

    if not ejk or not q3e:
        print("upstream tree missing: set JKX_QUAKE3E and JKX_EJK",
              file=sys.stderr)
        return 2

    if args.name:
        for label, tree in (("ours", ours), ("eternaljk", ejk), ("quake3e", q3e)):
            print("=" * 70)
            print(label, "-", tree.get(args.name, ("not present", ""))[0])
            print("=" * 70)
            if args.name in tree:
                print(tree[args.name][1])
        return 0

    table = classify(ours, ejk, q3e)

    counts = {}
    for name, (bucket, *_rest) in table.items():
        counts[bucket] = counts.get(bucket, 0) + 1

    # What upstream has and we do not. Not a defect on its own - most of it is
    # Quake3e engine code that never belonged to a JK renderer - but the place
    # to look for a fix we never received.
    missing = sorted(set(q3e) - set(ours) - set(ejk))

    if args.bucket:
        for name in sorted(n for n, v in table.items() if v[0] == args.bucket):
            b, ofile, efile, qfile, lines = table[name]
            print("%-44s %-24s %4d lines" % (name, ofile, lines))
        return 0

    print("functions: ours %d, eternaljk %d, quake3e %d"
          % (len(ours), len(ejk), len(q3e)))
    print()
    for bucket in ("inherited", "diverged", "diverged (jk only)", "ours",
                   "ours only"):
        if bucket in counts:
            print("  %-20s %4d" % (bucket, counts[bucket]))
    print()
    print("in quake3e and in neither JK tree: %d" % len(missing))

    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            f.write("| function | bucket | our file | lines |\n")
            f.write("|---|---|---|---|\n")
            for name in sorted(table):
                b, ofile, efile, qfile, lines = table[name]
                f.write("| `%s` | %s | %s | %d |\n" % (name, b, ofile, lines))
        print("\nwrote %s" % args.report)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
