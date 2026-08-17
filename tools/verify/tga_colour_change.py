#!/usr/bin/env python3
"""Count named colours across several frames, and say which of them moved.

The question this answers is not "is the colour there" - tga_has_colour.py does
that - but "did it change between these two moments, and was it supposed to".

It exists for the texture-coordinate lane. A tcMod moves texture coordinates
over time, and what that does to a square painted half black and half one colour
is move the count of that colour. So:

    --unlike A=B      the two colours must have DIFFERENT counts in the first
                      frame. This is how a tcMod that does not animate is
                      checked: it changes the picture once and then holds still,
                      so the only thing that can see it is the same frame
                      compared against a square that has no tcMod at all

    --move R,G,B[:MINSHIFT]
                      the CENTRE of the colour must shift, by at least MINSHIFT
                      thousandths of the frame. For a deform that translates a
                      surface rigidly: its pixel count is the same number
                      wherever it has gone, so counting it says nothing and
                      finding it says everything

    --differ R,G,B    the count MUST change between the two frames. This is a
                      tcMod that animates, and a count that stayed put means it
                      did not run
    --same R,G,B[:MAXMOVE]
                      the count must NOT change, by more than MAXMOVE, which
                      defaults to nothing at all. Two uses: the control square,
                      which has no tcMod on it and takes the default, because a
                      control that is allowed to wobble is not a control; and a
                      static tcMod, which changes the picture once and then
                      holds still and is allowed a pixel or two of edge

A control that has to hold still is deliberate. This project has twice announced
an effect that turned out to be its own noise floor, and both times the thing
that settled it was running A against A before A against B. Here that control is
part of the measurement rather than something to remember to do.

    tga_colour_change.py <frame.tga> <frame.tga> [more.tga ...]
        [--differ R,G,B[:MINCHANGE]] [--same R,G,B] [--unlike A=B]
        [--min-pixels N]

Three frames rather than two, and the reason is measured. A tcMod is periodic,
the lane samples at fixed frame counts, and a rate whose period happens to
divide that interval comes back to exactly where it started - the same scroll
measured 42 pixels of movement in one run and 2 in the next, at the same rate,
because the engine's clock does not advance in step with the frame counter. So
--differ takes the spread across ALL the frames given, and aliasing would have
to happen on every interval at once.

MINCHANGE defaults to 1: any movement counts. Give it a number when a specific
size of movement is the point.

--min-pixels is a floor on how much of each named colour there is in the FIRST
frame, so that "it did not change" cannot be satisfied by a square that is not
being drawn at all. Zero and zero do not change either.
"""

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
        raise ValueError("%s is %d bits per pixel, expected 24 or 32" % (path, bpp))
    return width, height, data, 18 + id_len, bpp // 8


def centroid(path, colour):
    """Where the colour is, in fractions of the frame, and how much of it.

    For the deforms that MOVE geometry rigidly. deformVertexes move translates a
    surface without changing its size, so a count of its pixels is the same
    number wherever it has gone - the thing that changed is where it is, and
    that needs measuring rather than inferring.
    """
    width, height, data, off, stride = read_tga(path)
    r, g, b = colour
    n = 0
    sx = 0
    sy = 0
    for p in range(width * height):
        base = off + p * stride
        if data[base] == b and data[base + 1] == g and data[base + 2] == r:
            n += 1
            sx += p % width
            sy += p // width
    if not n:
        return None, None, 0
    return sx / float(n * width), sy / float(n * height), n


def count(path, colour):
    width, height, data, off, stride = read_tga(path)
    r, g, b = colour
    n = 0
    for p in range(width * height):
        base = off + p * stride
        # Targa stores blue, green, red. Getting that backwards produces a
        # check that passes on the wrong colour, which is worth stating.
        if data[base] == b and data[base + 1] == g and data[base + 2] == r:
            n += 1
    return n


def parse(spec, default=1):
    parts = spec.split(":")
    rgb = tuple(int(v) for v in parts[0].split(","))
    bound = int(parts[1]) if len(parts) > 1 else default
    return rgb, bound


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2

    frames = []
    i = 1
    while i < len(argv) and not argv[i].startswith("--"):
        frames.append(argv[i])
        i += 1
    if len(frames) < 2:
        sys.stderr.write(__doc__)
        return 2
    first = frames[0]

    differ = []
    same = []
    unlike = []
    move = []
    min_pixels = 0

    while i < len(argv):
        if argv[i] == "--differ":
            differ.append(parse(argv[i + 1]))
            i += 2
        elif argv[i] == "--same":
            rgb, most = parse(argv[i + 1], default=0)
            same.append((rgb, most))
            i += 2
        elif argv[i] == "--move":
            move.append(parse(argv[i + 1]))
            i += 2
        elif argv[i] == "--unlike":
            left, right = argv[i + 1].split("=")
            unlike.append((parse(left)[0], parse(right)[0]))
            i += 2
        elif argv[i] == "--min-pixels":
            min_pixels = int(argv[i + 1])
            i += 2
        else:
            sys.stderr.write("unknown option %s\n" % argv[i])
            return 2

    rc = 0

    for rgb, bound in differ + same:
        counts = [count(f, rgb) for f in frames]
        a = counts[0]
        moved = max(counts) - min(counts)
        wanted_change = (rgb, bound) in differ

        print("  rgb(%d, %d, %d): %s, spread %d"
              % (rgb + (", ".join(str(c) for c in counts), moved)))

        if a < min_pixels:
            print("    FAIL: only %d pixel(s) in the first frame, wanted at "
                  "least %d - the square is not being drawn, and a square that "
                  "is not drawn does not change either" % (a, min_pixels))
            rc = 1
            continue

        if wanted_change and moved < bound:
            print("    FAIL: wanted it to move by at least %d across the frames "
                  "and its spread was %d" % (bound, moved))
            rc = 1
        elif not wanted_change and moved > bound:
            print("    FAIL: wanted it to hold still to within %d and its "
                  "spread was %d" % (bound, moved))
            rc = 1

    for rgb, least in move:
        places = [centroid(f, rgb) for f in frames]
        if any(p[2] < max(min_pixels, 1) for p in places):
            print("  rgb(%d, %d, %d): not drawn in every frame - %s"
                  % (rgb + (", ".join(str(p[2]) for p in places),)))
            rc = 1
            continue
        shift = max(
            abs(a[0] - b[0]) + abs(a[1] - b[1])
            for a in places for b in places
        )
        print("  rgb(%d, %d, %d): centre %s, shifted %d thousandth(s)"
              % (rgb + (" -> ".join("%.3f,%.3f" % (p[0], p[1]) for p in places),
                        int(shift * 1000))))
        if int(shift * 1000) < least:
            print("    FAIL: wanted the centre to shift by at least %d "
                  "thousandth(s) and it shifted %d" % (least, int(shift * 1000)))
            rc = 1

    for left, right in unlike:
        a = count(first, left)
        b = count(first, right)
        print("  rgb(%d, %d, %d) %d against rgb(%d, %d, %d) %d in the first frame"
              % (left + (a,) + right + (b,)))
        if a == b:
            print("    FAIL: the same count, so whatever separates these two "
                  "materials did nothing")
            rc = 1

    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
