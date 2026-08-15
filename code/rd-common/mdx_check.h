/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#pragma once

// Does this model file describe itself consistently?
//
// The three model formats - MD3 for props and weapons, MDXM for a Ghoul2 mesh,
// MDXA for its skeleton - all begin with an identifier, a version, and a set of
// count-and-offset pairs, and every loader walks those pairs without ever
// comparing one against the length of the file it read.
//
// The worst of it is one line. R_LoadMDXM takes ofsEnd straight out of the file
// and hands it to the model cache as the size to allocate and copy:
//
//     size = (pinmodel->ofsEnd);
//     ...
//     mdxm = CModelCache->Allocate(size, buffer, ...);
//
// so a .glm whose ofsEnd says two gigabytes copies two gigabytes out of a
// buffer that holds however many bytes the file had. A .glm arrives in a pk3.
//
// Below that, every offset is trusted the same way: R_LoadMDXA reads
// numFrames * numBones * 3 bytes from ofsFrames, R_LoadMDXM walks numLODs from
// ofsLODs and numSurfaces from ofsSurfHierarchy, and none of the four numbers
// in either sentence has been looked at.
//
// And the identifier is read before the file is known to have four bytes:
// R_RegisterMD3 does `ident = *(unsigned *)buf;` on whatever came back.
//
// This is the first pass and it is deliberately the shallow one: the header,
// its top-level offsets, and the arrays their counts imply. Per-surface and
// per-LOD offsets are a second pass and they belong with the loaders, which
// know what the fields mean. The shallow one has to come first, because until
// the top-level arrays are known to be inside the file there is nothing safe to
// walk.
//
// Dependency-free on purpose, like cm_bsp_check and tr_image_tga_decode: it
// takes a pointer and a length, so a test can hand it a few hundred thousand
// malformed models.

#include <stddef.h>

// Answers NULL when the file is one of the three formats and describes itself
// consistently, or a message saying what does not fit.
//
// A file whose identifier is none of the three is NOT an error here - the
// caller has its own message for that, and this is not the place to decide what
// counts as a model. Such a file answers NULL and is left alone.
//
// Nothing is written to data.
const char *MDX_CheckHeader( const unsigned char *data, size_t len );

// The same, and then the second pass: the surface hierarchy and every LOD and
// surface of an MDXM, the bone table and every frame index of an MDXA, every
// surface and every triangle index of an MD3. Where
// the header check asks whether the top-level arrays are inside the file, this
// asks whether the loader can walk what is in them - both formats nest, and
// every level down carries offsets of its own that nothing compared against
// anything.
//
// This is what a caller loading a model wants. MDX_CheckHeader is left public
// because the two are tested separately: the shallow pass has to be able to
// stand on its own, since the deep one assumes what it established.
const char *MDX_CheckModel( const unsigned char *data, size_t len );
