/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The single-player export surface.
//
// Two different things live here, and they are different on purpose.
//
// ADAPTERS. Single-player and multiplayer both have the entry point, with
// different shapes. Adapting is honest and the loss, if any, is named in the
// comment above each one.
//
// NOT PORTED YET. Single-player has the entry point and this renderer does not
// implement it at all, because it grew up as a multiplayer renderer and these
// are features multiplayer does not have: the screen dissolve, the goggles
// overlay, the scissor rectangle, the save-game thumbnail path, the Ghoul2
// index-based animation calls. They are stubs that warn once and return
// something harmless.
//
// A stub is not a port. The point of putting them all in one file, each with
// the same shape, is that the list is countable: `grep -c JKX_UNPORTED` here is
// how much of single-player the Vulkan renderer still does not do, and that
// number has to reach zero before this renderer can replace rd-vanilla.
// docs/Phase2-BringUp.md carries the same list with what each one needs.
//
// This file only exists in the single-player build. Under the harness the
// whole thing is empty.

#include "tr_local.h"

#ifdef JKX_SP_FIELDS

#include "tr_cache.h"
#include "tr_sp_exports.h"

// Defined in tr_init.cpp beside the rest of the level-load hooks, and not in a
// header on either side.
extern void		C_LevelLoadBegin( const char *psMapName, ForceReload_e eForceReload );

// The weather system's own header declares only the part multiplayer uses.
extern bool		R_GetWindGusting( void );
extern bool		R_GetWindVector( vec3_t windVector );
extern bool		R_IsShaking( void );

// Warn once and carry on. Returning a harmless value rather than calling
// ri.Error keeps the engine running through a missing feature, which is what
// makes it possible to see the next one - and every one of these prints, so
// nothing here is silent.
#define JKX_UNPORTED( what ) \
	do { \
		static qboolean warned = qfalse; \
		if ( !warned ) { \
			warned = qtrue; \
			ri.Printf( PRINT_WARNING, "rd-vulkan: " what " is not ported from rd-vanilla yet\n" ); \
		} \
	} while ( 0 )

// ---------------------------------------------------------------------------
// Adapters
// ---------------------------------------------------------------------------

// Multiplayer dropped the screen dissolve, so its level-load hook has nothing
// to say about one. The flag is dropped here rather than silently ignored
// somewhere deeper; it starts being used again when the dissolve is ported.
void RE_SP_RegisterMedia_LevelLoadBegin( const char *psMapName, ForceReload_e eForceReload, qboolean bAllowScreenDissolve )
{
	(void)bAllowScreenDissolve;
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
// multiplayer, which is a real difference: single-player's wind and shaking are
// per-zone. Until the zone lookup is ported these answer for the world, so the
// answer is right in a single-zone map and coarse in a multi-zone one.
bool RE_SP_GetWindGusting( vec3_t atPoint )
{
	(void)atPoint;
	return R_GetWindGusting();
}

bool RE_SP_GetWindVector( vec3_t windVector, vec3_t atPoint )
{
	(void)atPoint;
	return R_GetWindVector( windVector );
}

bool RE_SP_IsShaking( vec3_t pos )
{
	(void)pos;
	return R_IsShaking();
}

// ---------------------------------------------------------------------------
// Not ported yet
// ---------------------------------------------------------------------------

int RE_GetAnimationCFG( const char *psCFGFilename, char *psDest, int iDestSize )
{
	JKX_UNPORTED( "GetAnimationCFG" );
	(void)psCFGFilename;
	if ( psDest && iDestSize > 0 )
		psDest[0] = '\0';
	return 0;
}

qboolean RE_GetLighting( const vec3_t org, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir )
{
	JKX_UNPORTED( "GetLighting" );
	(void)org;
	VectorClear( ambientLight );
	VectorClear( directedLight );
	VectorSet( lightDir, 0.0f, 0.0f, 1.0f );
	return qfalse;
}

void RE_LAGoggles( void )
{
	JKX_UNPORTED( "LAGoggles" );
}

void RE_Scissor( float x, float y, float w, float h )
{
	JKX_UNPORTED( "Scissor" );
	(void)x; (void)y; (void)w; (void)h;
}

qboolean RE_InitDissolve( qboolean bForceCircularExtroWipe )
{
	JKX_UNPORTED( "InitDissolve" );
	(void)bForceCircularExtroWipe;
	return qfalse;
}

// qfalse means "the dissolve is finished", which is what the caller does with a
// dissolve that never started.
qboolean RE_ProcessDissolve( void )
{
	JKX_UNPORTED( "ProcessDissolve" );
	return qfalse;
}

void RE_GetScreenShot( byte *data, int w, int h )
{
	JKX_UNPORTED( "GetScreenShot" );
	if ( data )
		Com_Memset( data, 0, (size_t)w * (size_t)h * 3 );
}

byte *RE_TempRawImage_ReadFromFile( const char *psLocalFilename, int *piWidth, int *piHeight,
	byte *pbReSampleBuffer, qboolean qbVertFlip )
{
	JKX_UNPORTED( "TempRawImage_ReadFromFile" );
	(void)psLocalFilename; (void)pbReSampleBuffer; (void)qbVertFlip;
	if ( piWidth ) *piWidth = 0;
	if ( piHeight ) *piHeight = 0;
	return NULL;
}

void RE_TempRawImage_CleanUp( void )
{
	JKX_UNPORTED( "TempRawImage_CleanUp" );
}

void RE_GetModelBounds( refEntity_t *refEnt, vec3_t bounds1, vec3_t bounds2 )
{
	JKX_UNPORTED( "GetModelBounds" );
	(void)refEnt;
	VectorClear( bounds1 );
	VectorClear( bounds2 );
}

void R_ClearStuffToStopGhoul2CrashingThings( void )
{
	JKX_UNPORTED( "R_ClearStuffToStopGhoul2CrashingThings" );
}

bool RE_SetTempGlobalFogColor( vec3_t color )
{
	JKX_UNPORTED( "SetTempGlobalFogColor" );
	(void)color;
	return false;
}

// The distortion cvars the client reads straight out of the renderer. Static
// storage rather than NULL: the client dereferences these without checking.
float *get_tr_distortionAlpha( void )
{
	JKX_UNPORTED( "tr_distortionAlpha" );
	static float alpha = 1.0f;
	return &alpha;
}

float *get_tr_distortionStretch( void )
{
	JKX_UNPORTED( "tr_distortionStretch" );
	static float stretch = 0.0f;
	return &stretch;
}

qboolean *get_tr_distortionPrePost( void )
{
	JKX_UNPORTED( "tr_distortionPrePost" );
	static qboolean prePost = qfalse;
	return &prePost;
}

qboolean *get_tr_distortionNegate( void )
{
	JKX_UNPORTED( "tr_distortionNegate" );
	static qboolean negate = qfalse;
	return &negate;
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

// The Ghoul2 calls that take a bone or animation index instead of a name.
// Single-player's Ghoul2 has them and multiplayer's does not, so they arrive
// with the rest of Ghoul2 in 2.3 rather than one at a time here.
void G2API_AnimateG2Models( CGhoul2Info_v &ghoul2, int AcurrentTime, CRagDollUpdateParams *params )
{
	JKX_UNPORTED( "G2API_AnimateG2Models" );
	(void)ghoul2; (void)AcurrentTime; (void)params;
}

void G2API_DetachEnt( int *boltInfo )
{
	JKX_UNPORTED( "G2API_DetachEnt" );
	(void)boltInfo;
}

char *G2API_GetAnimFileInternalNameIndex( qhandle_t modelIndex )
{
	JKX_UNPORTED( "G2API_GetAnimFileInternalNameIndex" );
	(void)modelIndex;
	return NULL;
}

int G2API_GetAnimIndex( CGhoul2Info *ghlInfo )
{
	JKX_UNPORTED( "G2API_GetAnimIndex" );
	(void)ghlInfo;
	return 0;
}

qboolean G2API_GetAnimRangeIndex( CGhoul2Info *ghlInfo, const int boneIndex, int *startFrame, int *endFrame )
{
	JKX_UNPORTED( "G2API_GetAnimRangeIndex" );
	(void)ghlInfo; (void)boneIndex;
	if ( startFrame ) *startFrame = 0;
	if ( endFrame ) *endFrame = 0;
	return qfalse;
}

qboolean G2API_GetBoneAnimIndex( CGhoul2Info *ghlInfo, const int iBoneIndex, const int AcurrentTime,
	float *currentFrame, int *startFrame, int *endFrame, int *flags, float *animSpeed, int *ignore )
{
	JKX_UNPORTED( "G2API_GetBoneAnimIndex" );
	(void)ghlInfo; (void)iBoneIndex; (void)AcurrentTime; (void)ignore;
	if ( currentFrame ) *currentFrame = 0.0f;
	if ( startFrame ) *startFrame = 0;
	if ( endFrame ) *endFrame = 0;
	if ( flags ) *flags = 0;
	if ( animSpeed ) *animSpeed = 0.0f;
	return qfalse;
}

qboolean G2API_PauseBoneAnimIndex( CGhoul2Info *ghlInfo, const int boneIndex, const int AcurrentTime )
{
	JKX_UNPORTED( "G2API_PauseBoneAnimIndex" );
	(void)ghlInfo; (void)boneIndex; (void)AcurrentTime;
	return qfalse;
}

qboolean G2API_SetAnimIndex( CGhoul2Info *ghlInfo, const int index )
{
	JKX_UNPORTED( "G2API_SetAnimIndex" );
	(void)ghlInfo; (void)index;
	return qfalse;
}

#endif // JKX_SP_FIELDS
