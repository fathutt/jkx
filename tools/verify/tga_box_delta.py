#!/usr/bin/env python3
"""Do two parts of one frame differ, and by how much?

Every other comparison here is between two frames. This one is inside a single
frame, and it exists because counting differing pixels could not answer the
question volumetric fog asks.

That question is whether the fog's colour follows the map's lighting. The
obvious check - photograph the scene with the effect off and on and count what
changed - passes just as happily when the effect reads ONE cell of the light
volume everywhere: every fogged pixel still changes, and the count and the
centre of mass look the same either way. Mutation-tested, and it passed. A check
that cannot fail is not a check.

What separates the two is a gradient. Put a bright half and a dim half in the
volume, look at two patches of frame at the same distance, and a working march
reports different colours for them while a constant one reports the same colour
twice. So this measures the MEAN of a channel in two boxes and asserts how far
apart they are - with an upper bound as well as a lower one, because the same
tool then states the control: with the effect off, those two patches have to
agree.

    tga_box_delta.py <frame.tga> x0,y0,x1,y1 x0,y0,x1,y1 CHANNEL [--min N] [--max N]

Boxes are fractions of the image, 0,0 top left. CHANNEL is r, g, b or l for
luminance. Targa stores blue, green, red; handled below.
"""

import struct
import sys

CHANNELS = {"b": 0, "g": 1, "r": 2, "l": -1}


def read_tga(path):
    with open(path, "rb") as handle:
        data = handle.read()

    idlen, cmaptype, imgtype = data[0], data[1], data[2]
    width, height = struct.unpack_from("<HH", data, 12)
    depth, descriptor = data[16], data[17]

    if cmaptype != 0 or imgtype != 2 or depth not in (24, 32):
        raise SystemExit(f"{path}: not an uncompressed true-colour targa")

    stride = depth // 8
    start = 18 + idlen
    pixels = data[start:start + width * height * stride]
    if len(pixels) < width * height * stride:
        raise SystemExit(f"{path}: truncated, {len(pixels)} bytes of pixel data")

    return width, height, stride, pixels, bool(descriptor & 0x20)


def box_mean(width, height, stride, pixels, top_down, box, channel):
    x0, y0, x1, y1 = (float(v) for v in box.split(","))
    total = 0.0
    count = 0

    for row in range(int(y0 * height), int(y1 * height)):
        src = row if top_down else height - 1 - row
        base = src * width * stride
        for col in range(int(x0 * width), int(x1 * width)):
            i = base + col * stride
            if channel < 0:
                # Rec. 601 luma, on the same byte order as everything else here.
                total += 0.114 * pixels[i] + 0.587 * pixels[i + 1] + 0.299 * pixels[i + 2]
            else:
                total += pixels[i + channel]
            count += 1

    if count == 0:
        raise SystemExit(f"the box {box} covers no pixels of a {width}x{height} frame")

    return total / count


def main():
    args = sys.argv[1:]
    if len(args) < 4:
        raise SystemExit(__doc__)

    path, box_a, box_b, name = args[0], args[1], args[2], args[3]
    lo = None
    hi = None

    i = 4
    while i < len(args):
        if args[i] == "--min" and i + 1 < len(args):
            lo = float(args[i + 1])
            i += 2
        elif args[i] == "--max" and i + 1 < len(args):
            hi = float(args[i + 1])
            i += 2
        else:
            raise SystemExit(f"unexpected argument: {args[i]}")

    if name not in CHANNELS:
        raise SystemExit(f"unknown channel {name}; known: r g b l")

    width, height, stride, pixels, top_down = read_tga(path)
    channel = CHANNELS[name]

    mean_a = box_mean(width, height, stride, pixels, top_down, box_a, channel)
    mean_b = box_mean(width, height, stride, pixels, top_down, box_b, channel)
    delta = abs(mean_a - mean_b)

    print(f"{path}: {name} is {mean_a:.1f} in {box_a} and {mean_b:.1f} in {box_b},"
          f" apart by {delta:.1f}")

    if lo is not None and delta < lo:
        print(f"  wanted at least {lo:.1f}. The two patches agree, so whatever was")
        print("  meant to vary between them did not.")
        return 1

    if hi is not None and delta > hi:
        print(f"  wanted at most {hi:.1f}. The two patches were supposed to agree.")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
