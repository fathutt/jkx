#!/usr/bin/env python3
"""Two runs of the same fixture, compared frame by frame.

Some changes are supposed to be invisible. A depth pre-pass is the clearest
example: it draws the same geometry into the same depth buffer a frame earlier
so that the hardware can throw occluded fragments away, and if the picture moves
by so much as a pixel it has got the depth wrong. "Looks fine" cannot tell the
difference between that and a hole in a wall you happen not to be facing.

So this compares two directories of screenshots and fails if they disagree by
more than a stated amount.

    ab_frames.py <dir a> <dir b> [--max-pixels N] [--threshold T]

--threshold is the per-channel difference at which a pixel counts as different
at all (default 32, which is well above dither and well below anything a person
would call the same colour). --max-pixels is how many such pixels are tolerated
per frame before the comparison fails.

The tolerance is meant to be earned rather than chosen, and the order in which
this was built is the point. It started out comparing runs that disagreed by
several per cent of the frame: the sky, drawn at the far plane where a hundredth
of a degree of leftover view angle is worth pixels; the horizon settling onto
the floor over real time rather than over frames; a console cursor blinking.
Each of those was removed at the source - see JKX_SMOKE_PLAIN in
smoke_headless.sh - until what remained was one or two pixels on the floor's
edge. Only then was a number written down, and it was written down from a
control run rather than from taste.

That order matters. A tolerance wide enough to swallow a moving horizon is wide
enough to swallow a wall that has gone missing, and the whole point of comparing
frames is to catch the wall.
"""

import os
import struct
import sys


def read_tga(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 18:
        raise ValueError("%s is too short to be a Targa" % path)
    id_len = data[0]
    bpp = data[16]
    width, height = struct.unpack_from("<HH", data, 12)
    if bpp not in (24, 32):
        raise ValueError("%s is %d bits per pixel" % (path, bpp))
    step = bpp // 8
    off = 18 + id_len
    return width, height, step, data[off:off + width * height * step]


def compare(path_a, path_b, threshold):
    wa, ha, sa, a = read_tga(path_a)
    wb, hb, sb, b = read_tga(path_b)

    if (wa, ha) != (wb, hb):
        return None, "%dx%d against %dx%d" % (wa, ha, wb, hb)

    over = 0
    worst = 0
    where = None

    for i in range(wa * ha):
        pa = i * sa
        pb = i * sb
        d = max(abs(a[pa + k] - b[pb + k]) for k in range(3))
        if d > worst:
            worst = d
            where = (i % wa, i // wa)
        if d > threshold:
            over += 1

    return over, "worst channel delta %d at %s" % (worst, where)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write("usage: %s <dir a> <dir b> "
                         "[--max-pixels N] [--threshold T]\n" % argv[0])
        return 2

    dir_a, dir_b = argv[1], argv[2]
    max_pixels = 0
    threshold = 32

    rest = argv[3:]
    while rest:
        flag = rest.pop(0)
        if flag == "--max-pixels":
            max_pixels = int(rest.pop(0))
        elif flag == "--threshold":
            threshold = int(rest.pop(0))
        else:
            sys.stderr.write("unknown option: %s\n" % flag)
            return 2

    # Only the frames the run was told to write. A run that dies partway leaves a
    # timestamped screenshot behind, and comparing against a file the other run
    # never produced reports a difference that is really an early exit.
    names = sorted(n for n in os.listdir(dir_a)
                   if n.startswith("jkx_") and n.endswith(".tga"))
    if not names:
        sys.stderr.write("no screenshots in %s - the run did not get that far\n"
                         % dir_a)
        return 1

    failed = 0
    for name in names:
        pa = os.path.join(dir_a, name)
        pb = os.path.join(dir_b, name)

        if not os.path.exists(pb):
            print("%s: only one of the two runs wrote it" % name)
            failed += 1
            continue

        over, note = compare(pa, pb, threshold)
        if over is None:
            print("%s: %s" % (name, note))
            failed += 1
            continue

        if over > max_pixels:
            print("%s: %d pixels differ by more than %d (allowed %d), %s"
                  % (name, over, threshold, max_pixels, note))
            failed += 1
        else:
            print("  %-22s %d differing pixel(s)" % (name, over))

    if failed:
        print("%d frame(s) changed when they should not have" % failed)
        return 1

    print("the two runs drew the same %d frame(s)" % len(names))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
