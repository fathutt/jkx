/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#pragma once

// The engine services the renderer uses, declared rather than passed.
//
// These used to arrive as a struct of function pointers - refimport_t - filled
// in by the client and handed over at load time, because the renderer was a
// separate module and had no other way to reach the engine. Now that there is
// one binary, a table of pointers to functions that are right there is an
// indirection that buys nothing and costs the ability to inline, to see a call
// in a backtrace, and to have the compiler check anything at all.
//
// Every signature here is the one the table declared, because that is the one
// both sides already agreed on. Where the engine's function has a different
// name from the table entry - the table renamed a few - the engine's name is
// what appears, and the comment says which entry it was.

// Deliberately no includes: this is pulled in from tr_local.h after the types
// it names are already there. A header that drags in the engine's world is how
// the module boundary got blurry the first time.

class CMiniHeap;

// --- diagnostics -----------------------------------------------------------

// Was ri.Printf. The level is printParm_t; the engine routes PRINT_DEVELOPER
// through the developer cvar and colours PRINT_WARNING.
void QDECL	CL_RefPrintf( int printLevel, const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));

// Was ri.Error.
//
// NORETURN goes before the name, not after the parameter list. Under GCC it is
// __attribute__((noreturn)), which is happy in either place; under MSVC it is
// __declspec(noreturn), which is only a declaration specifier and is silently
// dropped anywhere else. Trailing, MSVC warns C4091 and then fails to parse
// what follows. Every other declaration in the tree already writes it here.
void NORETURN QDECL	Com_Error( int errorLevel, const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));

void QDECL	Com_Printf( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));

// Was ri.Milliseconds. For profiling only - game time comes from the refdef.
int			Sys_Milliseconds2( void );

// Was ri.LowPhysicalMemory.
qboolean	Sys_LowPhysicalMemory( void );

// --- memory ----------------------------------------------------------------

// Was ri.Malloc. The client's wrapper, which is where the renderer's
// allocations are tagged.
void		*CL_Malloc( int iSize, memtag_t eTag, qboolean bZeroit, int iAlign );

int			Z_Free( void *memory );
int			Z_MemSize( memtag_t eTag );
void		Z_MorphMallocTag( void *pvBuffer, memtag_t eDesiredTag );

// --- console ---------------------------------------------------------------

void		Cmd_ExecuteString( const char *text );
int			Cmd_Argc( void );
char		*Cmd_Argv( int arg );
void		Cmd_ArgsBuffer( char *buffer, int bufferLength );
void		Cmd_AddCommand( const char *cmd_name, xcommand_t function );
void		Cmd_RemoveCommand( const char *cmd_name );

void		Cvar_Set( const char *var_name, const char *value );
// The default argument is on the engine's own declaration, which is already in
// scope here; repeating it is an error rather than a duplicate.
cvar_t		*Cvar_Get( const char *var_name, const char *value, int flags, const char *var_desc );
void		Cvar_SetValue( const char *name, float value );
void		Cvar_CheckRange( cvar_t *cv, float minVal, float maxVal, qboolean shouldBeIntegral );
void		Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize );
char		*Cvar_VariableString( const char *var_name );
float		Cvar_VariableValue( const char *var_name );
int			Cvar_VariableIntegerValue( const char *var_name );

// --- strings ---------------------------------------------------------------

// Was ri.SE_GetString.
const char	*String_GetStringValue( const char *reference );

// --- files -----------------------------------------------------------------

void		FS_FreeFile( void *buffer );
void		FS_FreeFileList( char **fileList );
int			FS_Read( void *buffer, int len, fileHandle_t f );
long		FS_ReadFile( const char *qpath, void **buffer );
void		FS_FCloseFile( fileHandle_t f );
long		FS_FOpenFileRead( const char *qpath, fileHandle_t *file, qboolean uniqueFILE );
fileHandle_t FS_FOpenFileWrite( const char *qpath, qboolean safe );
int			FS_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode );
int			FS_FileIsInPAK( const char *filename, int *pChecksum );
char		**FS_ListFiles( const char *directory, const char *extension, int *numfiles );
int			FS_Write( const void *buffer, int len, fileHandle_t f );
void		FS_WriteFile( const char *qpath, const void *buffer, int size );

// Was ri.FS_FileExists, and it is the sound system's: the only caller asks
// about an audio file.
qboolean	S_FileExists( const char *psFilename );

// --- collision -------------------------------------------------------------

void		CM_DrawDebugSurface( void (*drawPoly)( int color, int numPoints, float *points ) );
bool		CM_CullWorldBox( const cplane_t *frustrum, const vec3pair_t bounds );
byte		*CM_ClusterPVS( int cluster );
int			CM_PointContents( const vec3_t p, clipHandle_t model );

void		SV_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
				int passEntityNum, int contentmask, EG2_Collision eG2TraceType, int useLod );

// --- sound and video -------------------------------------------------------

void		S_RestartMusic( void );
qboolean	SND_RegisterAudio_LevelLoadEnd( qboolean bDeleteEverythingNotUsedThisLevel );

e_status	CIN_RunCinematic( int handle );
int			CIN_PlayCinematic( const char *arg0, int xpos, int ypos, int width, int height, int bits, const char *psAudioFile );
void		CIN_UploadCinematic( int handle );

// --- window and Vulkan -----------------------------------------------------

window_t	WIN_Init( const windowDesc_t *desc, glconfig_t *glConfig );
void		WIN_SetGamma( glconfig_t *glConfig, byte red[256], byte green[256], byte blue[256] );
void		WIN_Present( window_t *window );
void		WIN_Shutdown( void );

// Were ri.VK_IsMinimized and friends, which the table renamed on the way in.
qboolean	WIN_VK_IsMinimized( void );
void		*WIN_VK_GetInstanceProcAddress( void );
qboolean	WIN_VK_CreateSurface( VkInstance instance, VkSurfaceKHR *surface );
void		WIN_VK_DestroyWindow( void );

// --- the rest --------------------------------------------------------------

// Was ri.GetG2VertSpaceServer. The arena Ghoul2 transforms vertices in.
CMiniHeap	*GetG2VertSpaceServer( void );

bool		PD_Store( const char *name, const void *data, size_t size );
const void	*PD_Load( const char *name, size_t *size );

// The cached map image and the flags beside it. These were getters in the table
// because a module cannot see another module's globals; here they are globals,
// and the accessors that wrapped them are gone with the table.
extern void			*gpvCachedMapDiskImage;
extern char			gsCachedMapDiskImage[MAX_QPATH];
extern qboolean		gbUsingCachedMapDataRightNow;
extern qboolean		gbAlreadyDoingLoad;
extern int			com_frameTime;
