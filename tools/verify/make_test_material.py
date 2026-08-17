#!/usr/bin/env python3
"""Write the two textures a physically-based material needs, so that one exists.

This bench ran the physically-based renderer for weeks without ever drawing a
single physically-based DRAW. The lane was there, the pipeline was built, the
shader compiled - and none of it mattered, because no material in the fixture
named a normal map. A shader permutation is generated from what a material asks
for, so the permutation that reads the normal map, the physical map and the
lighting environment was never generated, and the code path that binds them for
it was never taken.

What that hid: the DrawItem path in tr_backend.cpp bound descriptor sets zero to
four and never pushed set five, which is the set those three textures live in.
Any Ghoul2 mesh with a normal map ran a pipeline that statically uses set five
against nothing. On lavapipe that is a segmentation fault in a rasteriser worker
thread; on hardware it is whatever the driver does with an unbound set. It took
a retail model with real maps to produce the first such draw, and this file is
what makes the bench able to produce one on its own.

    make_test_material.py <directory>
    make_test_material.py --check

Two files, both 64 by 64, both flat:

  jkx_flat_n.tga    a normal map that says "straight out" everywhere - 128, 128,
                    255, which unpacks to ( 0, 0, 1 ). Flat on purpose: a normal
                    map with a pattern in it would change the shading, and the
                    question this fixture asks is not what the shading is, it is
                    whether the draw happens at all. A pattern belongs in a lane
                    about tangents, and that lane wants a model with more than
                    one triangle facing more than one way.

  jkx_flat_rmo.tga  roughness, metalness and occlusion in red, green and blue.
                    Mid roughness, no metal, no occlusion - 128, 0, 255. Not all
                    zero and not all 255, so a channel read in the wrong order
                    produces a different picture rather than the same one.

Targa stores blue, green, red, and getting that backwards here would swap
roughness with occlusion silently, which is why the two values differ.
"""

import os
import struct
import sys

SIZE = 64

# ( 0, 0, 1 ) packed the way a normal map packs it: n * 0.5 + 0.5, times 255.
FLAT_NORMAL = (128, 128, 255)

# Roughness in red, metalness in green, occlusion in blue. See the docstring for
# why none of the three is equal to another.
FLAT_RMO = (128, 0, 255)


def write_tga(path, colour):
    """Uncompressed 24-bit Targa, bottom-up, one flat colour."""
    header = struct.pack("<3B2HB4H2B",
                         0,         # no id field
                         0,         # no colour map
                         2,         # uncompressed true colour
                         0, 0, 0,   # colour map spec
                         0, 0, SIZE, SIZE,
                         24,        # bits per pixel
                         0)         # descriptor: bottom-up

    r, g, b = colour
    body = bytes((b, g, r)) * (SIZE * SIZE)

    with open(path, "wb") as f:
        f.write(header)
        f.write(body)


def write_half_tga(path, colour):
    """The same Targa, half black and half one colour, split down the middle.

    A texture with an EDGE in it, which the flat ones above deliberately are
    not. tcMod moves texture coordinates, and moving them across a flat texture
    changes nothing at all - so a fixture made of flat textures cannot see a
    single one of the six tcMod kinds, whatever it does with them.

    Half and half rather than a checkerboard, because the check is a count: how
    many pixels of the colour are on the square. A checkerboard of that colour
    keeps roughly half of them wherever it is scrolled to; one edge moves the
    count in proportion to how far the coordinates moved, which is a number
    rather than a coincidence.

    The u coordinate is what splits, so a scroll along u is the one that changes
    it most and a scroll along v changes it not at all. That is a property worth
    having: it means the lane can tell the two apart.
    """
    header = struct.pack("<3B2HB4H2B",
                         0, 0, 2,
                         0, 0, 0,
                         0, 0, SIZE, SIZE,
                         24, 0)

    r, g, b = colour
    row = bytes((0, 0, 0)) * (SIZE // 2) + bytes((b, g, r)) * (SIZE - SIZE // 2)

    with open(path, "wb") as f:
        f.write(header)
        f.write(row * SIZE)


def write_corner_tga(path, colour):
    """One quarter of the texture coloured, in a corner, off centre.

    For the ROTATING square, and it replaces a diagonal split that replaced a
    straight one. Both of those were wrong for the same reason and it took two
    runs and a red CI to see it: rotation is about the middle of the texture, and
    any region bounded by a line THROUGH the middle covers about half the square
    at every angle. Its pixel count barely moves - measured, a spread of eight
    and thirteen locally and two under load, below its own gate.

    A corner block is off centre, so rotation swings it around and what moves is
    not how much colour there is but WHERE it is. That is what the lane measures
    for this one, and the centroid of a block orbiting the middle moves a great
    deal for a rotation a count would hardly notice.
    """
    header = struct.pack("<3B2HB4H2B",
                         0, 0, 2,
                         0, 0, 0,
                         0, 0, SIZE, SIZE,
                         24, 0)

    r, g, b = colour
    body = bytearray()
    for v in range(SIZE):
        for u in range(SIZE):
            if u >= SIZE // 2 and v >= SIZE // 2:
                body += bytes((b, g, r))
            else:
                body += bytes((0, 0, 0))

    with open(path, "wb") as f:
        f.write(header)
        f.write(bytes(body))


def write_diagonal_tga(path, colour):
    """The same Targa, split on the DIAGONAL rather than down the middle.

    For the rotating square, and the reason is that a straight split is nearly
    invariant under rotation: turn a half-and-half texture about its centre and
    about half of it is still coloured, so the count barely moves - measured, a
    spread of two pixels against a control of zero, which is a signal but a thin
    one. A diagonal edge sweeps a corner of the square in and out as it turns and
    moves the count by an order of magnitude more.
    """
    header = struct.pack("<3B2HB4H2B",
                         0, 0, 2,
                         0, 0, 0,
                         0, 0, SIZE, SIZE,
                         24, 0)

    r, g, b = colour
    body = bytearray()
    for v in range(SIZE):
        for u in range(SIZE):
            if u + v > SIZE:
                body += bytes((b, g, r))
            else:
                body += bytes((0, 0, 0))

    with open(path, "wb") as f:
        f.write(header)
        f.write(bytes(body))


# The colours the texture-coordinate lane paints its squares in. One per
# material, all different from each other and from everything else this fixture
# draws, so a count of one colour is a count of one square and needs no mask.
#
# NONE of them is red-dominant, and that is not taste. The seam check three
# hundred lines further on in smoke_headless.sh reads the red-dominant pixels of
# the frame - the fixture's light grid is red on purpose - so a red square
# standing in the same picture is measured as part of the model and reported as
# a shading step across a seam. It was, on the first run of this lane.
TC_COLOURS = (
    ("ref", (0, 255, 128)),
    ("scroll", (0, 128, 255)),
    ("rotate", (128, 0, 255)),
    ("stretch", (0, 200, 100)),
    ("scale", (100, 0, 200)),
)


def build_tc(directory):
    os.makedirs(directory, exist_ok=True)
    written = []
    for name, colour in TC_COLOURS:
        path = os.path.join(directory, "jkx_tc_%s.tga" % name)
        if name == "rotate":
            write_corner_tga(path, colour)
        else:
            write_half_tga(path, colour)
        written.append(path)
    return written


def build(directory):
    os.makedirs(directory, exist_ok=True)
    normal = os.path.join(directory, "jkx_flat_n.tga")
    rmo = os.path.join(directory, "jkx_flat_rmo.tga")
    write_tga(normal, FLAT_NORMAL)
    write_tga(rmo, FLAT_RMO)
    return normal, rmo


def check():
    """The header the loader will read, and the byte order inside it.

    R_LoadTGA refuses a file whose header disagrees with its length, and a
    generator that writes the size or the depth wrong produces a texture that
    either fails to load - which this lane would notice - or loads at the wrong
    stride, which it would not.
    """
    import tempfile

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        for path, colour in zip(build(tmp), (FLAT_NORMAL, FLAT_RMO)):
            data = open(path, "rb").read()
            expected = 18 + SIZE * SIZE * 3

            if len(data) != expected:
                failures.append("%s is %d bytes, not %d"
                                % (os.path.basename(path), len(data), expected))
                continue

            if data[2] != 2 or data[16] != 24:
                failures.append("%s is not an uncompressed 24-bit Targa"
                                % os.path.basename(path))

            width, height = struct.unpack_from("<HH", data, 12)
            if (width, height) != (SIZE, SIZE):
                failures.append("%s says it is %dx%d"
                                % (os.path.basename(path), width, height))

            # Blue, green, red on disk. If this reads back as the colour that
            # was asked for, the order is right; if it reads back reversed, a
            # normal map would point sideways and nobody would see why.
            r, g, b = colour
            if bytes(data[18:21]) != bytes((b, g, r)):
                failures.append("%s: first texel is %s, wanted %s in BGR"
                                % (os.path.basename(path),
                                   tuple(data[18:21]), (b, g, r)))

    for f in failures:
        print("error: %s" % f, file=sys.stderr)
    if failures:
        return 1

    print("material textures: %dx%d, normal %s, rmo %s, byte order checked"
          % (SIZE, SIZE, FLAT_NORMAL, FLAT_RMO))
    return 0


def main(argv):
    args = argv[1:]

    if "--check" in args:
        return check()

    if "--tc" in args:
        args = [a for a in args if a != "--tc"]
        if len(args) != 1:
            print("usage: %s --tc <directory>" % argv[0], file=sys.stderr)
            return 2
        written = build_tc(args[0])
        print("%d half-and-half texture(s) in %s, %dx%d"
              % (len(written), args[0], SIZE, SIZE))
        return 0

    if len(args) != 1:
        print("usage: %s <directory> | %s --tc <directory>"
              % (argv[0], argv[0]), file=sys.stderr)
        return 2

    normal, rmo = build(args[0])
    print("%s and %s: %dx%d" % (normal, rmo, SIZE, SIZE))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
