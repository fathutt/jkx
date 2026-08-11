#!/usr/bin/env python3
"""Assert that named colours are present in a screenshot.

A count of distinct colours says a frame is not flat. It does not say who drew
it, and that turned out to matter: the in-game frame check was passing on 242
distinct colours of which nearly two hundred belonged to console notify text
still fading at the top of the screen. With con_notifytime 0 the same frame has
51, and the gate would have gone on passing with the head-up display drawing
nothing at all.

So this checks for the colours the fixture's own interface paints. They are flat
fills at exact values, which makes them easy to demand and impossible to satisfy
by accident.

    tga_has_colour.py <shot.tga> R,G,B[:minpixels] [more...]

Colours are given in RGB. Targa stores them the other way round and this handles
that, which is worth stating because getting it backwards produces a check that
passes on the wrong colour.
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


def main(argv):
    if len(argv) < 3:
        print("usage: %s <shot.tga> R,G,B[:minpixels] ..." % argv[0], file=sys.stderr)
        return 2

    path = argv[1]
    wanted = []
    for spec in argv[2:]:
        colour, _, minimum = spec.partition(":")
        r, g, b = (int(v) for v in colour.split(","))
        wanted.append(((r, g, b), int(minimum) if minimum else 1))

    width, height, data, offset, stride = read_tga(path)

    counts = {c: 0 for c, _ in wanted}
    end = offset + width * height * stride
    for i in range(offset, end, stride):
        # Targa is blue, green, red.
        pixel = (data[i + 2], data[i + 1], data[i])
        if pixel in counts:
            counts[pixel] += 1

    failures = []
    for colour, minimum in wanted:
        got = counts[colour]
        if got < minimum:
            failures.append("%s: %d pixel(s) of rgb%s, wanted at least %d"
                            % (path, got, colour, minimum))

    for f in failures:
        print(f, file=sys.stderr)
    if failures:
        return 1

    print("%s: %s" % (path, ", ".join(
        "rgb%s x%d" % (c, counts[c]) for c, _ in wanted)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
