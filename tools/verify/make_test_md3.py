#!/usr/bin/env python3
"""A two-frame MD3, so that vertex animation is something the bench can see.

Everything else in this fixture is one frame, which is why nobody noticed that
vertex animation had stopped working: the vertex buffer built for an MD3 is
sized numVerts, not numVerts * numFrames, so every model was drawn at frame 0
and every model in the fixture only had one.

This writes the smallest model that can prove the difference: one square, two
frames, and the square is somewhere else in the second one. Held at frame 0 it
is on the left of where it was placed; at frame 1 it is on the right. A build
that ignores frames draws the same picture twice, and the check beside it in
smoke_headless.sh is that the two pictures differ.

The format, from qfiles.h, and the two parts that are easy to get wrong:

  - every ofs* in a surface is measured from the start of THAT SURFACE, not
    from the start of the file, while every ofs* in the header is from the
    start of the file;
  - positions are shorts in units of 1/64, so the range is about +/-512 and a
    model larger than that silently wraps.

Usage:
    make_test_md3.py <out.md3> [--check]
"""

import struct
import sys

MD3_IDENT = (ord('3') << 24) + (ord('P') << 16) + (ord('D') << 8) + ord('I')
MD3_VERSION = 15
MD3_XYZ_SCALE = 1.0 / 64

MAX_QPATH = 64

# How far the square moves between the two frames, in world units. Large enough
# that no filtering or resolution can make the two frames look alike.
SHIFT = 24.0

# The square, in the plane the bench looks at.
SIZE = 16.0


def qpath(name):
    raw = name.encode("ascii")
    if len(raw) >= MAX_QPATH:
        raise ValueError("name too long: %s" % name)
    return raw + b"\0" * (MAX_QPATH - len(raw))


def name16(name):
    raw = name.encode("ascii")
    if len(raw) >= 16:
        raise ValueError("frame name too long: %s" % name)
    return raw + b"\0" * (16 - len(raw))


def frame(radius):
    """md3Frame_t: bounds, localOrigin, radius, name. 56 bytes."""
    out = struct.pack("<6f", -radius, -radius, -radius, radius, radius, radius)
    out += struct.pack("<3f", 0.0, 0.0, 0.0)
    out += struct.pack("<f", radius)
    out += name16("f")
    return out


def xyz_normal(x, y, z):
    """Position in 1/64 units, plus a packed normal pointing at +X.

    The normal is two bytes: latitude in the high byte, longitude in the low,
    both as 255ths of a turn. +X is lat 0, lng 0 - the encoding puts z along
    cos(lat), so lat 64 (a quarter turn) is the XY plane and lng 0 is +X.
    """
    lat = 64
    lng = 0
    packed = ((lat & 0xFF) << 8) | (lng & 0xFF)
    return struct.pack("<3h", int(round(x / MD3_XYZ_SCALE)),
                       int(round(y / MD3_XYZ_SCALE)),
                       int(round(z / MD3_XYZ_SCALE))) + struct.pack("<H", packed)


def build(shader="textures/jkx/anim"):
    num_frames = 2
    num_verts = 4
    num_tris = 2

    # The surface, whose offsets are all relative to its own start.
    surf_header_size = 4 + MAX_QPATH + 4 * 10
    shaders_size = (MAX_QPATH + 4) * 1
    tris_size = 12 * num_tris
    st_size = 8 * num_verts
    xyz_size = 8 * num_verts * num_frames

    ofs_shaders = surf_header_size
    ofs_triangles = ofs_shaders + shaders_size
    ofs_st = ofs_triangles + tris_size
    ofs_xyz = ofs_st + st_size
    ofs_end = ofs_xyz + xyz_size

    surf = struct.pack("<i", MD3_IDENT)
    surf += qpath("anim")
    surf += struct.pack("<i", 0)                # flags
    surf += struct.pack("<i", num_frames)
    surf += struct.pack("<i", 1)                # numShaders
    surf += struct.pack("<i", num_verts)
    surf += struct.pack("<i", num_tris)
    surf += struct.pack("<i", ofs_triangles)
    surf += struct.pack("<i", ofs_shaders)
    surf += struct.pack("<i", ofs_st)
    surf += struct.pack("<i", ofs_xyz)
    surf += struct.pack("<i", ofs_end)
    assert len(surf) == surf_header_size

    surf += qpath(shader) + struct.pack("<i", 0)

    # Two triangles, wound so the +X normal above faces the camera.
    surf += struct.pack("<3i", 0, 1, 2)
    surf += struct.pack("<3i", 0, 2, 3)

    for st in ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)):
        surf += struct.pack("<2f", st[0], st[1])

    # Frame 0 to one side, frame 1 to the other. The square stands in the YZ
    # plane and moves along Y, which is across the screen for a camera looking
    # down +X.
    for f in range(num_frames):
        dy = -SHIFT if f == 0 else SHIFT
        for corner in ((-SIZE, -SIZE), (SIZE, -SIZE), (SIZE, SIZE), (-SIZE, SIZE)):
            surf += xyz_normal(0.0, dy + corner[0], corner[1])

    assert len(surf) == ofs_end

    header_size = 4 + 4 + MAX_QPATH + 4 * 9
    ofs_frames = header_size
    ofs_tags = ofs_frames + 56 * num_frames
    ofs_surfaces = ofs_tags                     # no tags
    file_end = ofs_surfaces + len(surf)

    out = struct.pack("<i", MD3_IDENT)
    out += struct.pack("<i", MD3_VERSION)
    out += qpath("models/jkx/anim.md3")
    out += struct.pack("<i", 0)                 # flags
    out += struct.pack("<i", num_frames)
    out += struct.pack("<i", 0)                 # numTags
    out += struct.pack("<i", 1)                 # numSurfaces
    out += struct.pack("<i", 0)                 # numSkins
    out += struct.pack("<i", ofs_frames)
    out += struct.pack("<i", ofs_tags)
    out += struct.pack("<i", ofs_surfaces)
    out += struct.pack("<i", file_end)
    assert len(out) == header_size

    radius = SHIFT + SIZE * 1.5
    for _ in range(num_frames):
        out += frame(radius)

    out += surf
    assert len(out) == file_end
    return out


def check(data):
    """Read it back the way the engine does, and say what it found."""
    ident, version = struct.unpack_from("<2i", data, 0)
    if ident != MD3_IDENT:
        raise SystemExit("bad ident")
    if version != MD3_VERSION:
        raise SystemExit("bad version")

    num_frames, num_tags, num_surfaces = struct.unpack_from("<3i", data, 4 + 4 + MAX_QPATH + 4)
    ofs_surfaces = struct.unpack_from("<i", data, 4 + 4 + MAX_QPATH + 4 * 7)[0]

    if num_frames != 2:
        raise SystemExit("a fixture with one frame proves nothing")

    s = ofs_surfaces
    s_frames, _, s_verts = struct.unpack_from("<3i", data, s + 4 + MAX_QPATH + 4)
    ofs_xyz = struct.unpack_from("<i", data, s + 4 + MAX_QPATH + 4 * 8)[0]

    if s_frames != num_frames:
        raise SystemExit("surface and model disagree about the frame count")

    # The whole point: the two frames have to hold different positions.
    a = struct.unpack_from("<3h", data, s + ofs_xyz)
    b = struct.unpack_from("<3h", data, s + ofs_xyz + 8 * s_verts)
    if a == b:
        raise SystemExit("the two frames are identical, so the fixture cannot "
                         "tell a working build from a broken one")

    print("ok   %d bytes, %d frame(s), %d vert(s), first vertex moves %s -> %s"
          % (len(data), num_frames, s_verts, a, b))
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    data = build()

    if "--check" in sys.argv:
        return check(data)

    if not args:
        raise SystemExit(__doc__)

    with open(args[0], "wb") as handle:
        handle.write(data)
    print("wrote %s, %d bytes" % (args[0], len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
