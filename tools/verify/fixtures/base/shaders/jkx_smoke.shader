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
