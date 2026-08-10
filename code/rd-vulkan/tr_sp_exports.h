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
void			RE_LAGoggles( void );
qboolean		RE_InitDissolve( qboolean bForceCircularExtroWipe );
qboolean		RE_ProcessDissolve( void );
float *			get_tr_distortionAlpha( void );
float *			get_tr_distortionStretch( void );
qboolean *		get_tr_distortionPrePost( void );
qboolean *		get_tr_distortionNegate( void );
unsigned int	AnyLanguage_ReadCharFromString2( char **psText, qboolean *pbIsTrailingPunctuation );


