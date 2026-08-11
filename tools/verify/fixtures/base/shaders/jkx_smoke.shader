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

// NOT called "white", deliberately, and the name is the whole point.
//
// cl_main.cpp registers cls.whiteShader = RegisterShader("white"), and the
// client fills the black bars beside the fitted frame with it. Two things fall
// out of that, both found in the first hour this fixture was checked for
// geometry rather than for colour count, and both still open:
//
//  1. With no "white" definition it resolves to the default shader, which
//     ignores the colour RE_StretchPic asks for - so the letterbox comes out
//     white instead of black. That the engine needs a shader from the retail
//     game to draw its own letterbox is a dependency it should not have; see
//     docs/Backlog.md section 10.
//
//  2. Defining one - which every retail install has - collapses this menu to a
//     seventy-pixel square at the frame's origin, deterministically, at 16:9
//     and at 32:9 alike. Renaming this block to "white" reproduces it in one
//     run. That is a defect in the two-space work, not in the fixture.
//
// Kept under a name nobody registers, so the smoke test measures the engine
// rather than the second bug. docs/Backlog.md section 14.
jkx/notwhite
{
	{
		map $whiteimage
		rgbGen vertex
	}
}
