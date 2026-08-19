#!/usr/bin/env python3
"""Write a map, so that the headless run can load one.

The three crashes the first real-hardware session found all lived between
Hunk_Clear wiping the renderer and RE_BeginRegistration building it again, and
the thing that fires inside that window is the game registering the models of a
map's entities. Nothing here could reach it, because nothing here has a map:
retail BSPs are the game's, and this repository holds none of the game's data
(the project backlog, section 10).

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

    make_test_bsp.py <out.bsp> [--shader NAME] [--sky NAME] [--lightmap]
                      [--prop MODEL[:Y[:Z]] ...] [--npc NAME[:Y[:Z]] ...]
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

import math
import os
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
CONTENTS_WATER = 32
SURF_NODAMAGE = 0x1


def qpath(name):
    b = name.encode("ascii")
    if len(b) >= MAX_QPATH:
        raise ValueError("shader name too long: %s" % name)
    return b + b"\0" * (MAX_QPATH - len(b))


def shaders(visible, sky=None, water=None):
    """One the brush sides carry, one the floor carries, one the sky wall.

    CMod_LoadBrushes and CMod_LoadBrushSides both range-check shaderNum against
    this lump, so the count here is not free - it is the bound they check.

    The water entry carries CONTENTS_WATER and NOT CONTENTS_SOLID.

    Worth being exact about which of the two content fields does what, because
    they have the same name and different owners. THIS one, in the BSP, is what
    the collision model reads - it is why a player swims rather than walks. What
    the RENDERER reads is shader->contentFlags, and that comes from the
    `surfaceparm water` line in the .shader file (tr_shader.cpp:373), not from
    here. So a water fixture needs both, and a lane that sets only one of them
    tests half of what it looks like it tests.
    """
    out = b""
    out += qpath("textures/jkx/solid") + struct.pack("<ii", 0, CONTENTS_SOLID)
    out += qpath(visible) + struct.pack("<ii", SURF_NODAMAGE, CONTENTS_SOLID)
    if sky:
        out += qpath(sky) + struct.pack("<ii", SURF_NODAMAGE, CONTENTS_SOLID)
    if water:
        out += qpath(water) + struct.pack("<ii", SURF_NODAMAGE, CONTENTS_WATER)
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
PLANE_UNDER = 12    # ( 0  0 -1)  z >= FLOOR_Z - SLAB, the underside of the slab

# How thick the floor is. Anything is fine; a trace only ever enters it from
# above, and a thin slab is one more way for a trace with a big box to go
# through it.
SLAB = 64.0

# The pool: a horizontal quad hanging over the floor, marked CONTENTS_WATER.
#
# It is a SURFACE and not a brush on purpose. What the renderer decides from is
# shader->contentFlags, which come from the shader lump entry the surface points
# at, so a water face needs a shader entry that says water and nothing else -
# the collision side of a real water volume is the game's business and no
# renderer lane can see it.
#
# Height and extent are chosen so the floor stays visible AROUND it as well as
# THROUGH it: depth attenuation, foam at the edge and refraction all need a
# reference that is the same picture without water in front of it, in the same
# frame rather than in another one.
#
# TILTED, and that is the whole reason there is anything to measure.
#
# The first version was horizontal, at a constant height over a horizontal
# floor, so the body of water was the same thickness everywhere. Nothing that
# depends on thickness has anything to grip on that: absorption is one number
# across the whole surface and foam, which lives where the water is thin, has
# nowhere to be. A pool with a shallow end is not a decoration here, it is the
# only arrangement in which "deeper is bluer" and "there is froth at the shore"
# are statements about a picture.
#
# So the near edge sits two units off the floor and the far edge sixty-four, and
# the surface normal is tilted to match. That also puts the one code path that
# would otherwise never run under the lane: every water face anyone has ever
# built is horizontal, and the shader builds its wave frame against the
# surface's own normal rather than against world Z precisely so that it does not
# stop working when one is not. A horizontal fixture cannot tell those two
# apart.
#
# BELOW the eye, and that is not a detail. The quad's normal points up, world
# faces are culled, and the player's eye sits at FLOOR_Z + 24 + 26. The first
# version of this put the surface at FLOOR_Z + 96, which is above that: the
# pool was a back face, nothing drew, and the lane reported a frame with no
# water in it and no error anywhere.
WATER_Z_NEAR = FLOOR_Z + 2.0
WATER_Z_FAR = FLOOR_Z + 64.0
WATER_HALF = 120.0
WATER_Y = 200.0


def water_normal():
    """The tilted pool's normal, from its own slope.

    Written out rather than typed in, because a normal that disagrees with the
    geometry it belongs to is not an error anywhere: the surface still draws,
    the lighting is simply wrong by an amount nobody can eyeball on a flat
    colour. The shader builds its wave frame on this vector.
    """
    dz = WATER_Z_FAR - WATER_Z_NEAR
    dy = 2.0 * WATER_HALF
    length = math.sqrt(dy * dy + dz * dz)
    return (0.0, -dz / length, dy / length)


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
        ((0, 0, -1), -( FLOOR_Z - SLAB )),
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
    """The floor, as a slab under it - and not the room, which is what this was.

    A brush is the intersection of the half spaces behind its sides, so a brush
    whose six sides are the six room faces pointing outwards IS the room: the
    whole interior, marked CONTENTS_SOLID. That is what this wrote, and the
    effect was invisible for as long as the fixture existed because nothing here
    ever asked the player to move.

    What it did to a run: every trace from inside the room came back allsolid.
    PM_GroundTrace then reports no ground at all, PM_SlideMove sees a completely
    trapped entity, zeroes the vertical velocity and returns without moving. So
    the player stood at the spawn with gravity 800 and never fell, +forward
    raised his velocity to 127 and his origin did not change, and noclip - which
    does not trace - worked perfectly. Measured, with a print in front of Pmove:
    "org=0.0 0.0 -39.8 grav=800 grnd=1023", frame after frame.

    A convex brush cannot be "everything outside the room", so the room's shell
    would be six slabs. It does not need to be: the only surface this fixture
    draws is the floor, so the only thing worth colliding with is the floor, and
    one slab under it is exactly that. The walls are drawn by nothing and now
    stop nothing, which is honest - a fixture that collides with a wall nobody
    can see is a fixture that will confuse someone later.

    Sides point out of the slab: the room's four vertical faces (which already
    point outwards and bound it in x and y), the floor plane pointing up, and a
    new one SLAB below it pointing down.

    The ORDER of the six is not free, which is the second thing this got wrong.
    CM_BoundBrush does not look at the planes' normals - it reads the first six
    sides positionally:

        bounds[0][0] = -sides[0].dist    bounds[1][0] = sides[1].dist
        bounds[0][1] = -sides[2].dist    bounds[1][1] = sides[3].dist
        bounds[0][2] = -sides[4].dist    bounds[1][2] = sides[5].dist

    so side 0 has to be the -X face, side 1 the +X face, and so on to side 4
    being -Z and side 5 being +Z. Written the other way round, as it was, the
    brush gets a bounding box somewhere else entirely and every trace rejects it
    before looking at a single plane. That is a silent miss: the brush is there,
    it is solid, and nothing ever touches it.
    """
    out = b""
    for plane in ( PLANE_X_NEG, PLANE_X_POS, PLANE_Y_NEG, PLANE_Y_POS,
            PLANE_UNDER, PLANE_FLOOR_UP ):
        out += struct.pack("<3i", plane, 0, -1)
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


# A real lightmap page, and the reason there was not one.
#
# Every surface in this fixture has been LIGHTMAP_BY_VERTEX since it was written,
# with its vertex colours at full white, and the lightmap lump has been empty.
# That is a whole subsystem the bench has never executed: R_LoadLightmaps, the
# lightmap atlas, the lightmap texture coordinates, and every USE_LIGHTMAP
# variant the shader generator builds. It is also how EVERY real map is lit.
#
# The second cost is that the world here cannot be measured. Its surfaces are
# white by design - a flat colour makes a pixel check exact - so "did the map get
# brighter" runs into 255 and stops. That came up the moment a question about
# overbright bits needed an answer and the bench could not give one.
#
# So: one page, one grey, everywhere. Grey rather than white because the point is
# to have somewhere to go in both directions, and one value rather than a pattern
# because what is being measured is a level, not a placement - the placement is
# what the sky faces and the crosshair checks are for.
LIGHTMAP_SIZE = 128

# Thirty-two, and the number is chosen so nothing clips.
#
# R_ColorShiftLightingBytes shifts a lightmap left by
# ( r_mapOverBrightBits - tr.overbrightBits ) and then, if any channel has gone
# over 255, divides all three by the largest - so a value that saturates does not
# just stop getting brighter, it stops being measurable. At 32 the three settings
# that matter give 32, 64 and 128, which are far apart, exact, and all inside the
# range. At 64 the last of them would be 256 and would come back as 255, and the
# check would read "the knob does nothing" when it had done everything.
LIGHTMAP_GREY = 32


def lightmaps(present):
    """One page of LIGHTMAP_SIZE squared, three bytes per texel.

    R_LoadLightmaps takes the page count from the lump length divided by that
    size, so a lump that is not a whole number of pages is not an error - it is
    a page count that disagrees with the surfaces referring to it.
    """
    if not present:
        return b""

    return bytes([LIGHTMAP_GREY, LIGHTMAP_GREY, LIGHTMAP_GREY]) * (LIGHTMAP_SIZE * LIGHTMAP_SIZE)


# An external HDR lightmap, which is what a map built in the last ten years has
# instead of a page inside the BSP.
#
# The bench had never seen one, and the path that reads them is not a variant of
# the internal path - it is a different loader producing a buffer that is EIGHT
# bytes per pixel rather than four, handed to an uploader that had four written
# into it in three places. The first real map with external lightmaps that was
# ever pointed at this engine died on the memcpy, sixteen megabytes past the end
# of a scratch buffer allocated at half the size it needed.
#
# So the size here is not arbitrary. It has to be big enough that the overrun
# leaves the mapping instead of landing in heap slack, because a corruption that
# does not fault is a lane that passes. At 1024 the buffer is four megabytes -
# past the threshold where malloc goes to mmap - and the copy asks for eight, so
# the second four are off the end of an mmap and the process dies. At 128 it
# would not, and the lane would be decorative.
HDR_LIGHTMAP_SIZE = 1024

# One value everywhere, and a float rather than a byte, because the point of an
# HDR lightmap is that it is not clamped at one. The loader multiplies by 1/pi,
# so this arrives as roughly 0.318 - inside the range, nowhere near either end,
# and different enough from the internal page's grey that the two lanes cannot
# be confused for one another by their pixels.
HDR_LIGHTMAP_VALUE = 1.0


def rgbe(value):
    """One Radiance RGBE pixel for a grey of the given intensity.

    RGBE is a shared exponent: the three mantissas are the colour scaled so the
    largest is in [128,256), and the fourth byte is the exponent plus 128. A
    value of zero is all four bytes zero and not an exponent of -128, which is
    the one special case worth getting right even though this fixture never asks
    for it.
    """
    if value <= 0.0:
        return bytes([0, 0, 0, 0])

    mantissa, exponent = math.frexp(value)
    scaled = int(mantissa * 256.0)
    return bytes([scaled, scaled, scaled, exponent + 128])


def hdr_lightmap(size=HDR_LIGHTMAP_SIZE, value=HDR_LIGHTMAP_VALUE):
    """A Radiance .hdr file, flat rather than run-length encoded.

    Uncompressed scanlines are legal and are what a reader falls back to when
    the first two bytes of a line are not 0x02 0x02. Writing them keeps this
    function short enough to be obviously correct, which matters more here than
    the file size - it is generated, never committed, and lives for one run.
    """
    header = (b"#?RADIANCE\n"
              b"FORMAT=32-bit_rle_rgbe\n"
              b"\n"
              b"-Y %d +X %d\n" % (size, size))
    return header + rgbe(value) * (size * size)


def drawverts(sky=False, water=False):
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

    if water:
        # Wound the same way as the floor - see drawindexes for why that is not
        # the order it reads right on paper - and with texture coordinates that
        # run over the whole quad, so a scrolling normal map has somewhere to
        # scroll.
        nx, ny, nz = water_normal()
        for x, y, z, s, t in ((-WATER_HALF, WATER_Y - WATER_HALF, WATER_Z_NEAR, 0.0, 0.0),
                              (WATER_HALF, WATER_Y - WATER_HALF, WATER_Z_NEAR, 1.0, 0.0),
                              (WATER_HALF, WATER_Y + WATER_HALF, WATER_Z_FAR, 1.0, 1.0),
                              (-WATER_HALF, WATER_Y + WATER_HALF, WATER_Z_FAR, 0.0, 1.0)):
            out += struct.pack("<3f", x, y, z)
            out += struct.pack("<2f", s, t)
            out += struct.pack("<8f", *([s, t] * MAXLIGHTMAPS))
            out += struct.pack("<3f", nx, ny, nz)
            out += bytes([255, 255, 255, 255] * MAXLIGHTMAPS)

    return out


def drawindexes(sky=False, water=False):
    """Two triangles per surface, and the numbers are RELATIVE to firstVert.

    Not absolute indices into the lump. ParseFace does `verts += ds->firstVert`
    and then copies the indices across untouched, so a second surface that
    numbered its corners 4..7 - which is where they really are in the lump -
    reads four vertices past the end of a surface that allocated four. That
    corrupts the heap rather than failing: the first run of this fixture with a
    sky in it died in glibc with "corrupted double-linked list" during the map
    load, several allocations after the damage was done.

    The order is 0,2,1 rather than 0,1,2, and that one transposition is the
    difference between a floor and no floor.

    The corners of every surface here are listed anticlockwise seen from the
    front, which is the order a right-handed normal comes out of and the order
    that reads correctly on paper. It is not the order this engine draws: a
    world face is wound the other way, and a face wound the way it looked right
    is a back face and is culled. So the floor of this fixture was never once
    drawn - not in a single run since the map generator was written - and
    nothing said so, because everything in the fixture that IS visible is
    two-sided: the animated square and the lit model both say `cull none`, and
    the sky does not go through face culling at all.

    What made it invisible instead of obvious is the check that was supposed to
    catch exactly this. jkx_inmap.tga is asserted to have white near the bottom
    of the frame and called "the map's floor"; the white it was finding is the
    player's model, which stands there and is baked white. Changing the floor's
    shader - to a lightmap, to flat green - left the frame byte for byte
    identical, which is what finally said it.
    """
    out = struct.pack("<6i", 0, 2, 1, 0, 3, 2)
    if sky:
        out += struct.pack("<6i", 0, 2, 1, 0, 3, 2) * len(SKY_WALLS)
    if water:
        out += struct.pack("<6i", 0, 2, 1, 0, 3, 2)
    return out


def surfaces(sky=False, fog=False, lightmap=False, water=False, water_shader_num=2):
    """One planar quad, lit per vertex or from a lightmap page.

    lightmapNum is LIGHTMAP_BY_VERTEX in every slot by default: with no lightmap
    lump, any other value sends R_LoadSurfaces looking for a page that is not
    there. With one, the floor points at page zero and the sky walls do not -
    a sky surface is not lit and asking for a lightmap on one would only test
    that the renderer ignores it.
    """
    # fogNum is stored one less than the index the renderer uses: the loader does
    # fogIndex = fogNum + 1, so -1 is "no fog" and 0 is the first one.
    out = struct.pack("<3i", 1, 0 if fog else -1, MST_PLANAR)
    out += struct.pack("<2i", 0, 4)                 # verts
    out += struct.pack("<2i", 0, 6)                 # indexes
    out += bytes([LS_NORMAL] + [LS_NONE] * 3)       # lightmapStyles
    out += bytes([LS_NORMAL] + [LS_NONE] * 3)       # vertexStyles
    if lightmap:
        out += struct.pack("<4i", 0, LIGHTMAP_BY_VERTEX,
                           LIGHTMAP_BY_VERTEX, LIGHTMAP_BY_VERTEX)
    else:
        out += struct.pack("<4i", *([LIGHTMAP_BY_VERTEX] * MAXLIGHTMAPS))
    out += struct.pack("<4i", *([0] * MAXLIGHTMAPS))    # lightmapX
    out += struct.pack("<4i", *([0] * MAXLIGHTMAPS))    # lightmapY
    if lightmap:
        out += struct.pack("<2i", LIGHTMAP_SIZE, LIGHTMAP_SIZE)
    else:
        out += struct.pack("<2i", 0, 0)                 # lightmapWidth/Height
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

    if water:
        first_vert = 4 + (len(SKY_WALLS) * 4 if sky else 0)
        first_index = 6 + (len(SKY_WALLS) * 6 if sky else 0)

        out += struct.pack("<3i", water_shader_num, -1, MST_PLANAR)
        out += struct.pack("<2i", first_vert, 4)
        out += struct.pack("<2i", first_index, 6)
        out += bytes([LS_NORMAL] + [LS_NONE] * 3)
        out += bytes([LS_NORMAL] + [LS_NONE] * 3)
        out += struct.pack("<4i", *([LIGHTMAP_BY_VERTEX] * MAXLIGHTMAPS))
        out += struct.pack("<4i", *([0] * MAXLIGHTMAPS))
        out += struct.pack("<4i", *([0] * MAXLIGHTMAPS))
        out += struct.pack("<2i", 0, 0)
        out += struct.pack("<3f", -WATER_HALF, WATER_Y - WATER_HALF, WATER_Z_NEAR)
        out += struct.pack("<3f", 1.0, 0.0, 0.0)
        out += struct.pack("<3f", 0.0, 1.0, 0.0)
        out += struct.pack("<3f", *water_normal())
        out += struct.pack("<2i", 0, 0)

    return out


# The light grid: how the world lights everything that is not part of it.
#
# Every model in every real map is lit by asking this grid where it stands -
# R_SetupEntityLightingGrid - and the fixture shipped both of its lumps empty,
# which is not the same as shipping a dark map. An empty LUMP_LIGHTARRAY fails
# the size check in R_LoadLightGridArray, which nulls lightGridData and returns;
# the warning it prints goes through vk_debug, so a release build says nothing at
# all. Every headless run so far has therefore exercised the fall-back branch and
# never the grid.
#
# The engine derives the grid's shape from the world model's bounds rather than
# from anything in these lumps, so the array length is not a free choice - get it
# wrong and the load silently reverts to the state described above. The
# arithmetic below is R_LoadLightGrid's, repeated, and the self-check compares
# the two.
GRID_SIZE = (64.0, 64.0, 128.0)     # the engine's default, and no worldspawn key


def light_grid_bounds():
    """Cells per axis, by the engine's own formula."""
    mins = (-HALF, -HALF, FLOOR_Z)
    maxs = (HALF, HALF, HALF)
    out = []
    for i in range(3):
        step = GRID_SIZE[i]
        origin = step * math.ceil(mins[i] / step)
        top = step * math.floor(maxs[i] / step)
        out.append(int((top - origin) / step) + 1)
    return out


# One cell's worth of light, reused by every cell. A strong colour rather than a
# plausible one: the question this fixture asks is whether the grid was read at
# all, and a tasteful grey is indistinguishable from the fall-back.
#
# Ambient is what reaches a surface from everywhere, directed is what arrives
# along latLong. Both are shifted by R_ColorShiftLightingBytes on load, so these
# are not the numbers that come out the other end.
GRID_AMBIENT = (160, 32, 32)
GRID_DIRECT = (255, 64, 64)
GRID_LATLONG = (32, 64)


def lightgrid():
    """A single mgrid_t: ambient, directed, styles, and a direction.

    Thirty bytes, and the layout is four lightmap styles deep like everything
    else in RBSP: byte ambient[4][3], byte direct[4][3], byte styles[4],
    byte latLong[2].
    """
    out = bytearray()
    for _ in range(MAXLIGHTMAPS):
        out += bytes(GRID_AMBIENT)
    for _ in range(MAXLIGHTMAPS):
        out += bytes(GRID_DIRECT)
    out += bytes([LS_NORMAL] + [LS_NONE] * 3)
    out += bytes(GRID_LATLONG)
    return bytes(out)


def lightarray():
    """One unsigned short per cell, all naming the single grid record.

    R_LoadLightGridArray checks this length against the bounds it derived
    itself, and on a mismatch it throws the grid away rather than failing - so
    the length is the whole contract.
    """
    x, y, z = light_grid_bounds()
    return struct.pack("<%dH" % (x * y * z), *([0] * (x * y * z)))


# A fog volume, in the form that needs no brush: brushNum -1 means the fog is the
# whole world, and R_LoadFogs takes that branch without looking at any geometry.
#
# Worth having because RB_FogPass had never run in a headless test. The fog path
# is not small - a second blended pass over every fogged surface, its own shader
# permutation, its own texture coordinate generation in RB_CalcFogTexCoords and a
# collapse path that folds it into the surface's own stage - and none of it was
# reached, because the generated map had no fogs and the retail maps are not in
# this repository.
#
# The record is dfog_t: char shader[MAX_QPATH], int brushNum, int visibleSide.
def fogs(name=None):
    if not name:
        return b""
    return qpath(name) + struct.pack("<2i", -1, -1)


def visibility():
    """One cluster that can see itself.

    numClusters and clusterBytes, then the vector. CM_ClusterPVS indexes this
    with the leaf's cluster, so it has to cover cluster 0.
    """
    return struct.pack("<2i", 1, 1) + bytes([0xFF])


# Where the map's own model entity stands: in front of the player start, along
# the way he faces, and far enough back to be whole in the frame rather than
# filling it.
#
# The player start is at ( 0, 0, FLOOR_Z + 24 ) facing +Y, and the camera in
# third person sits behind him, so a hundred and sixty units ahead puts this
# comfortably inside the view without touching him.
PROP_Y = 160.0
PROP_Z = FLOOR_Z + 32


def parse_props(specs):
    """MODEL[:Y[:Z]] into (model, y, z), defaulting to the single prop's place.

    Several of them, because the transparency lane needs a backdrop and three
    translucent squares in front of it at different heights, and the one thing
    that makes a blend result an exact number is knowing what is behind it.
    """
    props = []
    for spec in specs:
        parts = spec.split(":")
        model = parts[0]
        y = float(parts[1]) if len(parts) > 1 else PROP_Y
        z = float(parts[2]) if len(parts) > 2 else PROP_Z
        props.append((model, y, z))
    return props


def parse_npcs(specs):
    """NAME[:Y[:Z]] into (name, y, z), for the NPC_spawner entities.

    A map's own NPC, spawned while the level spawns, is a different thing from
    the player - and the difference is the whole reason this exists. The player
    connects AFTER cgame has initialised, so the skin he asks for reaches the
    CS_CHARSKINS configstrings too late for the loop in CG_RegisterGraphics that
    turns configstring indexes into renderer handles. A map's NPC is spawned
    with the rest of the entities, before any of that, which is what every
    retail level does and what no fixture here did.
    """
    out = []
    for i, spec in enumerate(specs):
        parts = spec.split(":")
        name = parts[0]
        # In front of the player start by default, and spread apart, because a
        # character behind the camera proves nothing about which skin he is
        # wearing. PROP_Y is where the props stand and is known to be in frame.
        y = int(parts[1]) if len(parts) > 1 and parts[1] else int(PROP_Y + i * 80)
        z = int(parts[2]) if len(parts) > 2 and parts[2] else int(FLOOR_Z + 24)
        out.append((name, y, z))
    return out


def entities(props=(), npcs=(), cloud=False):
    """Worldspawn, one player start, and any props the MAP owns.

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

    The prop is misc_model_breakable, and it is here because until now not one
    entity in this fixture carried a model. Everything the bench has drawn was
    put on screen by the console - testmodel - or by the player himself, and
    that is a different path in every respect that matters: a map's prop gets
    its model through G_ModelIndex, which is a CONFIGSTRING index, and cgame
    turns that into a renderer handle in two separate places - the loop in
    CG_RegisterGraphics during the load, and CG_ConfigStringModified afterwards.
    Which of the two a given prop goes through is exactly the difference between
    a level that is furnished when it appears and one where the furniture
    arrives a moment later, in front of the player. That was reported from
    hardware and this fixture could not say anything about it.

    misc_model_breakable rather than misc_model, because misc_model is compiled
    into the BSP by the map compiler and does not exist at runtime at all - a
    generated map cannot have one. The breakable is a real entity with a real
    modelindex, which is the thing being measured.

    SOLID is off deliberately: a prop the player can walk through cannot change
    where he ends up standing, and the determinism of every other check in this
    fixture depends on him ending up in the same place.
    """
    out = (
        b'{\n'
        b'"classname" "worldspawn"\n'
        b'"message" "JKX headless fixture"\n'
        b'}\n'
        b'{\n'
        b'"classname" "info_player_start"\n'
        + b'"origin" "0 0 %d"\n' % int(FLOOR_Z + 24) +
        b'"angle" "90"\n'
        b'}\n'
    )

    for model, y, z in props:
        out += (
            b'{\n'
            b'"classname" "misc_model_breakable"\n'
            + b'"model" "%s"\n' % model.encode("ascii") +
            b'"origin" "0 %d %d"\n' % (int(y), int(z)) +
            b'"angle" "270"\n'
            b'"spawnflags" "0"\n'
            b'}\n'
        )

    # NPC_spawner is the generic one: every NPC_<name> entity in a retail map is
    # this with the type filled in. NOTSOLID (spawnflag 64) and no angle change,
    # for the same reason the props are not solid - nothing here may move the
    # player, because every other check in this fixture is written against where
    # he ends up standing.
    for name, y, z in npcs:
        out += (
            b'{\n'
            b'"classname" "NPC_spawner"\n'
            + b'"NPC_type" "%s"\n' % name.encode("ascii") +
            b'"origin" "0 %d %d"\n' % (int(y), int(z)) +
            b'"angle" "270"\n'
            b'"spawnflags" "64"\n'
            b'}\n'
        )

    # An entity whose refEntity type this renderer has no surface function for.
    #
    # fx_cloudlayer is the one that exists in real maps - "mostly for bespin
    # undercity but could be used other places" - and CG_Clouds submits it as
    # RT_CLOUDS every frame. RB_SurfaceEntity has no case for that, and until
    # now the default drew RB_SurfaceAxis: three coloured lines from the origin,
    # a developer's orientation aid, in front of the player, in normal play.
    #
    # The fixture never had one, so the whole default branch of RB_SurfaceEntity
    # was unreachable from this bench - which is why "there is an RGB cross
    # floating in the air" could only ever be reported from a real map.
    #
    # radius small and contents zero: the layer must not reach the player or
    # anything else this fixture measures. It is here to be SUBMITTED, not to be
    # looked at.
    if cloud:
        out += (
            b'{\n'
            b'"classname" "fx_cloudlayer"\n'
            + b'"origin" "0 200 %d"\n' % int(FLOOR_Z + 96) +
            b'"radius" "64"\n'
            b'"random" "16"\n'
            b'"wait" "0"\n'
            b'}\n'
        )

    return out + b'\0'


def build(visible_shader, sky_shader=None, fog_shader=None, lightmap=None,
          props=(), npcs=(), cloud=False, water_shader=None):
    """lightmap is None, "internal" or "hdr".

    "internal" puts a page in LUMP_LIGHTMAPS, which is how every retail map is
    lit. "hdr" leaves the lump EMPTY and still points the floor at page zero:
    that is the arrangement a modern map ships with, and it is what makes
    R_LoadLightmaps go looking for maps/<name>/lm_0000.hdr instead. The two
    paths share almost nothing after that line, and until now the bench only
    ever took the first.
    """
    sky = bool(sky_shader)
    water = bool(water_shader)
    count = 1 + len(SKY_WALLS) if sky else 1
    if water:
        count += 1
    # The shader lump is written in this order, so the water entry lands after
    # the sky one when both are present. Working the index out here rather than
    # in surfaces() keeps the two in step: a surface pointing at the wrong
    # shader entry is not an error, it is a water face that draws as the floor.
    water_shader_num = 3 if sky else 2
    lumps = {
        LUMP_ENTITIES: entities(props, npcs, cloud),
        LUMP_SHADERS: shaders(visible_shader, sky_shader, water_shader),
        LUMP_PLANES: planes(),
        LUMP_NODES: nodes(),
        LUMP_LEAFS: leafs(count),
        LUMP_LEAFSURFACES: struct.pack("<%di" % count, *range(count)),
        LUMP_LEAFBRUSHES: struct.pack("<i", 0),
        LUMP_MODELS: models(count),
        LUMP_BRUSHES: brushes(),
        LUMP_BRUSHSIDES: brushsides(),
        LUMP_DRAWVERTS: drawverts(sky, water),
        LUMP_DRAWINDEXES: drawindexes(sky, water),
        LUMP_FOGS: fogs(fog_shader),
        LUMP_SURFACES: surfaces(sky, bool(fog_shader), lightmap is not None,
                                water, water_shader_num),
        LUMP_LIGHTMAPS: lightmaps(lightmap == "internal"),
        LUMP_LIGHTGRID: lightgrid(),
        LUMP_VISIBILITY: visibility(),
        LUMP_LIGHTARRAY: lightarray(),
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
    LUMP_LIGHTGRID: 30,     # byte[4][3] + byte[4][3] + byte[4] + byte[2]
    LUMP_LIGHTARRAY: 2,
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


# ---------------------------------------------------------------------------
# Deliberate damage, and the invariants it breaks.
#
# Everything above writes a map the engine should accept. This part writes maps
# the engine should REFUSE, one broken field at a time, because "the loader is
# careful" is a claim about the maps nobody generates - a truncated download, a
# compiler that crashed halfway through writing, a file someone edited. A loader
# that walks off the end of one of these does it inside SV_SpawnServer, after
# Hunk_Clear has already wiped the renderer, so what a player sees is not a
# rejected map: it is a dead process on a map that "sometimes does not work".
#
# The list is derived from the format rather than from any loader. RBSP is a
# directory of eighteen lumps and a graph of integer indices between them, so
# there are exactly two families of thing that can be wrong, and the second one
# is not visible to a reader that only checks the first:
#
#   the directory  - an offset or a length that is negative, that leaves the
#                    file, or that is not a whole number of records. Nothing
#                    inside the lump has to be read for this to be fatal: the
#                    loader has already been handed a pointer and a count.
#
#   the graph      - an index that leaves the lump it points into. Every one of
#                    these reads a plausible-looking integer out of whatever
#                    happens to follow the lump, which is why they are the
#                    expensive half: a directory fault dies immediately and an
#                    index fault dies later, somewhere else, in code that did
#                    nothing wrong.
#
# Each kind changes ONE field and nothing else - the file keeps its length and
# its layout, and --check asserts that no corruption moves more than eight
# bytes. That is the whole point: when a lane goes red, the field named in the
# kind is the field the engine failed to check, with no second candidate.
#
# Field offsets are sizeof-arithmetic on the structures in qfiles.h, and they
# are spelled out rather than computed so that a structure change breaks this
# loudly instead of shifting it quietly.
BAD_INDEX = 4242            # past the end of every lump in the fixture

SURFACE_SHADERNUM = 0       # dsurface_t
SURFACE_FOGNUM = 4
SURFACE_FIRSTVERT = 12
SURFACE_NUMVERTS = 16
SURFACE_FIRSTINDEX = 20
SURFACE_NUMINDEXES = 24

NODE_PLANENUM = 0           # dnode_t
NODE_CHILD0 = 4
NODE_CHILD1 = 8

LEAF_FIRSTLEAFSURFACE = 32  # dleaf_t
LEAF_NUMLEAFSURFACES = 36

MODEL_FIRSTSURFACE = 24     # dmodel_t
MODEL_NUMSURFACES = 28

BRUSHSIDE_PLANENUM = 0      # dbrushside_t


def lump_at(data, index):
    """The directory entry for a lump: (fileofs, filelen)."""
    return struct.unpack_from("<2i", data, 8 + index * 8)


def set_lump(data, index, ofs, length):
    struct.pack_into("<2i", data, 8 + index * 8, ofs, length)


def poke(data, offset, value):
    struct.pack_into("<i", data, offset, value)


def record_at(data, index, n):
    """Byte offset of record n of a lump, by that lump's own record size."""
    ofs, _ = lump_at(data, index)
    return ofs + n * RECORD_SIZES[index]


def count_in(data, index):
    _, length = lump_at(data, index)
    return length // RECORD_SIZES[index]


# The corruptions. Each is (name, tag, description, function), where the tag
# names the invariant validate() below reports when it is broken - that pairing
# is what --check verifies, so a corruption that quietly stops corrupting
# anything fails the unit tests rather than passing a headless lane.
def _c_ident(d):
    d[0:4] = b"XBSP"


def _c_version(d):
    poke(d, 4, BSP_VERSION + 99)


def _c_lump_past_eof(d):
    ofs, length = lump_at(d, LUMP_SURFACES)
    set_lump(d, LUMP_SURFACES, ofs, length + len(d))


def _c_lump_negative_ofs(d):
    _, length = lump_at(d, LUMP_SURFACES)
    set_lump(d, LUMP_SURFACES, -64, length)


def _c_lump_negative_len(d):
    ofs, _ = lump_at(d, LUMP_SURFACES)
    set_lump(d, LUMP_SURFACES, ofs, -SURFACE_SIZE)


def _c_lump_odd_size(d):
    # Not a whole number of dplane_t: the "funny lump size" of every id Tech
    # loader since Quake. One byte short is enough and is the shape a truncated
    # write actually has.
    ofs, length = lump_at(d, LUMP_PLANES)
    set_lump(d, LUMP_PLANES, ofs, length - 1)


def _c_node_plane(d):
    poke(d, record_at(d, LUMP_NODES, 0) + NODE_PLANENUM, BAD_INDEX)


def _c_node_child_node(d):
    # A positive child is a node index, and the fixture has exactly one node.
    poke(d, record_at(d, LUMP_NODES, 0) + NODE_CHILD0, BAD_INDEX)


def _c_node_child_leaf(d):
    # A negative child is -(leaf + 1), and the fixture has exactly two leafs.
    poke(d, record_at(d, LUMP_NODES, 0) + NODE_CHILD1, -(BAD_INDEX + 1))


def _c_node_cycle(d):
    # Node zero's front child is node zero. Every walk of a BSP tree is a
    # recursion with no depth limit in it, so this is not an out-of-range read -
    # it is an unbounded one, and the first symptom is the stack.
    poke(d, record_at(d, LUMP_NODES, 0) + NODE_CHILD0, 0)


def _c_leaf_surfaces(d):
    # The leaf claims a run of leafsurfaces longer than the lump.
    poke(d, record_at(d, LUMP_LEAFS, 0) + LEAF_NUMLEAFSURFACES, BAD_INDEX)


def _c_leafsurface_range(d):
    # The entry inside the lump, which is a different check from the one above:
    # the run is legal and what it holds is not.
    ofs, _ = lump_at(d, LUMP_LEAFSURFACES)
    poke(d, ofs, BAD_INDEX)


def _c_brushside_plane(d):
    poke(d, record_at(d, LUMP_BRUSHSIDES, 0) + BRUSHSIDE_PLANENUM, BAD_INDEX)


def _c_surface_shader(d):
    poke(d, record_at(d, LUMP_SURFACES, 0) + SURFACE_SHADERNUM, BAD_INDEX)


def _c_surface_fog(d):
    # fogNum is stored one less than the index the renderer uses, so this asks
    # for fog 4243 in a map with no fogs at all.
    poke(d, record_at(d, LUMP_SURFACES, 0) + SURFACE_FOGNUM, BAD_INDEX)


def _c_surface_firstvert(d):
    poke(d, record_at(d, LUMP_SURFACES, 0) + SURFACE_FIRSTVERT, BAD_INDEX)


def _c_surface_numverts(d):
    # Past the end of the lump AND past SHADER_MAX_VERTEXES, which are two
    # different limits with two different consequences.
    poke(d, record_at(d, LUMP_SURFACES, 0) + SURFACE_NUMVERTS, BAD_INDEX)


def _c_surface_numindexes(d):
    poke(d, record_at(d, LUMP_SURFACES, 0) + SURFACE_NUMINDEXES, BAD_INDEX)


def _c_model_surfaces(d):
    # Model zero is the world and owns every surface in it.
    poke(d, record_at(d, LUMP_MODELS, 0) + MODEL_NUMSURFACES, BAD_INDEX)


def _c_vis_short(d):
    # The lump's own header says one cluster of one byte; the lump is left with
    # room for the header and nothing else. CM_ClusterPVS indexes the vector
    # with a leaf's cluster and there is no vector.
    ofs, _ = lump_at(d, LUMP_VISIBILITY)
    set_lump(d, LUMP_VISIBILITY, ofs, 8)


def _c_lightarray_range(d):
    # One unsigned short per grid cell, naming a record in LUMP_LIGHTGRID. The
    # fixture has exactly one record, and this cell asks for the four thousandth
    # - which is read every time anything in the map is lit, not at load time.
    ofs, _ = lump_at(d, LUMP_LIGHTARRAY)
    struct.pack_into("<H", d, ofs, BAD_INDEX)


def _c_entities_unterminated(d):
    # The entity lump is a C string and the terminator is inside the lump. Take
    # it out and the parser reads past the end of the hunk block the lump was
    # copied into.
    ofs, length = lump_at(d, LUMP_ENTITIES)
    set_lump(d, LUMP_ENTITIES, ofs, length - 1)


CORRUPTIONS = (
    ("ident", "header.ident",
     "the four magic bytes are not RBSP", _c_ident),
    ("version", "header.version",
     "the version is not the one this engine reads", _c_version),
    ("lump-past-eof", "lump.surfaces.range",
     "the surfaces lump ends past the end of the file", _c_lump_past_eof),
    ("lump-negative-ofs", "lump.surfaces.range",
     "the surfaces lump starts at a negative offset", _c_lump_negative_ofs),
    ("lump-negative-len", "lump.surfaces.range",
     "the surfaces lump has a negative length", _c_lump_negative_len),
    ("lump-odd-size", "lump.planes.stride",
     "the planes lump is not a whole number of planes", _c_lump_odd_size),
    ("node-plane", "node.planeNum",
     "a node splits on a plane that does not exist", _c_node_plane),
    ("node-child-node", "node.child",
     "a node's front child is a node that does not exist", _c_node_child_node),
    ("node-child-leaf", "node.child",
     "a node's back child is a leaf that does not exist", _c_node_child_leaf),
    ("node-cycle", "node.cycle",
     "a node is its own front child, so the tree is not a tree",
     _c_node_cycle),
    ("leaf-surfaces", "leaf.leafSurfaces",
     "a leaf claims more leafsurfaces than the lump holds", _c_leaf_surfaces),
    ("leafsurface-range", "leafsurface.value",
     "a leafsurface names a surface that does not exist",
     _c_leafsurface_range),
    ("brushside-plane", "brushside.planeNum",
     "a brush side is on a plane that does not exist", _c_brushside_plane),
    ("surface-shader", "surface.shaderNum",
     "a surface carries a shader that does not exist", _c_surface_shader),
    ("surface-fog", "surface.fogNum",
     "a surface is in a fog volume that does not exist", _c_surface_fog),
    ("surface-firstvert", "surface.firstVert",
     "a surface's vertices start past the end of the vertex lump",
     _c_surface_firstvert),
    ("surface-numverts", "surface.numVerts",
     "a surface has more vertices than the lump holds", _c_surface_numverts),
    ("surface-numindexes", "surface.numIndexes",
     "a surface has more indices than the lump holds", _c_surface_numindexes),
    ("model-surfaces", "model.numSurfaces",
     "the world model owns more surfaces than exist", _c_model_surfaces),
    ("vis-short", "vis.length",
     "the visibility lump is shorter than its own header says", _c_vis_short),
    ("lightarray-range", "lightarray.value",
     "a light grid cell names a grid record that does not exist",
     _c_lightarray_range),
    ("entities-unterminated", "entities.terminator",
     "the entity string has no terminator inside its lump",
     _c_entities_unterminated),
)

CORRUPT_KINDS = tuple(name for name, _, _, _ in CORRUPTIONS)


def corrupt(data, kind):
    """Break one field of a finished BSP and return the result."""
    for name, _, _, fn in CORRUPTIONS:
        if name == kind:
            out = bytearray(data)
            fn(out)
            return bytes(out)
    raise ValueError("unknown corruption: %s" % kind)


def corrupt_description(kind):
    for name, _, description, _ in CORRUPTIONS:
        if name == kind:
            return description
    raise ValueError("unknown corruption: %s" % kind)


def validate(data):
    """Everything the format says about a BSP, as (tag, message) pairs.

    This is the reader the engine ought to have, written from qfiles.h and from
    nothing else - deliberately, because a check written from the loader can
    only ever agree with the loader. It has two uses: it is the assertion that
    the fixture the bench generates is well formed, and it is what --check uses
    to prove that each corruption really did break the thing it claims to break.

    Directory faults return early. Once a lump's offset or length is wrong there
    is nothing meaningful to say about the integers inside it, and a page of
    consequential complaints buries the one that matters.
    """
    out = []
    header_size = 8 + HEADER_LUMPS * 8

    if len(data) < header_size:
        return [("header.size", "shorter than a %d byte header" % header_size)]
    if data[:4] != BSP_IDENT:
        out.append(("header.ident", "ident is %r, not %r"
                    % (bytes(data[:4]), BSP_IDENT)))
    if struct.unpack_from("<i", data, 4)[0] != BSP_VERSION:
        out.append(("header.version", "version is %d, not %d"
                    % (struct.unpack_from("<i", data, 4)[0], BSP_VERSION)))

    for i in range(HEADER_LUMPS):
        ofs, length = lump_at(data, i)
        name = LUMP_NAMES[i]
        if ofs < header_size or length < 0 or ofs + length > len(data):
            out.append(("lump.%s.range" % name,
                        "%s: %d..%d is outside a %d byte file"
                        % (name, ofs, ofs + length, len(data))))
            continue
        size = RECORD_SIZES.get(i)
        if size and length % size:
            out.append(("lump.%s.stride" % name,
                        "%s: %d bytes is not a whole number of %d byte records"
                        % (name, length, size)))

    if out:
        return out

    num_planes = count_in(data, LUMP_PLANES)
    num_nodes = count_in(data, LUMP_NODES)
    num_leafs = count_in(data, LUMP_LEAFS)
    num_leafsurfaces = count_in(data, LUMP_LEAFSURFACES)
    num_surfaces = count_in(data, LUMP_SURFACES)
    num_shaders = count_in(data, LUMP_SHADERS)
    num_fogs = count_in(data, LUMP_FOGS)
    num_verts = count_in(data, LUMP_DRAWVERTS)
    num_indexes = count_in(data, LUMP_DRAWINDEXES)
    num_models = count_in(data, LUMP_MODELS)
    num_brushsides = count_in(data, LUMP_BRUSHSIDES)
    num_grid = count_in(data, LUMP_LIGHTGRID)

    for n in range(num_nodes):
        base = record_at(data, LUMP_NODES, n)
        plane = struct.unpack_from("<i", data, base + NODE_PLANENUM)[0]
        if plane < 0 or plane >= num_planes:
            out.append(("node.planeNum",
                        "node %d splits on plane %d of %d"
                        % (n, plane, num_planes)))
        for c in range(2):
            child = struct.unpack_from("<i", data, base + NODE_CHILD0 + c * 4)[0]
            if child >= 0:
                if child >= num_nodes:
                    out.append(("node.child",
                                "node %d child %d is node %d of %d"
                                % (n, c, child, num_nodes)))
            elif -(child + 1) >= num_leafs:
                out.append(("node.child", "node %d child %d is leaf %d of %d"
                            % (n, c, -(child + 1), num_leafs)))

    # The tree, walked. Bounds alone do not make a tree: a child that is in
    # range and points back up the way it came is a loop, and a loop is what the
    # loader's own recursion runs into rather than what it reads.
    if num_nodes:
        seen = set()
        stack = [0]
        while stack:
            n = stack.pop()
            if n in seen:
                out.append(("node.cycle",
                            "node %d is reached twice, so the tree has a loop"
                            % n))
                break
            seen.add(n)
            base = record_at(data, LUMP_NODES, n)
            for c in range(2):
                child = struct.unpack_from("<i", data,
                                           base + NODE_CHILD0 + c * 4)[0]
                if 0 <= child < num_nodes:
                    stack.append(child)

    for n in range(num_leafs):
        base = record_at(data, LUMP_LEAFS, n)
        first, count = struct.unpack_from("<2i", data,
                                          base + LEAF_FIRSTLEAFSURFACE)
        if first < 0 or count < 0 or first + count > num_leafsurfaces:
            out.append(("leaf.leafSurfaces",
                        "leaf %d holds leafsurfaces %d..%d of %d"
                        % (n, first, first + count, num_leafsurfaces)))

    ofs, _ = lump_at(data, LUMP_LEAFSURFACES)
    for n in range(num_leafsurfaces):
        value = struct.unpack_from("<i", data, ofs + n * 4)[0]
        if value < 0 or value >= num_surfaces:
            out.append(("leafsurface.value",
                        "leafsurface %d names surface %d of %d"
                        % (n, value, num_surfaces)))

    for n in range(num_brushsides):
        base = record_at(data, LUMP_BRUSHSIDES, n)
        plane = struct.unpack_from("<i", data, base + BRUSHSIDE_PLANENUM)[0]
        if plane < 0 or plane >= num_planes:
            out.append(("brushside.planeNum",
                        "brush side %d is on plane %d of %d"
                        % (n, plane, num_planes)))

    for n in range(num_surfaces):
        base = record_at(data, LUMP_SURFACES, n)
        shader = struct.unpack_from("<i", data, base + SURFACE_SHADERNUM)[0]
        fog = struct.unpack_from("<i", data, base + SURFACE_FOGNUM)[0]
        first_vert, num = struct.unpack_from("<2i", data,
                                             base + SURFACE_FIRSTVERT)
        first_index, count = struct.unpack_from("<2i", data,
                                                base + SURFACE_FIRSTINDEX)

        if shader < 0 or shader >= num_shaders:
            out.append(("surface.shaderNum",
                        "surface %d carries shader %d of %d"
                        % (n, shader, num_shaders)))
        # -1 is "no fog"; anything else is an index into the fog lump.
        if fog < -1 or fog >= num_fogs:
            out.append(("surface.fogNum", "surface %d is in fog %d of %d"
                        % (n, fog, num_fogs)))
        if first_vert < 0 or first_vert >= max(num_verts, 1):
            out.append(("surface.firstVert",
                        "surface %d starts at vertex %d of %d"
                        % (n, first_vert, num_verts)))
        elif num < 0 or first_vert + num > num_verts:
            out.append(("surface.numVerts",
                        "surface %d holds vertices %d..%d of %d"
                        % (n, first_vert, first_vert + num, num_verts)))
        if first_index < 0 or first_index >= max(num_indexes, 1):
            out.append(("surface.firstIndex",
                        "surface %d starts at index %d of %d"
                        % (n, first_index, num_indexes)))
        elif count < 0 or first_index + count > num_indexes:
            out.append(("surface.numIndexes",
                        "surface %d holds indices %d..%d of %d"
                        % (n, first_index, first_index + count, num_indexes)))

    for n in range(num_models):
        base = record_at(data, LUMP_MODELS, n)
        first, count = struct.unpack_from("<2i", data,
                                          base + MODEL_FIRSTSURFACE)
        if first < 0 or count < 0 or first + count > num_surfaces:
            out.append(("model.numSurfaces",
                        "model %d owns surfaces %d..%d of %d"
                        % (n, first, first + count, num_surfaces)))

    vofs, vlen = lump_at(data, LUMP_VISIBILITY)
    if vlen:
        if vlen < 8:
            out.append(("vis.length",
                        "the visibility lump is %d bytes and holds no header"
                        % vlen))
        else:
            clusters, cluster_bytes = struct.unpack_from("<2i", data, vofs)
            need = 8 + clusters * cluster_bytes
            if clusters < 0 or cluster_bytes < 0 or vlen < need:
                out.append(("vis.length",
                            "the visibility lump is %d bytes and its own header "
                            "asks for %d" % (vlen, need)))

    aofs, alen = lump_at(data, LUMP_LIGHTARRAY)
    for n in range(alen // 2):
        value = struct.unpack_from("<H", data, aofs + n * 2)[0]
        if value >= num_grid:
            out.append(("lightarray.value",
                        "light grid cell %d names record %d of %d"
                        % (n, value, num_grid)))
            break

    eofs, elen = lump_at(data, LUMP_ENTITIES)
    if elen == 0 or data[eofs + elen - 1] != 0:
        out.append(("entities.terminator",
                    "the entity string does not end inside its own lump"))

    return out


def check_corruptions(failures):
    """--corrupt is only worth having if it corrupts.

    Three claims, and each has been wrong at some point in something shaped like
    this. That the clean map passes the format's own reader, so a red lane is
    about the engine and not about the fixture. That every kind breaks the
    invariant it names - a corruption whose offset arithmetic is off by four
    lands in a neighbouring field, still writes a file, and the engine still
    rejects it, so the lane goes green while testing something else entirely.
    And that nothing else moves: same length, at most eight bytes different.
    """
    clean = build("jkx/smoke")

    for tag, message in validate(clean):
        failures.append("the clean fixture fails its own format check: "
                        "%s: %s" % (tag, message))

    for name, tag, _, _ in CORRUPTIONS:
        broken = corrupt(clean, name)

        if len(broken) != len(clean):
            failures.append("--corrupt %s changed the file length, so it is not "
                            "one field" % name)
            continue

        moved = sum(1 for a, b in zip(clean, broken) if a != b)
        if moved == 0:
            failures.append("--corrupt %s changed nothing" % name)
            continue
        if moved > 8:
            failures.append("--corrupt %s changed %d bytes, which is more than "
                            "one field" % (name, moved))

        tags = [t for t, _ in validate(broken)]
        if tag not in tags:
            failures.append("--corrupt %s should break %s and the format check "
                            "reports %s instead"
                            % (name, tag, ", ".join(tags) or "nothing"))


def check():
    failures = []
    for label, args in (("plain", ("jkx/smoke", None)),
                        ("with water", ("jkx/smoke", None, None, None,
                                        (), (), False, "jkx/water")),
                        ("with a sky and water", ("jkx/smoke", "textures/jkx/sky",
                                                  None, None, (), (), False,
                                                  "jkx/water")),
                        ("with a sky", ("jkx/smoke", "textures/jkx/sky")),
                        ("with a sky and fog",
                         ("jkx/smoke", "textures/jkx/sky", "textures/jkx/fog"))):
        data = build(*args)
        before = len(failures)
        check_surfaces(data, failures)
        if len(failures) != before:
            failures[before:] = ["%s: %s" % (label, f) for f in failures[before:]]

    # Water: the surface's shaderNum has to land on the entry that says water.
    #
    # This is the trap named in build(). A surface pointing at the wrong shader
    # entry is not an error anywhere - the map loads, the face draws, and it
    # draws as whatever that entry is. Every water lane downstream would then be
    # photographing the floor shader and agreeing with itself.
    #
    # This checks the BSP side. The renderer's side is the `surfaceparm water`
    # line in the .shader file, and nothing here can see that; see the note in
    # shaders() for why the two are different fields with the same name.
    for label, sky in (("with water", None), ("with a sky and water", "textures/jkx/sky")):
        data = build("jkx/smoke", sky, None, None, (), (), False, "jkx/water")

        sofs, slen = struct.unpack_from("<2i", data, 8 + LUMP_SURFACES * 8)
        hofs, hlen = struct.unpack_from("<2i", data, 8 + LUMP_SHADERS * 8)
        shader_size = MAX_QPATH + 8

        # The water face is the last surface written.
        last = slen // SURFACE_SIZE - 1
        num = struct.unpack_from("<i", data, sofs + last * SURFACE_SIZE)[0]

        if num < 0 or (num + 1) * shader_size > hlen:
            failures.append("%s: the water surface points at shader %d, which is "
                            "not in the lump" % (label, num))
            continue

        base = hofs + num * shader_size
        name = data[base:base + MAX_QPATH].split(b"\0")[0].decode()
        contents = struct.unpack_from("<i", data, base + MAX_QPATH + 4)[0]

        if not contents & CONTENTS_WATER:
            failures.append("%s: the water surface points at shader %d (%s), "
                            "whose contents are 0x%x and do not include water - "
                            "the renderer decides from exactly this number"
                            % (label, num, name, contents))

    # The lightmap lump against the surface that refers to it.
    #
    # R_LoadLightmaps divides the lump length by one page to get the page count
    # and does not otherwise check it, so a lump that is not a whole number of
    # pages is not an error there - it is a page count that silently disagrees
    # with the surfaces, and the surface either draws a page that was never
    # uploaded or falls back to vertex lighting without saying so.
    lit = build("jkx/lightmapped", lightmap="internal")
    page = LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3
    lofs, llen = struct.unpack_from("<2i", lit, 8 + LUMP_LIGHTMAPS * 8)

    if llen != page:
        failures.append("the lightmap lump is %d bytes, not the one %d byte page "
                        "the floor asks for" % (llen, page))
    elif set(lit[lofs:lofs + llen]) != {LIGHTMAP_GREY}:
        failures.append("the lightmap page is not one flat value, so what reaches "
                        "the screen is not a measurement of the engine's scaling")

    # lightmapNum sits after the two four-byte style arrays, which is why this
    # is 36 rather than 32: getting it wrong reads vertexStyles and every value
    # is a sensible-looking small number.
    LIGHTMAPNUM_AT = 36

    def first_lightmap(bsp):
        at = struct.unpack_from("<2i", bsp, 8 + LUMP_SURFACES * 8)[0]
        return struct.unpack_from("<i", bsp, at + LIGHTMAPNUM_AT)[0]

    if first_lightmap(lit) != 0:
        failures.append("the floor's first lightmapNum is %d, not page zero"
                        % first_lightmap(lit))

    if first_lightmap(build("jkx/smoke")) != LIGHTMAP_BY_VERTEX:
        failures.append("without --lightmap the floor should still be lit by its "
                        "vertices; anything else points at a page that is not there")

    # The external arrangement, which is the opposite of the one above and has to
    # be checked as its own thing: the floor points at page zero AND the lump is
    # empty. Either half alone is a different map. With a page present the loader
    # never looks for a file; with lightmapNum at -3 it never has a page to look
    # for, and in both cases R_LoadLightmaps takes the internal path and the
    # eight-bytes-per-pixel code is not reached.
    ext = build("jkx/lightmapped", lightmap="hdr")
    _, elen = struct.unpack_from("<2i", ext, 8 + LUMP_LIGHTMAPS * 8)

    if elen != 0:
        failures.append("with --lightmap-hdr the lightmap lump is %d bytes and "
                        "should be empty; a lump that is there is the internal "
                        "path, and the external loader never runs" % elen)

    if first_lightmap(ext) != 0:
        failures.append("with --lightmap-hdr the floor's first lightmapNum is %d, "
                        "not page zero - the page count comes from the surfaces "
                        "when the lump is empty, so this IS the count"
                        % first_lightmap(ext))

    # The file the loader will go looking for, checked here rather than only on
    # disk: a header this reader cannot parse comes back as "no external
    # lightmap" and the map is simply unlit, with no error anywhere.
    hdr = hdr_lightmap(4, HDR_LIGHTMAP_VALUE)

    if not hdr.startswith(b"#?RADIANCE\n"):
        failures.append("the generated .hdr does not start with the Radiance magic")

    if b"-Y 4 +X 4\n" not in hdr:
        failures.append("the generated .hdr does not declare -Y 4 +X 4")

    if len(hdr) != hdr.index(b"-Y 4 +X 4\n") + len(b"-Y 4 +X 4\n") + 4 * 4 * 4:
        failures.append("the generated .hdr is not header plus four bytes a pixel, "
                        "so it is not the flat-scanline form a reader falls back to")

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

    check_corruptions(failures)

    for f in failures:
        print("error: %s" % f, file=sys.stderr)
    if failures:
        return 1

    print("test map: %d bytes, %d lumps, every record size and offset checks out"
          % (len(data), HEADER_LUMPS))
    print("%d corruptions, each breaking the one invariant it names"
          % len(CORRUPTIONS))
    return 0


def main(argv):
    args = argv[1:]
    if "--check" in args:
        return check()

    # The list of kinds, so that the shell lane loops over what this file
    # defines rather than over a copy of it. A second copy is a second thing to
    # forget: a kind added here and not there is a check nobody runs, and it
    # looks exactly like a check that passes.
    if "--corrupt-list" in args:
        for name, _, description, _ in CORRUPTIONS:
            print("%s %s" % (name, description))
        return 0

    visible = "jkx/smoke"
    sky = None
    fog = None
    lightmap = None
    prop_specs = []
    npc_specs = []
    cloud = False
    water = None
    damage = None
    path = None
    i = 0
    while i < len(args):
        if args[i] == "--shader" and i + 1 < len(args):
            visible = args[i + 1]
            i += 2
        elif args[i] == "--sky" and i + 1 < len(args):
            sky = args[i + 1]
            i += 2
        elif args[i] == "--fog" and i + 1 < len(args):
            fog = args[i + 1]
            i += 2
        elif args[i] == "--lightmap":
            lightmap = "internal"
            i += 1
        elif args[i] == "--lightmap-hdr":
            lightmap = "hdr"
            i += 1
        elif args[i] == "--cloud":
            cloud = True
            i += 1
        elif args[i] == "--water" and i + 1 < len(args):
            water = args[i + 1]
            i += 2
        elif args[i] == "--corrupt" and i + 1 < len(args):
            damage = args[i + 1]
            if damage not in CORRUPT_KINDS:
                print("unknown corruption: %s\nknown: %s"
                      % (damage, " ".join(CORRUPT_KINDS)), file=sys.stderr)
                return 2
            i += 2
        elif args[i] == "--prop" and i + 1 < len(args):
            # Repeatable. MODEL[:Y[:Z]] - see parse_props.
            prop_specs.append(args[i + 1])
            i += 2
        elif args[i] == "--npc" and i + 1 < len(args):
            # Repeatable. NAME[:Y[:Z]] - see parse_npcs.
            npc_specs.append(args[i + 1])
            i += 2
        elif path is None:
            path = args[i]
            i += 1
        else:
            print("unexpected argument: %s" % args[i], file=sys.stderr)
            return 2

    if path is None:
        print("usage: %s <out.bsp> [--shader NAME] [--sky NAME] [--fog NAME] "
              "[--lightmap] [--lightmap-hdr] [--cloud] [--water NAME] "
              "[--prop MODEL[:Y[:Z]] ...] [--npc NAME[:Y[:Z]] ...] "
              "[--corrupt KIND]\n"
              "       %s --corrupt-list"
              % (argv[0], argv[0]),
              file=sys.stderr)
        return 2

    props = parse_props(prop_specs)
    npcs = parse_npcs(npc_specs)
    data = build(visible, sky, fog, lightmap, props, npcs, cloud, water)
    if damage:
        data = corrupt(data, damage)
    with open(path, "wb") as f:
        f.write(data)

    note = ""
    if lightmap == "internal":
        note = ", lightmap %dx%d at %d" % (LIGHTMAP_SIZE, LIGHTMAP_SIZE, LIGHTMAP_GREY)
    elif lightmap == "hdr":
        # Beside the map and named for it, because that is where the loader
        # looks: maps/<name>/lm_0000.hdr. Not a choice this script makes - a
        # convention it has to match exactly or the map is simply unlit.
        base = os.path.splitext(path)[0]
        os.makedirs(base, exist_ok=True)
        target = os.path.join(base, "lm_0000.hdr")
        with open(target, "wb") as f:
            f.write(hdr_lightmap())
        note = ", external %s %dx%d at %g" % (
            os.path.basename(target), HDR_LIGHTMAP_SIZE, HDR_LIGHTMAP_SIZE,
            HDR_LIGHTMAP_VALUE)

    if damage:
        note += ", CORRUPTED: %s - %s" % (damage, corrupt_description(damage))

    print("%s: %d bytes, shader %s%s%s%s"
          % (path, len(data), visible, ", sky %s" % sky if sky else "",
             note, ", %d prop(s)" % len(props) if props else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
