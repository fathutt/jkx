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
