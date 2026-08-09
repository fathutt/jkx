/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Temporary bridge for phase 2.1: let the renderer be written against
// single-player types while it is still compiled against multiplayer ones.
//
// This renderer came from a multiplayer fork and does not build in this tree
// yet, so it is compiled inside a checkout of that fork (see
// tools/devkit/renderer_build_harness.sh). That means every step of the port
// has a problem: the moment a file starts using a single-player name, the only
// build we have stops working - and with it CI, the packaged engine and every
// hardware measurement. Converting the whole renderer in one commit to get it
// back is exactly the "red for weeks" failure this project keeps avoiding.
//
// So the source moves to single-player names one piece at a time, and this
// header supplies those names when the surrounding headers are the
// multiplayer ones. Each entry is a fact about how the two trees differ, and
// each disappears when phase 2 finishes and rd-common/tr_types.h is ours.
//
// RULES for anything added here:
//   - Only names, never behaviour. If the two engines disagree about what a
//     flag *means*, that is a port decision and belongs in the code, not in a
//     macro that hides it.
//   - Guard on the multiplayer name existing, so that once this tree provides
//     the real definition the shim silently stops applying.
//   - Every entry carries the bit values from both sides, because that is the
//     part a reader cannot check without two checkouts open.
//
// DELETE THIS FILE at the end of phase 2. If it is still here afterwards, the
// port did not finish; it just stopped being visible.

#pragma once

// RF_MORELIGHT (SP) and RF_MINLIGHT (MP) are the same flag: bit 0x00001, "always
// have some light (viewmodel, some items)", identical comment in both headers.
// Only the name was changed. Renaming is therefore safe in a way that almost
// nothing else in this header would be.
#if !defined( RF_MORELIGHT ) && defined( RF_MINLIGHT )
	#define RF_MORELIGHT RF_MINLIGHT
#endif

// Flags with NO counterpart on the other side get no entry here.
//
// RF_CAP_FRAMES (SP 0x00400) and RF_G2MINLOD (SP 0x100000) do not exist in
// multiplayer at all, and those bits are taken there by other flags -
// RF_FORCE_ENT_ALPHA and RF_ALPHA_DEPTH. Defining them to anything would be
// inventing behaviour, which is what this header is forbidden to do. Code that
// implements such a flag is wrapped in #ifdef instead, so it compiles away
// under multiplayer headers and appears the moment the types become ours.
// That is the same convention upstream already uses for its own multiplayer-
// only flags (RF_NOLOD, RDF_AUTOMAP and the rest).
