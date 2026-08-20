#!/usr/bin/env python3
"""Where two frames differ, not how much.

ab_frames.py asks whether two runs drew the same picture, which is the question
for a change that is supposed to be invisible. This asks the opposite question
about one thing that is supposed to be visible: something was drawn in the
second frame and not the first, and it belongs in a particular place.

The reason it exists is the crosshair. It was placed at half of 640 in a space
that is as wide as the window is at 480 units tall - 1707 units on 21:9 - so it
sat a fifth of the way in from the left instead of at the centre. On 4:3 the two
numbers are the same and the defect is invisible, which is why it survived thirty
years. No colour check can find it: the crosshair is white and so is half the
fixture. What identifies it is that it is the only thing that changed between a
frame drawn with it and a frame drawn without.

    tga_diff_where.py <without.tga> <with.tga> x0,y0,x1,y1 [min pixels] [max]
                      [--threshold N]

--threshold is how far apart a channel has to be before a pixel counts as
changed, and the default of forty is right for anything that REPLACES what was
there. It is wrong for a small multiplier: the caustic web under a liquid
surface shifts most of its pixels by ten to thirty, so counting at forty counts
its tail rather than the effect, and the same code on two machines then reports
four thousand pixels and eight hundred. Lower it for effects like that, and keep
the box: what stops a loose threshold from passing on noise is that the noise is
nowhere in particular.

The box is in fractions of the image, 0,0 top left. The count and the centre of
mass both have to be right: too few pixels means it was not drawn at all, too
many means it is the wrong shape - a disc covers pi/4 of the square that bounds
it, so a square drawn where a disc belongs is a quarter too big - and a centre
outside the box means it was drawn somewhere else.

Targa stores blue, green, red. Handled below.
"""

import struct
import sys

THRESHOLD = 40


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

    top_down = bool(descriptor & 0x20)
    return width, height, stride, pixels, top_down


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)

    args = sys.argv[1:]
    threshold = THRESHOLD
    if "--threshold" in args:
        at = args.index("--threshold")
        threshold = int(args[at + 1])
        del args[at:at + 2]

    without, with_it, box = args[0], args[1], args[2]
    want = int(args[3]) if len(args) > 3 else 100
    limit = int(args[4]) if len(args) > 4 else 0

    x0, y0, x1, y1 = (float(v) for v in box.split(","))

    aw, ah, astride, apix, atop = read_tga(without)
    bw, bh, bstride, bpix, btop = read_tga(with_it)

    if (aw, ah) != (bw, bh):
        raise SystemExit(f"{without} is {aw}x{ah} and {with_it} is {bw}x{bh}")

    count = 0
    sx = 0.0
    sy = 0.0

    for row in range(ah):
        arow = row if atop else ah - 1 - row
        brow = row if btop else bh - 1 - row
        abase = arow * aw * astride
        bbase = brow * bw * bstride
        for col in range(aw):
            ai = abase + col * astride
            bi = bbase + col * bstride
            if (abs(apix[ai] - bpix[bi]) > threshold
                    or abs(apix[ai + 1] - bpix[bi + 1]) > threshold
                    or abs(apix[ai + 2] - bpix[bi + 2]) > threshold):
                count += 1
                sx += col
                sy += row

    if count < want:
        print(f"{with_it}: {count} pixel(s) differ from {without}, wanted at least {want}")
        print("  Nothing was drawn, or it was drawn in the same colour as what is behind it.")
        return 1

    if limit and count > limit:
        print(f"{with_it}: {count} pixel(s) differ from {without}, wanted at most {limit}")
        print("  It was drawn, in the right place, and it is too big - which for a shape")
        print("  with a known area means it is the wrong shape.")
        return 1

    cx = sx / count / aw
    cy = sy / count / ah

    inside = x0 <= cx <= x1 and y0 <= cy <= y1
    print(f"{with_it}: {count} pixel(s) differ, centre {cx:.3f},{cy:.3f}"
          f" (wanted inside {x0},{y0}-{x1},{y1})")

    if not inside:
        print("  The difference is real but it is somewhere else. On a window that is not")
        print("  4:3 this is what a coordinate computed in one space and drawn in another")
        print("  looks like: the right picture, at the centre of a rectangle that is not")
        print("  the one on screen.")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
