/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026 JKX contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

// Ghoul2's internal interface, single-player edition.
//
// Multiplayer splits Ghoul2 into a public ghoul2/G2.h and this private header;
// single-player never had the split, so the same declarations live scattered
// across the renderer and code/game. This file re-establishes the split on the
// single-player side, which is where phase 2.3 is taking the G2_ sources
// anyway - out of the renderer and into the engine.
//
// It is deliberately NOT a copy of the multiplayer header. Every signature here
// was taken from the definition in our own sources and confirmed by compiling,
// because a transcribed declaration that disagrees with its definition compiles
// happily and then misbehaves - which is the single most expensive failure mode
// in this whole port.
//
// The harness build does not see this file: it includes the fork's own copy,
// because only code/rd-vulkan is synced into that checkout. So this header is
// exercised by exactly one build, the single-player target, and by nothing
// else until the harness is gone.

#pragma once

// Self-sufficient on purpose: the G2_ sources include this before tr_local.h,
// so it cannot rely on anything having been pulled in first. q_shared.h brings
// CGhoul2Info_v and IGhoul2InfoArray with it, via code/game/ghoul2_shared.h.
#include "qcommon/q_shared.h"
#include "qcommon/MiniHeap.h"
#include "ghoul2/G2.h"

class CRagDollUpdateParams;

// Multiplayer introduced a pure-virtual IHeapAllocator and made CMiniHeap
// implement it. Single-player has only the concrete class - which already has
// both methods the interface declares, ResetHeap() and MiniHeapAlloc(int). The
// renderer calls nothing else, so naming is all that is missing: a typedef
// costs nothing, where adding the interface would put a vtable on a memory
// arena for no one's benefit.
typedef CMiniHeap IHeapAllocator;

//hack for smoothing during ugly situations. forgive me.
#define GHOUL2_CRAZY_SMOOTH 0x2000

void Create_Matrix( const float *angle, mdxaBone_t *matrix );

extern mdxaBone_t worldMatrix;
extern mdxaBone_t worldMatrixInv;
