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
