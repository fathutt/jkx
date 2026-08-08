#!/bin/sh
# Run me. Finds the Steam install of Jedi Academy on its own, runs the
# measurements, and prints where the report went. Arguments are passed through,
# so "./verify.sh --title jk2" works.

here=$(cd "$(dirname "$0")" && pwd)

if command -v python3 >/dev/null 2>&1; then
    python3 "$here/verify.py" "$@"
elif command -v python >/dev/null 2>&1; then
    python "$here/verify.py" "$@"
else
    echo "Python 3 was not found on PATH." >&2
    exit 1
fi
