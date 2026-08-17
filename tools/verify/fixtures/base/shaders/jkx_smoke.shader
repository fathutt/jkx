// The one shader R_Init needs to exist before it will finish.
jkx/smoke
{
	{
		map $whiteimage
	}
}

// The console background. Without one the engine draws its default - which in
// a fixture with no assets is white - and white text on it is invisible, which
// cost an hour of looking for a clipping bug that was not there.
console
{
	{
		map $whiteimage
		rgbGen const ( 0.05 0.05 0.08 )
	}
}

// cl_main.cpp and ui_main.cpp both register "white", and the client fills the
// black bars beside the fitted frame with it. Without this definition it
// resolves to the default shader, which ignores the colour RE_StretchPic asks
// for - so the bars came out white, and a menu asking for a transparent
// background got an opaque one. The fixture was measuring the engine's
// behaviour when a shader it needs is absent, which is not the case worth
// measuring.
//
// rgbGen vertex is the whole point: it is what lets the caller's colour
// through.
white
{
	{
		map $whiteimage
		rgbGen vertex
	}
}

// A sky, so that something here draws one.
//
// The six faces are generated - see make_test_sky.py - and each carries its own
// colour plus a marker, because the question the bench asks about the sky is not
// "did anything draw" but "which face, which way up". A flat blue sky answers
// neither, and the sky is about to be moved onto a cubemap, which is a change to
// exactly those two things.
//
// skyParms takes the box prefix, then a cloud height, then an inner box. 512 is
// the height vanilla uses; "-" for the inner box means there is not one.
textures/jkx/sky
{
	surfaceparm sky
	surfaceparm nolightmap
	surfaceparm noimpact
	skyParms textures/jkx/sky 512 -
}

// The floor of the sky room, and the only reason it is a file rather than
// $whiteimage is its size: 4096 texels wide, which is past the 2048 the
// renderer used to clamp every texture to. Under the old ceiling the loader
// halved it on the way in and said nothing about it; imagelist is where the
// difference shows, and the run requires it.
//
// nopicmip so that a picmip setting cannot quietly halve it back under the old
// ceiling and turn the check into a check of nothing.
//
// White on purpose. It is the floor beneath the sky checks, and a texture that
// changed the picture would be testing two things at once.
textures/jkx/wide
{
	nopicmip
	{
		map textures/jkx/wide
	}
}

// The map's fog. Global rather than a brush: the generated map declares it with
// brushNum -1, which R_LoadFogs accepts without looking at any geometry.
//
// It is here because RB_FogPass had never run in a headless test - the generated
// map had no fogs, and the retail maps are not in this repository - so a second
// blended pass over every fogged surface, its shader permutation and its texture
// coordinate generation were all unexecuted.
//
// fogParms is the colour and the distance at which the fog becomes opaque. The
// colour is one nothing else in the fixture uses, so a fogged surface is
// recognisable from its colour alone.
textures/jkx/fog
{
	surfaceparm fog
	surfaceparm nonsolid
	fogParms ( 0.9 0.1 0.9 ) 512
}

// The floor, lit by a lightmap and by nothing else.
//
// One stage, and it is the lightmap itself: what lands on the screen is the
// lightmap texel with whatever scaling the engine applies on the way, and
// nothing from a texture, a vertex colour or a light grid is mixed in to
// confuse the reading. That is what makes the map's brightness a number rather
// than an impression, and the numbers it gives are exact: with the fixture's
// page at 32, r_mapOverBrightBits 0, 1, 2 and 3 put 32, 64, 128 and 255 on the
// screen.
//
// It is used only by the lightmap lane, because the ordinary fixture's floor is
// white on purpose - a flat colour is what makes the pixel checks exact.
jkx/lightmapped
{
	{
		map $lightmap
		rgbGen identity
	}
}

// The one material in this fixture whose colour depends on a normal.
//
// Every other shader here is rgbGen const, which is deliberate - a flat colour
// is what makes a pixel check say something exact - and the cost of it is that
// this bench could not see a lighting change at all. Not a normal, not a light,
// not a shading term. A weld of model normals was landed and measured against a
// number that turned out to be the noise floor of an animated character, and
// the reason no frame could settle it is this line missing.
//
// rgbGen lightingDiffuse asks the entity lighting for the colour, which comes
// from the light grid and from the normal at each vertex. The grid in this
// fixture is deliberately red, so a model under this shader is red and its
// SHADE varies across the surface - and the variation is the measurement.
//
// Single-sided, and that is a change. It used to say `cull none`, and so did
// every other material here that draws geometry, and between them they hid the
// fact that this whole fixture was built inside out: every generated model and
// the map's floor were wound the wrong way round, so their front faces pointed
// away from the camera. Nothing could see it, because nothing was culled.
//
// The floor is what gave it away - it is single-sided and it was simply never
// drawn, in any run, since the map generator was written. What was being
// checked instead was a white shape near the bottom of the frame that is not
// the floor.
//
// So the windings are fixed and the two-sidedness is gone with them. Keeping it
// would mean this stays unmeasurable, and it is not a detail: a tangent basis
// comes out of the winding and the texture coordinates together, so an inside
// out model has its handedness inverted, and this is the model the normal
// mapping work gets measured on.
//
// What this costs: the lane now depends on testmodel leaving the model facing
// the camera, which is what its default yaw does. If that ever changes, these
// two lanes go dark rather than wrong, and that is the message.
textures/jkx/lit
{
	{
		map $whiteimage
		rgbGen lightingDiffuse
	}
}

// The two-frame model from make_test_md3.py, in a colour nothing else in the
// fixture uses. It has to be unmistakable: the check on it is that the square
// is somewhere else in the second frame, and a white square against a white
// floor cannot answer that.
//
// Single-sided for the reason written out above textures/jkx/lit.
textures/jkx/anim
{
	{
		map $whiteimage
		rgbGen const ( 0.0 1.0 1.0 )
	}
}

// A material with a normal map on it, which this fixture had none of.
//
// That absence was not cosmetic. A shader permutation is generated from what a
// material asks for, so with no normalMap anywhere the permutation that reads
// the normal map, the physical map and the lighting environment - descriptor
// set five - was never generated, and the code that has to bind set five was
// never run. It was wrong: the DrawItem path in tr_backend.cpp bound sets zero
// to four and pushed nothing into five, so any mesh drawn that way with a
// normal map ran a pipeline that reads an unbound set. On lavapipe that is a
// segmentation fault in a rasteriser worker; on hardware it is undefined.
//
// It took a retail model with real maps to produce the first such draw. This
// material is what lets the bench produce one without any retail data at all.
//
// The maps are flat - see make_test_material.py for why - so this does not
// measure shading. It measures that the draw happens, which is the thing that
// was not happening.
//
// rgbGen lightingDiffuse rather than a constant, because a material that takes
// no light does not go down the physically-based path at all.
jkx/pbr_body
{
	{
		map $whiteimage
		normalMap textures/jkx/jkx_flat_n
		rmoMap textures/jkx/jkx_flat_rmo
		rgbGen lightingDiffuse
	}
}

// The menu model, lit rather than painted.
//
// Every other material in this fixture that draws a model is rgbGen const,
// which is what makes the pixel checks exact - and it is also why the bench
// could not see the thing a player sees first. A model in a menu is not lit by
// the world: R_SetupEntityLighting takes the RDF_NOWORLDMODEL branch, where the
// light is two fixed numbers rather than the light grid, and nothing in this
// fixture had ever gone down that branch and landed on the screen.
//
// White albedo on purpose. `map $whiteimage` with rgbGen lightingDiffuse puts
// the LIGHT VALUE itself on the screen - albedo one, so the pixel is the sum
// the shader computed - which turns "the menu looks blown out" into a
// histogram. See tga_grey_levels.py for what is done with it.
jkx/menu_lit
{
	{
		map $whiteimage
		rgbGen lightingDiffuse
	}
}

// The player character's skin, in a colour nothing else here is.
//
// It used to be jkx/smoke, which is white - and so is almost everything else in
// this fixture, so a model drawn with the WRONG skin looked exactly like a
// model drawn with the right one. That matters because of a defect this bench
// could not see: G_SetSkin hands G2API_SetSkin a configstring index where the
// renderer expects a skin handle (tr_ghoul2.cpp reads mCustomSkin through
// R_GetSkinByHandle), and the two agree only while their counters happen to.
//
// Green here and red in the alternate skin, so "the wrong skin" is a colour and
// not a shrug.
jkx/skin_body
{
	{
		map $whiteimage
		rgbGen const ( 0.0 1.0 0.0 )
	}
}

jkx/skin_body_alt
{
	{
		map $whiteimage
		rgbGen const ( 1.0 0.0 0.0 )
	}
}

// What the .glm itself names on its one surface, and a third colour on purpose.
//
// With this blue, the default skin green and the alternate skin red, one
// screenshot answers a three-way question instead of a yes/no: blue means the
// skin was never applied, green means it was applied correctly, and red or
// anything else means a skin was applied and it was the wrong one. The fixture
// used to have the model, the skin and half the world all drawn in white, so
// none of those three could be told from the others.
jkx/glm_baked
{
	{
		map $whiteimage
		rgbGen const ( 0.0 0.0 1.0 )
	}
}

// Transparency, and the four materials that make it a number.
//
// Blending had never been measured here. The fixture draws opaque squares and
// one fogged pass, so blendFunc, its pipeline state and the order surfaces are
// drawn in were all exercised only by whatever the interface happens to do -
// which is 2D, unsorted, and says nothing about a translucent surface in a
// world.
//
// The arrangement is a backdrop of a known colour with three small squares in
// front of it at different heights, so each composite is independent and each
// is arithmetic rather than an impression. What each one should be is written
// beside it; what it MEASURES is in smoke_headless.sh, because the byte a
// constant colour turns into is the engine's business and is read off the
// screen rather than assumed.
//
// Nothing here is two-sided and nothing moves: the squares are built with
// --flat, one frame and no shift, because a blend result is only exact if both
// surfaces are in the same place in every frame.

// The thing behind. Opaque, mid grey, and large enough that all three squares
// in front of it are inside it on screen - which is the whole point: a
// translucent surface blended against the sky or the floor would be measuring
// where it is rather than how it blends.
jkx/trans_backdrop
{
	{
		map $whiteimage
		rgbGen const ( 0.4 0.4 0.4 )
	}
}

// dst + src. Over the backdrop this is grey plus a quarter, and it is the one
// of the three that can saturate - which is why the backdrop is a half and not
// more.
jkx/trans_add
{
	{
		map $whiteimage
		blendFunc GL_ONE GL_ONE
		rgbGen const ( 0.25 0.25 0.25 )
	}
}

// src * a + dst * (1 - a). White at a quarter alpha over the backdrop.
//
// alphaGen const rather than an alpha channel, because $whiteimage has an
// alpha of one and a blend that always reads one is not a blend.
jkx/trans_blend
{
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen const ( 1.0 1.0 1.0 )
		alphaGen const 0.25
	}
}

// dst * src, which is the one that cannot brighten anything. Half of the
// backdrop.
jkx/trans_filter
{
	{
		map $whiteimage
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen const ( 0.5 0.5 0.5 )
	}
}

// Texture coordinates, and the six keywords that move them.
//
// Not one tcMod had ever executed on this bench, and the reason is the same
// shape as every other blind spot here: every texture the fixture draws is
// FLAT. Moving texture coordinates across a flat colour changes nothing, so a
// fixture made of flat textures cannot see a tcMod whatever it does.
//
// So these five draw a texture with an edge in it - half black, half one
// colour, split down the middle in u; see write_half_tga in
// make_test_material.py. The count of that colour on the square is then a
// number that moves when the coordinates move.
//
// One colour per material, so a count of a colour is a count of a square and no
// mask is needed. What each one is checked against is in smoke_headless.sh, and
// the shape of it matters: the reference square below is the NOISE FLOOR. It
// has no tcMod at all, so if its count changes between the two shots then the
// whole frame is moving and none of the other numbers mean anything. This
// project has twice announced an effect that turned out to be its own noise,
// and this is what that lesson looks like built into a fixture.

// No tcMod. The control, and the thing the static mods are compared against.
jkx/tc_ref
{
	{
		map textures/jkx/jkx_tc_ref
	}
}

// Along u, which is the axis the texture is split on. A scroll along v would
// move the count not at all - a distinction this lane can make and a flat
// texture could not.
//
// Every rate in this block is SLOW, and that is the whole lesson of building
// this lane.
//
// A tcMod is periodic and the lane samples it at three moments. Picking a rate
// and hoping the sampling interval does not divide its period does not work,
// and it does not work in the most confusing possible way: the same scroll
// measured a hundred pixels of movement in one run and one pixel in the next,
// at the same rate, on the same binary. The engine's clock does not advance in
// step with the frame counter, so the interval is not the same interval twice.
//
// What does work is making the PERIOD long compared with the whole sampling
// window. At 0.05 per second the scroll takes twenty seconds to come back to
// itself and the window is three or four, so it can only move in one direction
// while the lane is watching. Same for the stretch at 0.07 hertz. The rotation
// at 37 degrees a second takes ten seconds round and gets the same treatment.
jkx/tc_scroll
{
	{
		map textures/jkx/jkx_tc_scroll
		tcMod scroll 0.05 0
	}
}

jkx/tc_rotate
{
	{
		map textures/jkx/jkx_tc_rotate
		tcMod rotate 37
	}
}

jkx/tc_stretch
{
	{
		map textures/jkx/jkx_tc_stretch
		tcMod stretch sin 1 0.5 0 0.07
	}
}

// The one that does not animate. It is here because a static mod fails
// differently from a moving one: nothing about it changes over time, so the
// only way to see it is to compare it against the reference square in the SAME
// frame. Two texels across instead of one, so half as much colour.
jkx/tc_scale
{
	{
		map textures/jkx/jkx_tc_scale
		tcMod scale 2 2
	}
}

// Geometry that moves, and the four keywords that move it.
//
// deformVertexes had never run here either, and for a different reason from
// tcMod: nothing in this fixture asked for one. The keywords are parsed, the
// permutations exist, and no material named a single one - so RB_DeformTessGeometry
// and every branch inside it went unexecuted, in a renderer where a deform is
// what makes a flag flap, a plant sway and a force effect bulge.
//
// Flat colours here rather than textures, because a deform moves VERTICES: what
// changes is the shape on screen, so a count of the colour and where its centre
// is are the two things worth measuring. One colour per material for the same
// reason as the texture-coordinate block, and none of them red-dominant for the
// same reason again.
//
// Slow, and every rate here is chosen the same way the tcMod rates were: the
// period is long compared with the whole sampling window, so the deform can only
// move one way while the lane is watching.
//
// The constants are fifths and not halves, and that is not a style. rgbGen const
// multiplies by 255 and truncates, so 0.5 is 127 and not 128 - the first run of
// this lane asked for three colours that were each one off what the engine put
// on the screen, found none of them, and reported three squares as not drawn.
// 0.4 is 102 exactly, 0.8 is 204 exactly, 1.0 is 255 exactly.

// No deform. The noise floor, and it is the same argument as the other control:
// if this square moves or changes size, the frame is drifting and none of the
// three below means anything.
jkx/df_ref
{
	{
		map $whiteimage
		rgbGen const ( 0.0 1.0 0.4 )
	}
}

// Vertices pushed along their normals by a wave across the surface. The square
// stops being flat, so its outline on screen changes size.
jkx/df_wave
{
	deformVertexes wave 100 sin 0 12 0 0.06
	{
		map $whiteimage
		rgbGen const ( 0.0 0.4 1.0 )
	}
}

// The whole surface translated, rigidly. Its pixel count is the same number
// wherever it goes, which is why this one is measured by where its centre is
// rather than by how much of it there is.
jkx/df_move
{
	deformVertexes move 0 0 24 sin 0 1 0 0.07
	{
		map $whiteimage
		rgbGen const ( 0.4 0.0 1.0 )
	}
}

// A bulge, which is a wave along the texture's s axis rather than across the
// surface. Width, height, speed.
//
// This square is NOT DRAWN AT ALL on the vertex-buffer path, and that is the
// open defect this lane is currently reporting. It is not displacement carrying
// it off screen: with a height of zero, which cannot move anything, the square
// is still absent. So the draw itself is being skipped, and the shape of that
// is familiar - a pipeline that does not exist makes vk_bind_pipeline skip the
// draw in silence, which is exactly what hid the sky cubemap for months. It
// draws normally on the batch path, where the deform is done on the processor.
jkx/df_bulge
{
	deformVertexes bulge 1 4 0.1
	{
		map $whiteimage
		rgbGen const ( 0.0 0.8 0.4 )
	}
}
