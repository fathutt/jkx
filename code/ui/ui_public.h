/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#ifndef __UI_PUBLIC_H__
#define __UI_PUBLIC_H__


#include "../client/keycodes.h"


#define UI_API_VERSION	3


/*
===============================================================================

What the engine calls in the menu system.

Single-player compiles the interface into the engine rather than loading it as
a module, so these are ordinary function calls and this list is what the client
is allowed to make. It exists because the alternative was what cl_cgame.cpp did
for twenty years: include ui_shared.h - the menu system's whole private
vocabulary, 517 lines of it - and reach into menuDef_t and itemDef_t from
outside to read window rectangles and shader handles by hand.

The menu handle is void * on purpose. cgame asks for a menu by name, keeps the
answer and hands it back to be painted; nothing on this side of the boundary
needs to know what is behind the pointer, and once it does not, the header
describing it can stay where it belongs.

The parser is here for a less good reason: PC_* is a general text parser that
happens to live in ui_shared.cpp, and cgame reaches it through the client. It
is not part of the menu system and should not be in this list; moving it out is
its own piece of work.

===============================================================================
*/

// menus
void		*UI_FindMenuByName( const char *name );
void		UI_PaintMenuByHandle( void *menu, qboolean forcePaint );
qboolean	UI_GetMenuInfo( const char *menuName, int *x, int *y, int *w, int *h );
qboolean	UI_GetItemText( const char *menuName, const char *itemName, char *dest, int destSize );
qboolean	UI_GetItemInfo( const char *menuName, const char *itemName,
				int *x, int *y, int *w, int *h, vec4_t *color, qhandle_t *background );
void		Menus_OpenByName( const char *p );
void		Menus_CloseAll( void );
void		Menu_Reset( void );
void		Menu_New( char *buffer );
void		Menu_PaintAll( void );
void		String_Init( void );
void		UI_Cursor_Show( qboolean flag );

// the text parser that lives in the same file
int			PC_StartParseSession( const char *fileName, char **buffer );
void		PC_EndParseSession( char *buffer );
char		*PC_ParseExt( void );
qboolean	PC_ParseInt( int *number );
qboolean	PC_ParseFloat( float *number );
qboolean	PC_ParseString( const char **tempStr );


typedef struct {
	//============== general Quake services ==================

	// print message on the local console
	void		(*Printf)( const char *fmt, ... );

	// abort the game
	NORETURN_PTR void	(*Error)( int level, const char *fmt, ... );

	// console variable interaction
	void		(*Cvar_Set)( const char *name, const char *value );
	float		(*Cvar_VariableValue)( const char *var_name );
	void		(*Cvar_VariableStringBuffer)( const char *var_name, char *buffer, int bufsize );
	void		(*Cvar_SetValue)( const char *var_name, float value );
	void		(*Cvar_Reset)( const char *name );
	void		(*Cvar_Create)( const char *var_name, const char *var_value, int flags );
	void		(*Cvar_InfoStringBuffer)( int bit, char *buffer, int bufsize );

	// console command interaction
	int			(*Argc)( void );
	void		(*Argv)( int n, char *buffer, int bufferLength );
	void		(*Cmd_ExecuteText)( int exec_when, const char *text );
	void		(*Cmd_TokenizeString)( const char *text );

	// filesystem access
	int			(*FS_FOpenFile)( const char *qpath, fileHandle_t *file, fsMode_t mode );
	int 		(*FS_Read)( void *buffer, int len, fileHandle_t f );
	int 		(*FS_Write)( const void *buffer, int len, fileHandle_t f );
	void		(*FS_FCloseFile)( fileHandle_t f );
	int			(*FS_GetFileList)(  const char *path, const char *extension, char *listbuf, int bufsize );
	long		(*FS_ReadFile)( const char *name, void **buf );
	void		(*FS_FreeFile)( void *buf );

	// =========== renderer function calls ================

	qhandle_t	(*R_RegisterModel)( const char *name );			// returns rgb axis if not found
	qhandle_t	(*R_RegisterSkin)( const char *name );			// returns all white if not found
	qhandle_t	(*R_RegisterShader)( const char *name );			// returns white if not found
	qhandle_t	(*R_RegisterShaderNoMip)( const char *name );			// returns white if not found
	qhandle_t	(*R_RegisterFont)( const char *name );			// returns 0 for bad font

	int			(*R_Font_StrLenPixels)(const char *text, const int setIndex, const float scale );
	int			(*R_Font_HeightPixels)(const int setIndex, const float scale );
	void		(*R_Font_DrawString)(int ox, int oy, const char *text, const float *rgba, const int setIndex, int iMaxPixelWidth, const float scale );
	int			(*R_Font_StrLenChars)(const char *text);
	qboolean	(*Language_IsAsian) (void);
	qboolean	(*Language_UsesSpaces) (void);
	unsigned int (*AnyLanguage_ReadCharFromString)( char *psText, int *piAdvanceCount, qboolean *pbIsTrailingPunctuation /* = NULL */);

	// a scene is built up by calls to R_ClearScene and the various R_Add functions.
	// Nothing is drawn until R_RenderScene is called.
	void		(*R_ClearScene)( void );
	void		(*R_AddRefEntityToScene)( const refEntity_t *re );
	void		(*R_AddPolyToScene)( qhandle_t hShader , int numVerts, const polyVert_t *verts );
	void		(*R_AddLightToScene)( const vec3_t org, float intensity, float r, float g, float b );
	void		(*R_RenderScene)( const refdef_t *fd );

	void		(*R_ModelBounds)( qhandle_t handle, vec3_t mins, vec3_t maxs );

	void		(*R_SetColor)( const float *rgba );	// NULL = 1,1,1,1
	void		(*R_DrawStretchPic) ( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader );	// 0 = white
	void		(*R_ScissorPic) ( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader );	// 0 = white

	// force a screen update, only used during gamestate load
	void		(*UpdateScreen)( void );

#ifdef JK2_MODE
	// stuff for savegame screenshots...
	void		(*PrecacheScreenshot)( void );
#endif

	//========= model collision ===============

	// R_LerpTag is only valid for md3 models
	void		(*R_LerpTag)( orientation_t *tag, clipHandle_t mod, int startFrame, int endFrame,
						 float frac, const char *tagName );

	// =========== sound function calls ===============

	void		(*S_StartLocalSound)( sfxHandle_t sfxHandle, int channelNum );
	sfxHandle_t	(*S_RegisterSound)( const char* name);
	void		(*S_StartLocalLoopingSound)( sfxHandle_t sfxHandle);
	void		(*S_StopSounds)( void );


	// =========== getting save game picture ===============
	void	(*DrawStretchRaw) (int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty);
#ifdef JK2_MODE
	qboolean(*SG_GetSaveImage)( const char *psPathlessBaseName, void *pvAddress );
#endif
	int		(*SG_GetSaveGameComment)(const char *psPathlessBaseName, char *sComment, char *sMapName);
	qboolean (*SG_GameAllowedToSaveHere)(qboolean inCamera);
	void (*SG_StoreSaveGameComment)(const char *sComment);
	//byte *(*SCR_GetScreenshot)(qboolean *);

	// =========== data shared with the client system =============

	// keyboard and key binding interaction
	void		(*Key_KeynumToStringBuf)( int keynum, char *buf, int buflen );
	void		(*Key_GetBindingBuf)( int keynum, char *buf, int buflen );
	void		(*Key_SetBinding)( int keynum, const char *binding );
	qboolean	(*Key_IsDown)( int keynum );
	qboolean	(*Key_GetOverstrikeMode)( void );
	void		(*Key_SetOverstrikeMode)( qboolean state );
	void		(*Key_ClearStates)( void );
	int			(*Key_GetCatcher)( void );
	void		(*Key_SetCatcher)( int catcher );

#ifdef JK2_MODE
	qboolean	(*SP_Register)( const char *Package, unsigned char Registration );
	const char *(*SP_GetStringText)(unsigned short ID);
	const char *(*SP_GetStringTextString)(const char *Reference);
#endif
	void		(*GetClipboardData)( char *buf, int bufsize );

	void		(*GetGlconfig)( glconfig_t *config );

	connstate_t	(*GetClientState)( void );

	void		(*GetConfigString)( int index, char* buff, int buffsize );

	int			(*Milliseconds)( void );
	void		(*Draw_DataPad)(int HUDType);
} uiimport_t;

typedef enum {
	DP_HUD=0,
	DP_OBJECTIVES,
	DP_WEAPONS,
	DP_INVENTORY,
	DP_FORCEPOWERS
}dpTypes_t;

// uiImport_t was here: ninety-five ordinals naming the engine services the
// interface could ask for, in the multiplayer manner, over a system call.
//
// Single-player does not make that call. The interface is compiled into the
// engine and reaches it through uiimport_t above, a struct of function pointers
// CL_InitUI fills in directly, so the ordinals had exactly one reader -
// CL_UISystemCalls in cl_ui.cpp, which nothing installed and nothing called.
// The two remaining uses in ui_syscalls.cpp are inside a comment block; every
// other trap in that file had already been rewritten to call the engine
// function by name, one at a time, leaving the syscall line commented out
// above the working one.

#endif
