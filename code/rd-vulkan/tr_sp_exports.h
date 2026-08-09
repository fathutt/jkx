/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Declarations for tr_sp_exports.cpp - see the comment at the top of that file
// for what belongs there and what does not. Only the export table includes
// this.

#pragma once


// Adapters
void			RE_SP_RegisterMedia_LevelLoadBegin( const char *psMapName, ForceReload_e eForceReload, qboolean bAllowScreenDissolve );
void			RE_SP_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts );
void			RE_SP_LerpTag( orientation_t *tag, qhandle_t handle, int startFrame, int endFrame, float frac, const char *tagName );
qboolean		RE_SP_inPVS( vec3_t p1, vec3_t p2 );
bool			RE_SP_GetWindGusting( vec3_t atPoint );
bool			RE_SP_GetWindVector( vec3_t windVector, vec3_t atPoint );
bool			RE_SP_IsShaking( vec3_t pos );

// Not ported yet
int				RE_GetAnimationCFG( const char *psCFGFilename, char *psDest, int iDestSize );
qboolean		RE_GetLighting( const vec3_t org, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir );
void			RE_LAGoggles( void );
void			RE_Scissor( float x, float y, float w, float h );
qboolean		RE_InitDissolve( qboolean bForceCircularExtroWipe );
qboolean		RE_ProcessDissolve( void );
void			RE_GetScreenShot( byte *data, int w, int h );
byte *			RE_TempRawImage_ReadFromFile( const char *psLocalFilename, int *piWidth, int *piHeight, byte *pbReSampleBuffer, qboolean qbVertFlip );
void			RE_TempRawImage_CleanUp( void );
void			RE_GetModelBounds( refEntity_t *refEnt, vec3_t bounds1, vec3_t bounds2 );
void			R_ClearStuffToStopGhoul2CrashingThings( void );
bool			RE_SetTempGlobalFogColor( vec3_t color );
float *			get_tr_distortionAlpha( void );
float *			get_tr_distortionStretch( void );
qboolean *		get_tr_distortionPrePost( void );
qboolean *		get_tr_distortionNegate( void );
unsigned int	AnyLanguage_ReadCharFromString2( char **psText, qboolean *pbIsTrailingPunctuation );

void			G2API_AnimateG2Models( CGhoul2Info_v &ghoul2, int AcurrentTime, CRagDollUpdateParams *params );
void			G2API_DetachEnt( int *boltInfo );
char *			G2API_GetAnimFileInternalNameIndex( qhandle_t modelIndex );
int				G2API_GetAnimIndex( CGhoul2Info *ghlInfo );
qboolean		G2API_GetAnimRangeIndex( CGhoul2Info *ghlInfo, const int boneIndex, int *startFrame, int *endFrame );
qboolean		G2API_GetBoneAnimIndex( CGhoul2Info *ghlInfo, const int iBoneIndex, const int AcurrentTime, float *currentFrame, int *startFrame, int *endFrame, int *flags, float *animSpeed, int *ignore );
qboolean		G2API_PauseBoneAnimIndex( CGhoul2Info *ghlInfo, const int boneIndex, const int AcurrentTime );
qboolean		G2API_SetAnimIndex( CGhoul2Info *ghlInfo, const int index );

