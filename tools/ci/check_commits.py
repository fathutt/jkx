#!/usr/bin/env python3
"""No commit message may point at a conversation.

The rule is CODING-STANDARDS.md section 12.2, and the reason is shelf life. A
commit message is the only explanation that reaches whoever reads git log in
three years: it is versioned with the code, it survives a change of hosting, and
it works with no network. A link has none of those properties. It goes stale
without saying so and leaves a promise of an explanation where the explanation
should have been written.

So if something from the conversation matters, it goes in the message. If there
is nothing to write, there is nothing to link to either.

Co-Authored-By stays. That is attribution, not a pointer.

This is a ratchet, not a judgement call: the history was rewritten once to
remove 159 of these trailers, so the correct count is zero and any number above
zero is a new one.

    check_commits.py [rev-range]      default: every commit reachable from HEAD
"""

import re
import subprocess
import sys

# Deliberately broad. The trailer this was written for is "Claude-Session:",
# but the rule is about pointing at a conversation, not about one spelling of
# it, and the next tool to do this will spell it differently.
FORBIDDEN = [
    (re.compile(r"^\s*[A-Za-z-]*Session\s*:", re.M | re.I),
     "a session trailer"),
    (re.compile(r"https?://\S*/(?:session|chat|conversation)[_/]\S+", re.I),
     "a link to a conversation"),
    (re.compile(r"https?://claude\.ai/\S+", re.I),
     "a claude.ai link"),
]


def commits(rev_range: str) -> list[tuple[str, str]]:
    """Every commit in the range, as (short sha, full message)."""
    sep = "\x1e"
    out = subprocess.run(
        ["git", "log", "--format=%h%x1f%B%x1e", rev_range],
        capture_output=True, text=True, check=True).stdout

    found = []
    for chunk in out.split(sep):
        chunk = chunk.strip("\n")
        if not chunk:
            continue
        sha, _, body = chunk.partition("\x1f")
        found.append((sha.strip(), body))
    return found


def main(argv: list[str]) -> int:
    rev_range = argv[1] if len(argv) > 1 else "HEAD"

    try:
        history = commits(rev_range)
    except subprocess.CalledProcessError as exc:
        print(f"git log failed: {exc}", file=sys.stderr)
        return 2

    bad = 0
    for sha, body in history:
        for pattern, what in FORBIDDEN:
            match = pattern.search(body)
            if match:
                print(f"error: {sha} has {what}: {match.group(0).strip()}")
                bad += 1
                break

    if bad:
        print()
        print("A commit message is the explanation that outlives the link.")
        print("Write what matters into the message; see CODING-STANDARDS.md 12.2.")
        print("To fix the most recent one: git commit --amend")
        return 1

    print(f"checked {len(history)} commit message(s), none points at a conversation")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
