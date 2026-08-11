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

    make_test_bsp.py <out.bsp> [--shader NAME]
    make_test_bsp.py --check

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


def shaders(visible):
    """Two: one the brush sides carry, one the drawn surface carries.

    CMod_LoadBrushes and CMod_LoadBrushSides both range-check shaderNum against
    this lump, so the count here is not free - it is the bound they check.
    """
    out = b""
    out += qpath("textures/jkx/solid") + struct.pack("<ii", 0, CONTENTS_SOLID)
    out += qpath(visible) + struct.pack("<ii", SURF_NODAMAGE, CONTENTS_SOLID)
    return out


def planes():
    """Six box faces, each followed by its opposite.

    "planes x^1 is always the opposite of plane x" - qfiles.h. The tree below
    only uses the first of each pair, but the invariant is the format's and
    cheap to hold to.
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
    """One node splitting on the floor plane: above it empty, below it solid.

    Children are leaf indices encoded as -(leaf + 1), which is the format's way
    of distinguishing them from node indices.
    """
    mins = (-4096, -4096, -4096)
    maxs = (4096, 4096, 4096)
    return struct.pack("<i2i3i3i", 8, -1, -2, *(mins + maxs))


def leafs():
    """Two: the room, and the solid outside it.

    Cluster 0 for the room and -1 for the solid one. CM_ClusterPVS reads the
    visibility lump with this index, so the two have to agree - see visibility()
    below, which is one cluster of one byte.
    """
    room = struct.pack("<2i3i3i4i", 0, 0,
                       -4096, -4096, -4096, 4096, 4096, 4096,
                       0, 1,        # one leaf surface: the floor
                       0, 0)        # no brushes: this is the empty side
    solid = struct.pack("<2i3i3i4i", -1, 0,
                        -4096, -4096, -4096, 4096, 4096, 4096,
                        0, 0,
                        0, 1)       # one leaf brush: the box
    return room + solid


def models():
    """Model 0 is the world and owns everything.

    R_LoadLightGrid takes the grid's bounds from bmodels[0], so these are not
    decoration: they decide how many grid points the renderer thinks the map
    has.
    """
    return struct.pack("<6f4i",
                       -HALF, -HALF, FLOOR_Z, HALF, HALF, HALF,
                       0, 1,        # one surface
                       0, 1)        # one brush


def brushes():
    return struct.pack("<3i", 0, 6, 0)


def brushsides():
    """One per box face, on the even planes, all carrying the solid shader."""
    out = b""
    for i in range(6):
        out += struct.pack("<3i", i * 2, 0, -1)
    return out


def drawverts():
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
    return out


def drawindexes():
    return struct.pack("<6i", 0, 1, 2, 0, 2, 3)


def surfaces():
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
    return out


def visibility():
    """One cluster that can see itself.

    numClusters and clusterBytes, then the vector. CM_ClusterPVS indexes this
    with the leaf's cluster, so it has to cover cluster 0.
    """
    return struct.pack("<2i", 1, 1) + bytes([0xFF])


def entities():
    return (
        b'{\n'
        b'"classname" "worldspawn"\n'
        b'"message" "JKX headless fixture"\n'
        b'}\n'
        b'{\n'
        b'"classname" "info_player_start"\n'
        b'"origin" "0 0 0"\n'
        b'"angle" "90"\n'
        b'}\n\0'
    )


def build(visible_shader):
    lumps = {
        LUMP_ENTITIES: entities(),
        LUMP_SHADERS: shaders(visible_shader),
        LUMP_PLANES: planes(),
        LUMP_NODES: nodes(),
        LUMP_LEAFS: leafs(),
        LUMP_LEAFSURFACES: struct.pack("<i", 0),
        LUMP_LEAFBRUSHES: struct.pack("<i", 0),
        LUMP_MODELS: models(),
        LUMP_BRUSHES: brushes(),
        LUMP_BRUSHSIDES: brushsides(),
        LUMP_DRAWVERTS: drawverts(),
        LUMP_DRAWINDEXES: drawindexes(),
        LUMP_FOGS: b"",
        LUMP_SURFACES: surfaces(),
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


def check():
    data = build("jkx/smoke")
    failures = []

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
    path = None
    i = 0
    while i < len(args):
        if args[i] == "--shader" and i + 1 < len(args):
            visible = args[i + 1]
            i += 2
        elif path is None:
            path = args[i]
            i += 1
        else:
            print("unexpected argument: %s" % args[i], file=sys.stderr)
            return 2

    if path is None:
        print("usage: %s <out.bsp> [--shader NAME]" % argv[0], file=sys.stderr)
        return 2

    data = build(visible)
    with open(path, "wb") as f:
        f.write(data)
    print("%s: %d bytes, shader %s" % (path, len(data), visible))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
