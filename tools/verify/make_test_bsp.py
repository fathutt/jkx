#!/usr/bin/env python3
"""Write a map, so that the headless run can load one.

The three crashes the first real-hardware session found all lived between
Hunk_Clear wiping the renderer and RE_BeginRegistration building it again, and
the thing that fires inside that window is the game registering the models of a
map's entities. Nothing here could reach it, because nothing here has a map:
retail BSPs are the game's, and this repository holds none of the game's data
(docs/Backlog.md, section 10).

So this writes one. Not a level - a room: one box brush, one visible quad, one
worldspawn and one player start. Enough for CM_LoadMap to accept, for
R_LoadWorldMap to walk, and for the server to spawn on. It is about six hundred
bytes and it is generated rather than committed, the same way the font atlases
are, so that what it contains is readable as code instead of as a hex dump.

The format is Raven's RBSP, version 1 - eighteen lumps rather than Quake 3's
seventeen, four lightmap styles per surface rather than one. The structures are
in code/qcommon/qfiles.h and the reader that has to accept this is split between
code/qcommon/cm_load.cpp (collision) and code/rd-vulkan/tr_bsp.cpp (drawing);
both were read while writing this, and where a field has a value that is not
obviously right, the comment says which of the two demanded it.

    make_test_bsp.py <out.bsp> [--shader NAME] [--sky NAME]
    make_test_bsp.py --check

--sky adds a second drawn surface: a wall across the far end of the room
carrying the named shader. Give it a shader with skyParms and the room has a
sky, which is the only way anything here can exercise the sky path at all.

--check writes nothing and verifies the directory it would write: every lump a
whole number of its own records, every offset inside the file, the header the
length the engine expects. A generator that emits a subtly wrong stride does not
fail loudly - it produces a map that loads and draws in the wrong place - so it
is worth checking the arithmetic separately from running the engine.
"""

import struct
import sys

BSP_IDENT = b"RBSP"
BSP_VERSION = 1
HEADER_LUMPS = 18
MAX_QPATH = 64
MAXLIGHTMAPS = 4

(LUMP_ENTITIES, LUMP_SHADERS, LUMP_PLANES, LUMP_NODES, LUMP_LEAFS,
 LUMP_LEAFSURFACES, LUMP_LEAFBRUSHES, LUMP_MODELS, LUMP_BRUSHES,
 LUMP_BRUSHSIDES, LUMP_DRAWVERTS, LUMP_DRAWINDEXES, LUMP_FOGS, LUMP_SURFACES,
 LUMP_LIGHTMAPS, LUMP_LIGHTGRID, LUMP_VISIBILITY, LUMP_LIGHTARRAY) = range(18)

MST_PLANAR = 1
LIGHTMAP_BY_VERTEX = -3     # tr_local.h: pre-lit, no lightmap page needed
LS_NORMAL = 0
LS_NONE = 0xFF

# The room. Small enough that the light grid over it is a handful of points.
HALF = 256.0
FLOOR_Z = -64.0

CONTENTS_SOLID = 1
SURF_NODAMAGE = 0x1


def qpath(name):
    b = name.encode("ascii")
    if len(b) >= MAX_QPATH:
        raise ValueError("shader name too long: %s" % name)
    return b + b"\0" * (MAX_QPATH - len(b))


def shaders(visible, sky=None):
    """One the brush sides carry, one the floor carries, one the sky wall.

    CMod_LoadBrushes and CMod_LoadBrushSides both range-check shaderNum against
    this lump, so the count here is not free - it is the bound they check.
    """
    out = b""
    out += qpath("textures/jkx/solid") + struct.pack("<ii", 0, CONTENTS_SOLID)
    out += qpath(visible) + struct.pack("<ii", SURF_NODAMAGE, CONTENTS_SOLID)
    if sky:
        out += qpath(sky) + struct.pack("<ii", SURF_NODAMAGE, CONTENTS_SOLID)
    return out


# The six box faces, in the order planes() writes them. Each is followed by its
# opposite, so the plane for entry n is at 2n and its mirror at 2n+1 - the
# format's "planes x^1 is always the opposite of plane x".
#
# Named, because they were not, and the cost of that was the whole fixture
# drawing nothing: the node below asked for plane 8, its comment said "the floor
# plane", and plane 8 is the CEILING. Everything downstream was consistent with
# a camera standing in the solid leaf, which is a state the engine handles
# quietly - no PVS, no surfaces, a view of the clear colour - and the screenshot
# check passed on the head-up display drawn over it.
PLANE_X_POS = 0     # ( 1  0  0)  x <=  256
PLANE_X_NEG = 2     # (-1  0  0)  x >= -256
PLANE_Y_POS = 4
PLANE_Y_NEG = 6
PLANE_CEILING = 8   # ( 0  0  1)  z <=  256
PLANE_FLOOR = 10    # ( 0  0 -1)  z >= FLOOR_Z, pointing DOWN
PLANE_FLOOR_UP = 11 # its opposite: ( 0 0 1) at FLOOR_Z, pointing UP


def planes():
    """Six box faces, each followed by its opposite.

    "planes x^1 is always the opposite of plane x" - qfiles.h. The invariant is
    the format's and cheap to hold to, and the tree uses both halves of the last
    pair: the brush wants the face pointing out of the room, the node wants the
    one pointing into it.
    """
    out = b""
    for normal, dist in (
        ((1, 0, 0), HALF), ((-1, 0, 0), HALF),
        ((0, 1, 0), HALF), ((0, -1, 0), HALF),
        ((0, 0, 1), HALF), ((0, 0, -1), -FLOOR_Z),
    ):
        out += struct.pack("<4f", normal[0], normal[1], normal[2], dist)
        out += struct.pack("<4f", -normal[0], -normal[1], -normal[2], -dist)
    return out


def nodes():
    """One node splitting on the floor plane: in front of it empty, behind solid.

    Children are leaf indices encoded as -(leaf + 1), which is the format's way
    of distinguishing them from node indices. child[0] is the front side, so it
    has to be the plane that points UP out of the floor - the room is in front
    of that one and the solid is behind it.

    This asked for plane 8 for a long time. Plane 8 is the ceiling, at z = +256,
    and a camera standing at z = 0 is behind it - which put every view in leaf 1,
    the solid one, whose cluster is -1. A leaf with no cluster sees nothing, so
    the world drew nothing, every frame, from the first day this fixture
    existed. It did not look like a failure: the head-up display still drew, the
    colour checks still passed, and the run reported that it had reached the map.
    It had. It just could not see it.
    """
    mins = (-4096, -4096, -4096)
    maxs = (4096, 4096, 4096)
    return struct.pack("<i2i3i3i", PLANE_FLOOR_UP, -1, -2, *(mins + maxs))


def leafs(num_surfaces=1):
    """Two: the room, and the solid outside it.

    Cluster 0 for the room and -1 for the solid one. CM_ClusterPVS reads the
    visibility lump with this index, so the two have to agree - see visibility()
    below, which is one cluster of one byte.
    """
    room = struct.pack("<2i3i3i4i", 0, 0,
                       -4096, -4096, -4096, 4096, 4096, 4096,
                       0, num_surfaces,     # the floor, and the sky wall if asked
                       0, 0)        # no brushes: this is the empty side
    solid = struct.pack("<2i3i3i4i", -1, 0,
                        -4096, -4096, -4096, 4096, 4096, 4096,
                        0, 0,
                        0, 1)       # one leaf brush: the box
    return room + solid


def models(num_surfaces=1):
    """Model 0 is the world and owns everything.

    R_LoadLightGrid takes the grid's bounds from bmodels[0], so these are not
    decoration: they decide how many grid points the renderer thinks the map
    has.
    """
    return struct.pack("<6f4i",
                       -HALF, -HALF, FLOOR_Z, HALF, HALF, HALF,
                       0, num_surfaces,
                       0, 1)        # one brush


def brushes():
    return struct.pack("<3i", 0, 6, 0)


def brushsides():
    """One per box face, on the even planes, all carrying the solid shader."""
    out = b""
    for i in range(6):
        out += struct.pack("<3i", i * 2, 0, -1)
    return out


# The sky wall: the far end of the room, facing back at the player.
#
# The player starts at the origin looking along +Y ("angle" "90"), so a quad at
# y = +HALF fills the view. The sky is not drawn where this surface is - it is
# drawn *through* it: RB_StageIteratorSky takes the surface's extent, projects
# it onto the six box faces in AddSkyPolygon, and draws those. Which face comes
# out is decided by direction, so looking along +Y gives the face on axis 2.
#
# Axis order in AddSkyPolygon is +X, -X, +Y, -Y, +Z, -Z, and ParseSkyParms reads
# the suffixes in the order rt, bk, lf, ft, up, dn. Those two lists line up by
# index, which is how "lf" ends up being the face straight ahead. The names are
# not a description of anything.
SKY_Y = HALF
SKY_TOP = FLOOR_Z + 2.0 * HALF

# All four walls, not one. A single sky wall means the fixture can only ever
# look at one of the six faces, and for a long time it looked at the one face
# where two different parameterisations happen to agree - so a cubemap with five
# of its six faces rotated passed every assertion in the smoke test. Turning
# round has to show a different face, and that only works if there is sky behind
# the camera as well as in front of it.
#
# Each entry is the inward normal. "Right", as seen by someone standing at the
# origin looking at that wall, is forward x up with forward = -normal, and the
# corners are wound in that frame so every wall faces the room the same way the
# original single wall did.
SKY_WALLS = (
    (0.0, -1.0, 0.0),
    (0.0, 1.0, 0.0),
    (-1.0, 0.0, 0.0),
    (1.0, 0.0, 0.0),
)


def sky_wall_frame(normal):
    """Where the wall sits and which way is right on it."""
    nx, ny, nz = normal
    forward = (-nx, -ny, -nz)
    # forward x (0,0,1)
    right = (forward[1] * 1.0 - forward[2] * 0.0,
             forward[2] * 0.0 - forward[0] * 1.0,
             0.0)
    origin = (-nx * HALF, -ny * HALF, 0.0)
    return origin, right


def sky_wall_corners(normal):
    """The four corners, bottom left first, wound as the room sees them."""
    (ox, oy, _), (rx, ry, _) = sky_wall_frame(normal)
    return (
        (ox - rx * HALF, oy - ry * HALF, FLOOR_Z),
        (ox + rx * HALF, oy + ry * HALF, FLOOR_Z),
        (ox + rx * HALF, oy + ry * HALF, SKY_TOP),
        (ox - rx * HALF, oy - ry * HALF, SKY_TOP),
    )


def drawverts(sky=False):
    """Four corners of the floor.

    RBSP has four lightmap coordinate pairs and four colours per vertex rather
    than one; getting that count wrong shifts every following vertex and shows
    up as a surface with its texture coordinates in the wrong place, not as an
    error.
    """
    out = b""
    for x, y, s, t in ((-HALF, -HALF, 0.0, 0.0),
                       (HALF, -HALF, 1.0, 0.0),
                       (HALF, HALF, 1.0, 1.0),
                       (-HALF, HALF, 0.0, 1.0)):
        out += struct.pack("<3f", x, y, FLOOR_Z)
        out += struct.pack("<2f", s, t)
        out += struct.pack("<8f", *([s, t] * MAXLIGHTMAPS))
        out += struct.pack("<3f", 0.0, 0.0, 1.0)
        out += bytes([255, 255, 255, 255] * MAXLIGHTMAPS)

    if sky:
        for normal in SKY_WALLS:
            corners = sky_wall_corners(normal)
            for (x, y, z), (s, t) in zip(corners, ((0.0, 1.0), (1.0, 1.0),
                                                   (1.0, 0.0), (0.0, 0.0))):
                out += struct.pack("<3f", x, y, z)
                out += struct.pack("<2f", s, t)
                out += struct.pack("<8f", *([s, t] * MAXLIGHTMAPS))
                out += struct.pack("<3f", *normal)      # facing the room
                out += bytes([255, 255, 255, 255] * MAXLIGHTMAPS)

    return out


def drawindexes(sky=False):
    """Two triangles per surface, and the numbers are RELATIVE to firstVert.

    Not absolute indices into the lump. ParseFace does `verts += ds->firstVert`
    and then copies the indices across untouched, so a second surface that
    numbered its corners 4..7 - which is where they really are in the lump -
    reads four vertices past the end of a surface that allocated four. That
    corrupts the heap rather than failing: the first run of this fixture with a
    sky in it died in glibc with "corrupted double-linked list" during the map
    load, several allocations after the damage was done.
    """
    out = struct.pack("<6i", 0, 1, 2, 0, 2, 3)
    if sky:
        out += struct.pack("<6i", 0, 1, 2, 0, 2, 3) * len(SKY_WALLS)
    return out


def surfaces(sky=False):
    """One planar quad, lit per vertex.

    lightmapNum is LIGHTMAP_BY_VERTEX in every slot: with no lightmap lump, any
    other value sends R_LoadSurfaces looking for a page that is not there.
    """
    out = struct.pack("<3i", 1, -1, MST_PLANAR)     # shader 1, no fog, planar
    out += struct.pack("<2i", 0, 4)                 # verts
    out += struct.pack("<2i", 0, 6)                 # indexes
    out += bytes([LS_NORMAL] + [LS_NONE] * 3)       # lightmapStyles
    out += bytes([LS_NORMAL] + [LS_NONE] * 3)       # vertexStyles
    out += struct.pack("<4i", *([LIGHTMAP_BY_VERTEX] * MAXLIGHTMAPS))
    out += struct.pack("<4i", *([0] * MAXLIGHTMAPS))    # lightmapX
    out += struct.pack("<4i", *([0] * MAXLIGHTMAPS))    # lightmapY
    out += struct.pack("<2i", 0, 0)                     # lightmapWidth/Height
    out += struct.pack("<3f", -HALF, -HALF, FLOOR_Z)    # lightmapOrigin
    out += struct.pack("<3f", 1.0, 0.0, 0.0)            # lightmapVecs
    out += struct.pack("<3f", 0.0, 1.0, 0.0)
    out += struct.pack("<3f", 0.0, 0.0, 1.0)            # the surface normal
    out += struct.pack("<2i", 0, 0)                     # patch width/height

    if sky:
        for n, normal in enumerate(SKY_WALLS):
            corner = sky_wall_corners(normal)[0]
            _, right = sky_wall_frame(normal)

            out += struct.pack("<3i", 2, -1, MST_PLANAR)        # shader 2
            out += struct.pack("<2i", 4 + n * 4, 4)             # verts
            out += struct.pack("<2i", 6 + n * 6, 6)             # indexes
            out += bytes([LS_NORMAL] + [LS_NONE] * 3)
            out += bytes([LS_NORMAL] + [LS_NONE] * 3)
            out += struct.pack("<4i", *([LIGHTMAP_BY_VERTEX] * MAXLIGHTMAPS))
            out += struct.pack("<4i", *([0] * MAXLIGHTMAPS))
            out += struct.pack("<4i", *([0] * MAXLIGHTMAPS))
            out += struct.pack("<2i", 0, 0)
            out += struct.pack("<3f", *corner)
            out += struct.pack("<3f", *right)
            out += struct.pack("<3f", 0.0, 0.0, 1.0)
            out += struct.pack("<3f", *normal)
            out += struct.pack("<2i", 0, 0)

    return out


def visibility():
    """One cluster that can see itself.

    numClusters and clusterBytes, then the vector. CM_ClusterPVS indexes this
    with the leaf's cluster, so it has to cover cluster 0.
    """
    return struct.pack("<2i", 1, 1) + bytes([0xFF])


def entities():
    """Worldspawn and one player start, standing on the floor.

    Standing, not forty units above it. The player's box reaches 24 below his
    origin and the floor is at FLOOR_Z, so this is exactly where he ends up
    either way - the difference is that he no longer falls to get there.

    That fall was costing the fixture its determinism, and it took a while to
    see. Landing dips the view, and the dip recovers over real time rather than
    over frames, so a screenshot taken a fixed number of frames after the map
    loads catches the horizon at a slightly different height depending on how
    fast the run happened to be going. Two runs of the same binary differed by a
    hundred and fifty pixels along the floor's edge - which is the size of a
    difference worth catching, so the harness could not be allowed to produce one
    on its own.
    """
    return (
        b'{\n'
        b'"classname" "worldspawn"\n'
        b'"message" "JKX headless fixture"\n'
        b'}\n'
        b'{\n'
        b'"classname" "info_player_start"\n'
        + b'"origin" "0 0 %d"\n' % int(FLOOR_Z + 24) +
        b'"angle" "90"\n'
        b'}\n\0'
    )


def build(visible_shader, sky_shader=None):
    sky = bool(sky_shader)
    count = 1 + len(SKY_WALLS) if sky else 1
    lumps = {
        LUMP_ENTITIES: entities(),
        LUMP_SHADERS: shaders(visible_shader, sky_shader),
        LUMP_PLANES: planes(),
        LUMP_NODES: nodes(),
        LUMP_LEAFS: leafs(count),
        LUMP_LEAFSURFACES: struct.pack("<%di" % count, *range(count)),
        LUMP_LEAFBRUSHES: struct.pack("<i", 0),
        LUMP_MODELS: models(count),
        LUMP_BRUSHES: brushes(),
        LUMP_BRUSHSIDES: brushsides(),
        LUMP_DRAWVERTS: drawverts(sky),
        LUMP_DRAWINDEXES: drawindexes(sky),
        LUMP_FOGS: b"",
        LUMP_SURFACES: surfaces(sky),
        LUMP_LIGHTMAPS: b"",
        LUMP_LIGHTGRID: b"",
        LUMP_VISIBILITY: visibility(),
        LUMP_LIGHTARRAY: b"",
    }

    header_size = 8 + HEADER_LUMPS * 8
    body = b""
    directory = []
    offset = header_size
    for i in range(HEADER_LUMPS):
        data = lumps[i]
        # Four-byte alignment: several readers cast straight into the buffer.
        pad = (-len(data)) % 4
        directory.append((offset, len(data)))
        body += data + b"\0" * pad
        offset += len(data) + pad

    out = BSP_IDENT + struct.pack("<i", BSP_VERSION)
    for ofs, length in directory:
        out += struct.pack("<2i", ofs, length)
    return out + body


# Record sizes, for --check. Each is sizeof() of the structure in qfiles.h, and
# a lump whose length is not a multiple of its record size is what the engine
# calls "funny lump size".
RECORD_SIZES = {
    LUMP_SHADERS: MAX_QPATH + 8,
    LUMP_PLANES: 16,
    LUMP_NODES: 36,
    LUMP_LEAFS: 48,
    LUMP_LEAFSURFACES: 4,
    LUMP_LEAFBRUSHES: 4,
    LUMP_MODELS: 40,
    LUMP_BRUSHES: 12,
    LUMP_BRUSHSIDES: 12,
    LUMP_DRAWVERTS: 12 + 8 + 8 * MAXLIGHTMAPS + 12 + 4 * MAXLIGHTMAPS,
    LUMP_DRAWINDEXES: 4,
    LUMP_FOGS: MAX_QPATH + 8,
    LUMP_SURFACES: 12 + 8 + 8 + 8 + 16 + 16 + 16 + 8 + 12 + 36 + 8,
}

LUMP_NAMES = {
    LUMP_ENTITIES: "entities", LUMP_SHADERS: "shaders", LUMP_PLANES: "planes",
    LUMP_NODES: "nodes", LUMP_LEAFS: "leafs", LUMP_LEAFSURFACES: "leafsurfaces",
    LUMP_LEAFBRUSHES: "leafbrushes", LUMP_MODELS: "models",
    LUMP_BRUSHES: "brushes", LUMP_BRUSHSIDES: "brushsides",
    LUMP_DRAWVERTS: "drawverts", LUMP_DRAWINDEXES: "drawindexes",
    LUMP_FOGS: "fogs", LUMP_SURFACES: "surfaces", LUMP_LIGHTMAPS: "lightmaps",
    LUMP_LIGHTGRID: "lightgrid", LUMP_VISIBILITY: "visibility",
    LUMP_LIGHTARRAY: "lightarray",
}


SURFACE_SIZE = 148


def check_surfaces(data, failures):
    """Every surface's indices must be inside its own vertices.

    They are relative to firstVert, and the cost of getting that wrong is not an
    error message - it is a write past the end of the surface's point array,
    which surfaces later as heap corruption in an unrelated allocation. Cheap to
    check here, expensive to find anywhere else.
    """
    ofs, length = struct.unpack_from("<2i", data, 8 + LUMP_SURFACES * 8)
    iofs, ilen = struct.unpack_from("<2i", data, 8 + LUMP_DRAWINDEXES * 8)
    vofs, vlen = struct.unpack_from("<2i", data, 8 + LUMP_DRAWVERTS * 8)

    for n in range(length // SURFACE_SIZE):
        base = ofs + n * SURFACE_SIZE
        first_vert, num_verts = struct.unpack_from("<2i", data, base + 12)
        first_index, num_indexes = struct.unpack_from("<2i", data, base + 20)

        if (first_index + num_indexes) * 4 > ilen:
            failures.append("surface %d: indices %d..%d overrun the lump"
                            % (n, first_index, first_index + num_indexes))
            continue

        for i in range(num_indexes):
            value = struct.unpack_from("<i", data, iofs + (first_index + i) * 4)[0]
            if value < 0 or value >= num_verts:
                failures.append(
                    "surface %d: index %d is %d, outside its own %d vertices - "
                    "these are relative to firstVert" % (n, i, value, num_verts))
                break


def check():
    failures = []
    for label, args in (("plain", ("jkx/smoke", None)),
                        ("with a sky", ("jkx/smoke", "textures/jkx/sky"))):
        data = build(*args)
        before = len(failures)
        check_surfaces(data, failures)
        if len(failures) != before:
            failures[before:] = ["%s: %s" % (label, f) for f in failures[before:]]

    data = build("jkx/smoke")

    if data[:4] != BSP_IDENT:
        failures.append("ident is %r, not %r" % (data[:4], BSP_IDENT))
    if struct.unpack_from("<i", data, 4)[0] != BSP_VERSION:
        failures.append("version is not %d" % BSP_VERSION)

    header_size = 8 + HEADER_LUMPS * 8
    if len(data) < header_size:
        failures.append("shorter than a header")
        header_size = len(data)

    for i in range(HEADER_LUMPS):
        ofs, length = struct.unpack_from("<2i", data, 8 + i * 8)
        name = LUMP_NAMES[i]
        if ofs < header_size or ofs + length > len(data):
            failures.append("%s: %d..%d is outside a %d byte file"
                            % (name, ofs, ofs + length, len(data)))
        if ofs % 4:
            failures.append("%s: offset %d is not four-byte aligned" % (name, ofs))
        size = RECORD_SIZES.get(i)
        if size and length % size:
            failures.append("%s: %d bytes is not a whole number of %d byte records"
                            % (name, length, size))

    for f in failures:
        print("error: %s" % f, file=sys.stderr)
    if failures:
        return 1

    print("test map: %d bytes, %d lumps, every record size and offset checks out"
          % (len(data), HEADER_LUMPS))
    return 0


def main(argv):
    args = argv[1:]
    if "--check" in args:
        return check()
    visible = "jkx/smoke"
    sky = None
    path = None
    i = 0
    while i < len(args):
        if args[i] == "--shader" and i + 1 < len(args):
            visible = args[i + 1]
            i += 2
        elif args[i] == "--sky" and i + 1 < len(args):
            sky = args[i + 1]
            i += 2
        elif path is None:
            path = args[i]
            i += 1
        else:
            print("unexpected argument: %s" % args[i], file=sys.stderr)
            return 2

    if path is None:
        print("usage: %s <out.bsp> [--shader NAME] [--sky NAME]" % argv[0],
              file=sys.stderr)
        return 2

    data = build(visible, sky)
    with open(path, "wb") as f:
        f.write(data)
    print("%s: %d bytes, shader %s%s"
          % (path, len(data), visible, ", sky %s" % sky if sky else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
