#!/usr/bin/env python3
"""Pack a game directory into a pk3 and remove the loose copies.

A pk3 is a zip, and it is how every retail asset and every downloaded mod
arrives. The bench has never had one: tools/verify/fixtures/base is loose files
from end to end, so the whole archive half of the filesystem - FS_LoadZipFile,
the per-pack hash table, the pak branch of FS_FOpenFileRead, unzReadCurrentFile
in FS_Read, the pk3 case of FS_Seek - has never executed in a test. Every run
took the directory branch.

That is the gap this closes. Packing the fixture and deleting what was packed
leaves the engine with no loose file to fall back to, so the run either reads
through the archive or fails.

Two entries are written for every file, deflated and stored, alternating: the
decompressed path and the copied path through minizip are different code, and a
fixture that is entirely one of them tests half of what it looks like it tests.
(A zip may hold either per entry; the engine has no say and no opinion.)

Usage:
    make_pk3.py <base dir> <out.pk3> [--keep <relative path>]...

--keep leaves a file loose as well as packing it. Nothing needs it today; it is
here because a file the engine insists on finding outside an archive is exactly
the kind of thing this script exists to discover, and the discovery should not
have to be a patch.
"""

import os
import sys
import zipfile


def collect(base, out_name):
    """Every file under base, as (absolute path, name inside the zip)."""
    found = []
    for root, _dirs, files in os.walk(base):
        for name in sorted(files):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, base)
            if rel.replace(os.sep, "/") == out_name:
                continue
            found.append((full, rel.replace(os.sep, "/")))
    found.sort(key=lambda pair: pair[1])
    return found


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    base = argv[1]
    out = argv[2]
    keep = set()

    rest = argv[3:]
    while rest:
        if rest[0] == "--keep" and len(rest) > 1:
            keep.add(rest[1])
            rest = rest[2:]
        else:
            print("unknown argument: %s" % rest[0], file=sys.stderr)
            return 2

    if not os.path.isdir(base):
        print("no such directory: %s" % base, file=sys.stderr)
        return 2

    out_name = os.path.relpath(out, base).replace(os.sep, "/")
    entries = collect(base, out_name)
    if not entries:
        print("nothing to pack in %s" % base, file=sys.stderr)
        return 2

    with zipfile.ZipFile(out, "w") as z:
        for i, (full, rel) in enumerate(entries):
            method = zipfile.ZIP_DEFLATED if (i % 2) == 0 else zipfile.ZIP_STORED
            z.write(full, rel, compress_type=method)

    removed = 0
    for full, rel in entries:
        if rel in keep:
            continue
        os.unlink(full)
        removed += 1

    # Empty directories left behind are not harmless: FS_AddGameDirectory adds
    # the directory to the search path whatever is in it, so an empty tree still
    # gives the directory branch something to walk. Removing them is what makes
    # "the file came out of the archive" the only explanation left.
    for root, dirs, files in os.walk(base, topdown=False):
        del dirs
        if not files and not os.listdir(root) and os.path.abspath(root) != os.path.abspath(base):
            os.rmdir(root)

    print("%s: %i file(s) packed, %i removed" % (out, len(entries), removed))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
