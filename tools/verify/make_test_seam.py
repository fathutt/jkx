#!/usr/bin/env python3
"""A model whose shading can be measured: still, lit, and carrying a seam.

Nothing on this bench could see a lighting change. Every model in the fixture
uses rgbGen const, which ignores normals entirely, so no change to a normal, a
light, or a shading term could alter a single pixel of any model here. That was
found the hard way: a weld of model normals was landed, measured, and reported
with a number that turned out to be the noise floor of an animated character,
while the one model that could have held still had a shader that could not react
to it either way.

So this generates the missing fixture, and it has three properties on purpose.

STILL. Two frames that are byte for byte the same, which looks like a mistake
and is not.

Still matters because the character in this fixture breathes on real time, which
puts twenty-six thousand pixels between two runs of the identical binary - see
the note by the character lane in smoke_headless.sh. A model that does not move
can be compared with itself.

Two frames rather than one decides which draw path the engine uses. From
tr_mesh.cpp, a model with numFrames <= 1 goes through the vertex buffer and
anything else through the batch. Both are worth having a fixture for, so this
generator writes either: --frames 1 for the buffer, two by default for the batch.

A CORRECTION lives here, because the first version of this comment asserted that
the buffer path does not light a model at all - RB_SurfaceVBOMDVMesh never fills
tess.normal, CalcColor in the shader has no case for lightingDiffuse, and a
one-frame model measured 255,94,94 across its whole face. Three things that fit,
and the conclusion was still wrong. The one-frame model that measured flat was
the EARLIER version of this fixture, whose normals all sat within twenty degrees
of the light: a cosine barely changes near zero, so it would have been nearly
flat down any path. Once the strip below pointed its normals around a circle, the
one-frame model came out with a range of a hundred and twenty-seven shades. It is
lit, through some route other than CalcColor, and a shader case added to "fix" it
changed the picture without being needed - which the mutation test caught by not
failing.

Recorded rather than deleted, because the mistake is the same one this whole file
exists to prevent: a fixture whose inputs are all alike cannot tell you which
input mattered.

LIT. Its shader is rgbGen lightingDiffuse, so the colour of a pixel is a
function of the normal there and of the light grid. That is the whole point: it
makes normals visible.

SEAMED, and not once. The model is a STRIP of quad pairs. Each pair shares a
middle edge whose two vertices are DUPLICATED - one copy per quad, with normals
turned apart by a fixed angle. That is what a texture cut looks like in a real
model, and closing it is what a weld does: unwelded, the two halves of a pair
are lit as if unrelated and there is a hard step down the join; welded, they
share one normal and the step is gone.

The strip exists because one pair is not enough, and that is a measurement
rather than caution. How big a shading step a given turn of the normal produces
depends on where the light is: the brightness follows the cosine of the angle
between the normal and the light, and the cosine barely changes near zero. The
first version of this model put both copies within twenty degrees of the light
direction and the whole seam was three units of green out of 255 - real, visible
to a subtraction, useless as a gate.

So the pairs step their base normal around the circle. Whatever direction the
light comes from, some pair straddles the steep part of the cosine and shows a
large step, and the check asks for the largest step anywhere on the model. That
makes the fixture independent of the light direction, of the grid's numbers, and
of whatever yaw testmodel happens to apply.

The two copies at each join are 40 degrees apart, inside the default weld
threshold of 60 and far outside anything that could be called rounding.

Usage:
    make_test_seam.py <out.md3> [--shader NAME] [--no-seam] [--frames N]
    make_test_seam.py --check
"""

import struct
import sys

MD3_IDENT = (ord('3') << 24) + (ord('P') << 16) + (ord('D') << 8) + ord('I')
MD3_VERSION = 15
MAX_QPATH = 64
MD3_XYZ_SCALE = 1.0 / 64

# The quad pair stands in the YZ plane, which is what a camera looking along +X
# sees square. testmodel puts a model a hundred units in front of the camera.
SIZE = 24.0

# How far apart the two copies of a shared vertex point, in degrees. Inside the
# default weld threshold, well outside rounding.
SEAM_DEGREES = 40.0


def qpath(name):
    data = name.encode("ascii")
    if len(data) >= MAX_QPATH:
        raise ValueError("name too long for MAX_QPATH: %s" % name)
    return data + b"\0" * (MAX_QPATH - len(data))


def name16(name):
    data = name.encode("ascii")
    if len(data) >= 16:
        raise ValueError("frame name too long: %s" % name)
    return data + b"\0" * (16 - len(data))


def packed_normal(degrees_from_x):
    """The MD3 two-byte normal, turned by an angle within the XY plane.

    Latitude in the high byte, longitude in the low, both as 255ths of a turn.
    The decoder reads z as cos(latitude), so latitude 64 - a quarter turn - puts
    the normal in the XY plane, and longitude then turns it within that plane
    away from +X.
    """
    lat = 64
    lng = int(round(degrees_from_x * 256.0 / 360.0)) & 0xFF
    return ((lat & 0xFF) << 8) | lng


def vertex(y, z, normal_degrees):
    return struct.pack("<3h", 0,
                       int(round(y / MD3_XYZ_SCALE)),
                       int(round(z / MD3_XYZ_SCALE))) + \
        struct.pack("<H", packed_normal(normal_degrees))


# How many quad pairs, and how far their base normals step around the circle
# between them. Six pairs at thirty degrees covers half a turn, which is enough:
# a normal and its opposite give the same size of step, in opposite directions.
SEAM_PAIRS = 6
SEAM_BASE_STEP = 30.0


def build(shader="textures/jkx/lit", seam=True, frames=2):
    """A strip of quad pairs, each with a duplicated, split middle edge.

    One pair, looking along +X, all in the YZ plane:

        a ---- c | e ---- g
        |      | | |      |
        b ---- d | f ---- h

    c,d and e,f are at the same two places and carry normals turned apart by
    SEAM_DEGREES. The pair's base normal is what both are turned away FROM, and
    it steps by SEAM_BASE_STEP from one pair to the next.
    """
    half = SEAM_DEGREES / 2.0
    verts = []
    tris = []
    st = []

    # The strip spans the same width the single pair used to, so the model still
    # fits in the frame testmodel puts it in.
    width = 2.0 * SIZE
    pair_width = width / SEAM_PAIRS

    for pair in range(SEAM_PAIRS):
        base = pair * SEAM_BASE_STEP
        left = base - half if seam else base
        right = base + half if seam else base

        # A wide gap between pairs, and the width is not cosmetic.
        #
        # The gap exists so the only places two vertices share are the joins
        # inside a pair: without it the last column of one pair sits on the
        # first column of the next with a base normal thirty degrees away, which
        # means the model built with --no-seam still has divergent normals and
        # the check that is supposed to reject it accepts it. A negative control
        # that cannot fail is not a control.
        #
        # It is wide because a narrow one is not a gap once anything smooths the
        # picture. With multisampling on - it is on by default now - the pixels
        # across a thin gap are blends of the two pairs either side, they still
        # read as model rather than background, and the measurement sees a step
        # between two pairs that were never meant to touch. That turned the gate
        # from 1 to 59 the first time multisampling was switched on, which is the
        # fixture being fragile rather than the engine being wrong.
        #
        # Two fifths of each pair's slot is empty. At the size testmodel draws
        # this, that is tens of pixels of background.
        y0 = -SIZE + pair * pair_width
        y1 = y0 + pair_width * 0.3
        y2 = y0 + pair_width * 0.6

        first = len(verts)
        verts += [
            (y0,  SIZE, base),      # a  outer top, left quad
            (y0, -SIZE, base),      # b  outer bottom, left quad
            (y1,  SIZE, left),      # c  middle top, left quad
            (y1, -SIZE, left),      # d  middle bottom, left quad
            (y1,  SIZE, right),     # e  middle top, right quad
            (y1, -SIZE, right),     # f  middle bottom, right quad
            (y2,  SIZE, base),      # g  outer top, right quad
            (y2, -SIZE, base),      # h  outer bottom, right quad
        ]
        # Wound the way this engine draws, which is the opposite of the order
        # the corners are listed in above.
        #
        # It mattered as soon as it was looked at: with `cull none` taken off
        # textures/jkx/lit, the model built the other way round vanished from
        # the frame entirely and the seam measurement had nothing to measure.
        # Every model in this fixture was inside out and none of it showed,
        # because every shader it uses was two-sided.
        #
        # Two-sided is no longer the arrangement here, and the reason is not
        # tidiness: a tangent basis is derived from the winding and the texture
        # coordinates together, so a reversed winding is a reversed handedness,
        # and this is the model the normal-mapping work will be measured on.
        tris += [
            (first + 0, first + 3, first + 1), (first + 0, first + 2, first + 3),
            (first + 4, first + 7, first + 5), (first + 4, first + 6, first + 7),
        ]
        # The duplicated pair differ in s, which is what makes them a texture
        # cut rather than a mistake.
        u0 = pair / float( SEAM_PAIRS )
        u1 = ( pair + 0.5 ) / float( SEAM_PAIRS )
        u2 = ( pair + 1.0 ) / float( SEAM_PAIRS )
        st += [
            (u0, 0.0), (u0, 1.0), (u1, 0.0), (u1, 1.0),
            (u1, 0.0), (u1, 1.0), (u2, 0.0), (u2, 1.0),
        ]

    num_verts = len(verts)
    num_tris = len(tris)

    # Two by default, and they are identical. See the note at the top: one frame
    # goes through the vertex buffer and two go through the batch, and for a
    # long time only the batch read normals. Nothing moves between these two.
    #
    # One frame is asked for on purpose by the lane that checks the buffer path
    # is lit at all, which is the defect this fixture found.
    num_frames = frames

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
    surf += qpath("seam")
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

    for tri in tris:
        surf += struct.pack("<3i", tri[0], tri[1], tri[2])

    for coord in st:
        surf += struct.pack("<2f", coord[0], coord[1])

    for _ in range(num_frames):
        for v in verts:
            surf += vertex(v[0], v[1], v[2])

    assert len(surf) == ofs_end

    header_size = 4 + 4 + MAX_QPATH + 4 * 9
    ofs_frames = header_size
    ofs_tags = ofs_frames + 56 * num_frames
    ofs_surfaces = ofs_tags                     # no tags
    file_end = ofs_surfaces + len(surf)

    radius = SIZE * 1.5

    out = struct.pack("<i", MD3_IDENT)
    out += struct.pack("<i", MD3_VERSION)
    out += qpath("seam")
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

    for _ in range(num_frames):
        out += struct.pack("<6f", -radius, -radius, -radius, radius, radius, radius)
        out += struct.pack("<3f", 0.0, 0.0, 0.0)
        out += struct.pack("<f", radius)
        out += name16("f")

    out += surf
    assert len(out) == file_end

    return out


def decode_normal(packed):
    """The engine's own decode, so the check measures what the engine will see."""
    import math

    lat = ((packed >> 8) & 0xFF) * 2.0 * math.pi / 256.0
    lng = (packed & 0xFF) * 2.0 * math.pi / 256.0
    return (math.cos(lat) * math.sin(lng),
            math.sin(lat) * math.sin(lng),
            math.cos(lng))


def check(data):
    """Read the model back and prove it has the seam it claims to have.

    The assertion that matters is the angle between the two copies of a shared
    point. A generator that wrote the same normal twice would produce a model
    that looks perfectly reasonable and measures nothing - which is exactly the
    failure this whole fixture exists to avoid repeating.
    """
    import math

    ident, version = struct.unpack_from("<2i", data, 0)
    if ident != MD3_IDENT or version != MD3_VERSION:
        raise ValueError("not an MD3")

    num_frames, num_tags, num_surfaces = struct.unpack_from("<3i", data, 8 + MAX_QPATH + 4)
    if num_frames not in (1, 2):
        raise ValueError("this model wants one frame or two, not %d - see the note "
                         "at the top about which draw path reads normals" % num_frames)

    ofs_surfaces = struct.unpack_from("<i", data, 8 + MAX_QPATH + 4 + 4 * 5)[0]
    s = ofs_surfaces

    s_frames, _, s_verts = struct.unpack_from("<3i", data, s + 4 + MAX_QPATH + 4)
    ofs_xyz = struct.unpack_from("<i", data, s + 4 + MAX_QPATH + 4 + 4 * 7)[0]

    # And the frames really are identical, or "still" is a claim rather than a
    # property.
    frame_size = 8 * s_verts
    if num_frames == 2 and data[s + ofs_xyz:s + ofs_xyz + frame_size] != \
            data[s + ofs_xyz + frame_size:s + ofs_xyz + 2 * frame_size]:
        raise ValueError("the two frames differ, so the model does not hold still")

    places = {}
    for i in range(s_verts):
        x, y, z, packed = struct.unpack_from("<3hH", data, s + ofs_xyz + 8 * i)
        places.setdefault((x, y, z), []).append(decode_normal(packed))

    shared = [n for n in places.values() if len(n) > 1]
    if not shared:
        raise ValueError("no duplicated vertex, so this fixture measures nothing")

    worst = 0.0
    for group in shared:
        for a in range(len(group)):
            for b in range(a + 1, len(group)):
                dot = sum(p * q for p, q in zip(group[a], group[b]))
                dot = max(-1.0, min(1.0, dot))
                worst = max(worst, math.degrees(math.acos(dot)))

    if worst < 20.0:
        raise ValueError("the duplicated normals are only %.1f degrees apart, "
                         "which is not a seam anyone would see" % worst)
    if worst >= 60.0:
        raise ValueError("the duplicated normals are %.1f degrees apart, which is "
                         "outside the default weld threshold - the fixture would "
                         "prove the weld does nothing" % worst)

    print("ok   %d bytes, %d frame(s), %d vert(s), %d shared place(s), "
          "normals up to %.1f degrees apart"
          % (len(data), num_frames, s_verts, len(shared), worst))


def main():
    argv = sys.argv[1:]

    if argv and argv[0] == "--check":
        check(build())
        flat = build(seam=False)
        try:
            check(flat)
        except ValueError:
            print("ok   and a model built without the seam is rejected by the same check")
            return 0
        print("error: the check passed a model with no seam in it", file=sys.stderr)
        return 1

    if not argv:
        print(__doc__.strip().splitlines()[-4].strip(), file=sys.stderr)
        return 2

    out = argv[0]
    shader = "textures/jkx/lit"
    seam = True
    frames = 2

    i = 1
    while i < len(argv):
        if argv[i] == "--shader" and i + 1 < len(argv):
            shader = argv[i + 1]
            i += 2
            continue
        if argv[i] == "--frames" and i + 1 < len(argv):
            frames = int(argv[i + 1])
            i += 2
            continue
        if argv[i] == "--no-seam":
            seam = False
            i += 1
            continue
        print("unknown argument: %s" % argv[i], file=sys.stderr)
        return 2

    data = build(shader, seam, frames)
    with open(out, "wb") as f:
        f.write(data)

    print("%s: %d bytes, %d frame(s), shader %s, seam %s"
          % (out, len(data), frames, shader, "yes" if seam else "no"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
