/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Multiplayer calls this file G2_gore.h; single-player calls the same contents
// ghoul2_gore.h. The two headers declare the same things - CGoreSet,
// SGoreSurface, GoreTextureCoordinates, CRagDollParams, CRagDollUpdateParams -
// so the difference really is the name.
//
// Forwarding rather than renaming the includes, because the renderer's sources
// are still compiled inside the multiplayer fork by the harness, where only the
// multiplayer name exists. One include line that works in both places is worth
// more than a guard in seven files. Delete this when the harness is gone and
// the sources can say ghoul2_gore.h directly.

#pragma once

#include "ghoul2/ghoul2_gore.h"
