/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The single-player export surface: the entry points that exist in both
// renderers with different shapes, and the storage behind the ones the client
// writes to through the table.
//
// This file used to hold something else as well - twenty-five stubs, one per
// single-player feature this renderer did not have, each warning once and
// returning something harmless. `grep -c JKX_UNPORTED` counted them, and the
// count is now zero: the screen wipe, the goggles, the scissor, the save-game
// thumbnail, the scripted fog, the model bounds and the Ghoul2 index API are
// all real, and the refraction knobs point at the refraction pass. What is left
// here is adaptation, and every place that loses something in the adapting says
// so above itself.

#include "tr_local.h"


#include "tr_cache.h"
#include "tr_sp_exports.h"

// Defined in tr_init.cpp beside the rest of the level-load hooks, and not in a
// header on either side.
extern void		C_LevelLoadBegin( const char *psMapName, ForceReload_e eForceReload );
extern void		C_SetAllowScreenDissolve( qboolean allow );

// The weather system's own header declares only the part multiplayer uses.
extern bool		R_GetWindGusting( vec3_t atPoint );
extern bool		R_GetWindVector( vec3_t windVector, vec3_t atPoint );
extern bool		R_IsShaking( vec3_t pos );

// ---------------------------------------------------------------------------
// Adapters
// ---------------------------------------------------------------------------

// Multiplayer's level-load hook has no screen wipe to say anything about, so it
// takes one argument fewer. The flag is handed to the renderer separately and
// read at the end of the load, which is where the wipe starts.
void RE_SP_RegisterMedia_LevelLoadBegin( const char *psMapName, ForceReload_e eForceReload, qboolean bAllowScreenDissolve )
{
	C_SetAllowScreenDissolve( bAllowScreenDissolve );
	C_LevelLoadBegin( psMapName, eForceReload );
}

// Single-player adds one polygon at a time; multiplayer batches. Same call.
void RE_SP_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts )
{
	RE_AddPolyToScene( hShader, numVerts, verts, 1 );
}

// Single-player ignores the result. Multiplayer returns whether the tag was
// found, which is information single-player's callers never had.
void RE_SP_LerpTag( orientation_t *tag, qhandle_t handle, int startFrame, int endFrame,
	float frac, const char *tagName )
{
	R_LerpTag( tag, handle, startFrame, endFrame, frac, tagName );
}

// Multiplayer's version takes a visibility mask to test against; single-player
// has no caller that supplies one.
qboolean RE_SP_inPVS( vec3_t p1, vec3_t p2 )
{
	return R_inPVS( p1, p2, NULL );
}

// The weather system asks about a point in single-player and about the world in
// multiplayer, and that is a real difference rather than a signature one:
// single-player's wind and shaking are per-zone. These used to drop the point on
// the floor and answer globally, which is right in a map with one zone and wrong
// in every other. The point goes through now; see the local zone list in
// tr_WorldEffects.cpp.
bool RE_SP_GetWindGusting( vec3_t atPoint )
{
	return R_GetWindGusting( atPoint );
}

bool RE_SP_GetWindVector( vec3_t windVector, vec3_t atPoint )
{
	return R_GetWindVector( windVector, atPoint );
}

bool RE_SP_IsShaking( vec3_t pos )
{
	return R_IsShaking( pos );
}

// ---------------------------------------------------------------------------
// The refraction knobs
// ---------------------------------------------------------------------------
//
// The client does not call these to read a value; it takes the address and
// writes through it, which is why the export is a getter and the storage has to
// outlive the call. cgi_R_SetRefractProp is the only thing that writes them,
// nothing in the shipping game calls it, and the comment beside it in the game
// code says what it is for: "primarily for mod authors".
//
// They were written for a screen-wide stencil-and-stretch fill that this
// renderer does not have. What it has instead is a per-surface refraction pass,
// so the two that describe the effect carry over and the two that describe how
// the old one was staged do not:
//
//   alpha    how much of the refracted image replaces the surface. Carries over
//            exactly.
//   stretch  was an override for the old effect's automatic wobble. Here it
//            scales how far the ray is bent, which is the same knob in spirit:
//            zero still means "the renderer decides".
//   prePost  chose whether the screen was captured before or after the post
//            phase. In this renderer the extract always happens between the
//            main pass and bloom, so the answer is always "before" and there is
//            nothing to switch. Kept so writes to it stay harmless.
//   negate   the inverse blend. Carries over as a pipeline state.
//
float		tr_distortionAlpha = 1.0f;		// opaque
float		tr_distortionStretch = 0.0f;	// no override
qboolean	tr_distortionPrePost = qfalse;
qboolean	tr_distortionNegate = qfalse;

float *get_tr_distortionAlpha( void )
{
	return &tr_distortionAlpha;
}

float *get_tr_distortionStretch( void )
{
	return &tr_distortionStretch;
}

qboolean *get_tr_distortionPrePost( void )
{
	return &tr_distortionPrePost;
}

qboolean *get_tr_distortionNegate( void )
{
	return &tr_distortionNegate;
}

// Single-player's second string reader advances the caller's pointer instead of
// reporting how far it moved. rd-vanilla only defines it in JK2 mode and
// forwards to the other one; here it is the same forward, without the mode.
unsigned int AnyLanguage_ReadCharFromString2( char **psText, qboolean *pbIsTrailingPunctuation )
{
	int iAdvanceCount = 0;
	unsigned int c = AnyLanguage_ReadCharFromString( *psText, &iAdvanceCount, pbIsTrailingPunctuation );

	*psText += iAdvanceCount;
	return c;
}

