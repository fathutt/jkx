#!/usr/bin/env python3
"""Is the screen wipe's boundary a ramp, or a step?

The wipe used to be drawn with an alpha test: the boundary picture was compared
against a threshold, so every pixel was either the old screen or the new one and
the only softness available was the dither pattern baked into that picture.
Stretched from 64 texels across a 21:9 screen, that pattern is what the edge
looked like. It is geometry with an alpha ramp now, and the question this asks is
whether the ramp reached the screen.

    tga_soft_edge.py <min run> <shot.tga> [shot.tga ...]

It reads the middle row of each frame and finds the longest run of pixels whose
value only ever moves one way. That is the measurement, rather than a count of
distinct values, because a count cannot tell a ramp from a picture: the first
attempt counted sixty-three values in a frame and passed - and passed just as
happily with the ramp quantised back to a step, because what it was counting was
the menu underneath.

Two things this cannot do for itself, and the caller has to arrange:

  - The frame must be a wipe between two DIFFERENT pictures. A wipe blends the
    screen it captured against the screen being drawn now, and blending two
    copies of one picture gives that picture at every alpha. Held over a grey
    load screen arriving at a grey room, the wipe measured one pixel - correctly,
    because there was nothing in the frame - while the same build measured a
    hundred and twenty-nine over a coloured one.
  - The band has to cross the row this reads. A left-to-right wipe crosses every
    row; a top-to-bottom one crosses this row only while it is halfway down.

A ramp is long and monotonic: a tenth of the screen, changing by a step or two
per pixel and never turning round. Menu text and edges give runs a few pixels
long. A step gives none at all.

The check passes if ANY of the frames clears the bar, because when a wipe is on
screen is a question of frame rate - it runs for three quarters of a second
measured in milliseconds, and the caller counts frames. Several frames near the
start of the wipe, and one of them will be inside it.

The value read is the sum of the three channels rather than one of them. A blend
between two flat colours is linear in every channel at once, so the sum is a ramp
whenever any channel is - and it has three times the resolution. One channel is
not enough: the two games' load screens are different colours, and against a
green sky the red channel of a black-to-green wipe is zero the whole way across.
That measured as a flat row and reported a step, in the game whose wipe was
working.
"""

import struct
import sys


# How many pixels of no change a run may carry before it is two runs. The wipe's
# band is a tenth of the screen and the two things it blends differ by most of
# the range, so its own flat stretches are a pixel or two; anything longer than
# this is scenery, and bridging it joins ramps that have nothing to do with each
# other.
MAX_FLAT = 4


def longest_monotonic_run(path):
    with open(path, "rb") as handle:
        data = handle.read()

    idlen, cmaptype, imgtype = data[0], data[1], data[2]
    width, height = struct.unpack_from("<HH", data, 12)
    depth, descriptor = data[16], data[17]

    if cmaptype != 0 or imgtype != 2 or depth not in (24, 32):
        raise SystemExit(f"{path}: not an uncompressed true-colour targa")

    stride = depth // 8
    start = 18 + idlen
    top_down = bool(descriptor & 0x20)

    row = height // 2
    row = row if top_down else height - 1 - row
    base = start + row * width * stride

    row_values = [ data[base + col * stride + 0]
                 + data[base + col * stride + 1]
                 + data[base + col * stride + 2] for col in range(width) ]

    best = 0
    run = 1
    flat = 0
    direction = 0

    for i in range(1, len(row_values)):
        delta = row_values[i] - row_values[i - 1]

        if delta == 0:
            # A short flat stretch extends a ramp rather than breaking one: the
            # ramp is quantised to bytes, so a gentle one repeats values as it
            # goes. A long one is a wall, a floor or a sky, and joining two
            # unrelated ramps across it is how a flat frame measures as a ramp.
            flat += 1
            if flat > MAX_FLAT:
                run = 1
                direction = 0
            else:
                run += 1
            continue

        flat = 0
        step = 1 if delta > 0 else -1
        if step == direction:
            run += 1
        else:
            direction = step
            run = 2

        if run > best:
            best = run

    return best


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)

    want = int(sys.argv[1])
    best = 0
    best_path = None

    for path in sys.argv[2:]:
        try:
            run = longest_monotonic_run(path)
        except FileNotFoundError:
            continue
        print(f"{path}: longest one-way run is {run} pixel(s)")
        # best_path marks "a frame was read", so it is set on the first readable
        # one rather than on the first improvement. A frame measuring zero used
        # to leave it unset and report the file as missing, which sent the reader
        # looking for a screenshot that was sitting right there.
        if best_path is None or run > best:
            best, best_path = run, path

    if best_path is None:
        print("none of those frames exist")
        return 1

    if best < want:
        print(f"the longest one-way run in those frames is {best} pixel(s), "
              f"wanted at least {want}")
        print("  A boundary that is one pixel wide has no run: every pixel either")
        print("  side of it is one screen or the other. That is what an alpha test")
        print("  produces, and what the alpha ramp replaced.")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
