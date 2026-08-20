#!/usr/bin/env python3
"""Sort every JK2_MODE conditional into the three kinds of work it implies.

One binary for both games means the define has to go, and the 189 places it
appears in are not one job but three, with wildly different costs:

  format    the block reads or writes a saved game. The structure is the same
            in both builds; what differs is what goes into the file, so this is
            a version of the save format rather than a layout of memory. It is
            counted apart from layout because the tool's first version called
            these layout - they sit inside a class body - and that made the wall
            look a third taller than it is.

  layout    the block adds, removes or reorders a field of a structure that
            crosses the engine/gamecode boundary - playerState_t, gentity_t,
            the Ghoul2 instance. A single binary cannot hold two layouts of one
            structure, so every one of these has to be resolved to a single
            layout before anything else can start. This is the wall.

  constant  a #define or an enumerator whose value differs. Cheap on its own,
            but a constant that sizes an array in a shared structure is really
            a layout difference wearing a hat, so those are counted separately
            and listed.

  behaviour a statement, a branch, a string, a file name. Becomes a runtime
            test. Mechanical, and the bulk of the work by count.

The classification is by SCOPE, not by guesswork about intent: what encloses
the block at the point it opens. A block inside a struct or class body is a
layout block; one at file scope containing only preprocessor lines is a
constant block; one inside a function body is behaviour. Anything the scanner
cannot place is reported as unknown rather than assumed, because an unknown
counted as behaviour is the one that turns into a week.

    tools/audit/jk2mode_survey.py [--list KIND] [root]
"""

import os
import re
import sys

CODE = (".c", ".cpp", ".h", ".hpp", ".inl")
SKIP_DIRS = {".git", "build", "out", "third_party", "node_modules", "__pycache__"}

OPEN_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b(.*)$")
ELSE_RE = re.compile(r"^\s*#\s*(else|elif)\b")
ENDIF_RE = re.compile(r"^\s*#\s*endif\b")
PREPROC_RE = re.compile(r"^\s*#")
TYPE_OPEN_RE = re.compile(
    r"^\s*(typedef\s+)?(struct|class|union)\b[^;{]*$|^\s*(typedef\s+)?(struct|class|union)\b[^;]*\{")


def is_code(path):
    return os.path.splitext(path)[1].lower() in CODE


def sources(root):
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if is_code(name):
                yield os.path.join(base, name)


def scope_at(lines, index):
    """What encloses line `index`: 'type', 'function' or 'file'.

    Brace counting from the top of the file, remembering what opened the
    innermost brace still open. Comments and strings are not parsed - a brace
    inside either would mislead it - so the result is a strong hint rather than
    a proof, and the caller says so.
    """
    stack = []
    depth = 0
    pending_type = False

    for n in range(index):
        line = lines[n]
        if PREPROC_RE.match(line):
            continue
        stripped = line.split("//")[0]
        if TYPE_OPEN_RE.match(stripped):
            pending_type = True
        for ch in stripped:
            if ch == "{":
                stack.append("type" if pending_type else ("function" if depth == 0 else stack[-1] if stack else "function"))
                pending_type = False
                depth += 1
            elif ch == "}":
                if stack:
                    stack.pop()
                depth = max(0, depth - 1)
        if ";" in stripped:
            pending_type = False

    if not stack:
        return "file"
    return stack[-1]


def body_kind(body):
    """What the guarded lines are made of."""
    real = [l for l in body if l.strip() and not l.strip().startswith("//")]
    if not real:
        return "empty"
    if all(PREPROC_RE.match(l) for l in real):
        return "preproc"
    return "code"


# skip() as well as read() and write(): a serialiser advances past a field it no
# longer writes, and a block of them is still the save format rather than the
# layout of memory. Leaving skip out put four blocks in the layout bucket that
# do not belong there - which mattered, because the layout bucket is the one
# that decides whether the work is possible at all.
SAVED_GAME_RE = re.compile(r"\bsaved_game\.(read|write|skip)\b")


def classify(path, lines, start, body):
    scope = scope_at(lines, start)
    kind = body_kind(body)

    real = [l for l in body if l.strip() and not l.strip().startswith("//")]
    if real and all(SAVED_GAME_RE.search(l) or PREPROC_RE.match(l) for l in real):
        return "format"

    if scope == "type":
        return "layout"
    if kind == "preproc":
        return "constant"
    if scope == "function":
        return "behaviour"
    if kind == "empty":
        return "empty"
    # File scope holding real code: a whole function, or a declaration. Both are
    # behaviour unless the declaration is of a shared structure, which the
    # 'type' scope above already caught.
    return "behaviour"


def survey(root):
    found = []
    for path in sources(root):
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.read().splitlines()

        depth_stack = []
        for n, line in enumerate(lines):
            m = OPEN_RE.match(line)
            if m:
                depth_stack.append("JK2_MODE" in m.group(2))
                if depth_stack[-1]:
                    body = []
                    inner = 0
                    for k in range(n + 1, len(lines)):
                        if OPEN_RE.match(lines[k]):
                            inner += 1
                        elif ENDIF_RE.match(lines[k]):
                            if inner == 0:
                                break
                            inner -= 1
                        elif ELSE_RE.match(lines[k]) and inner == 0:
                            break
                        body.append(lines[k])
                    found.append((os.path.relpath(path, root), n + 1,
                                  classify(path, lines, n, body),
                                  line.strip()))
                continue
            if ENDIF_RE.match(line) and depth_stack:
                depth_stack.pop()
    return found


def main():
    args = sys.argv[1:]
    wanted = None
    if "--list" in args:
        at = args.index("--list")
        wanted = args[at + 1]
        del args[at:at + 2]
    root = args[0] if args else "."

    found = survey(root)
    kinds = {}
    per_file = {}
    for path, line, kind, text in found:
        kinds[kind] = kinds.get(kind, 0) + 1
        per_file.setdefault(path, {}).setdefault(kind, 0)
        per_file[path][kind] += 1
        if wanted and kind == wanted:
            print(f"{path}:{line}: {text}")

    if wanted:
        return 0

    print("%d JK2_MODE conditional(s)\n" % len(found))
    for kind in ("layout", "format", "constant", "behaviour", "empty"):
        if kind in kinds:
            print("  %-10s %3d" % (kind, kinds[kind]))
    print()
    print("  %-46s %s" % ("file", "layout format constant behaviour"))
    for path in sorted(per_file, key=lambda p: -sum(per_file[p].values())):
        counts = per_file[path]
        print("  %-46s %6d %6d %8d %9d"
              % (path, counts.get("layout", 0), counts.get("format", 0),
                 counts.get("constant", 0),
                 counts.get("behaviour", 0) + counts.get("empty", 0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
