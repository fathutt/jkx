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

// The two-frame model from make_test_md3.py, in a colour nothing else in the
// fixture uses. It has to be unmistakable: the check on it is that the square
// is somewhere else in the second frame, and a white square against a white
// floor cannot answer that.
textures/jkx/anim
{
	// Two-sided, so that which way the square happens to face after testmodel's
	// yaw is not part of what this measures.
	cull none
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
