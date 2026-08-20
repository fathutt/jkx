/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
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

// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "../server/exe_headers.h"

#include "client.h"
#include "client_ui.h"

extern console_t con;

qboolean	scr_initialized;		// ready to draw

cvar_t		*cl_timegraph;
cvar_t		*cl_debuggraph;
cvar_t		*cl_graphheight;
cvar_t		*cl_graphscale;
cvar_t		*cl_graphshift;

/*
================
SCR_DrawNamedPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawNamedPic( float x, float y, float width, float height, const char *picname ) {
	qhandle_t	hShader;

	assert( width != 0 );

	hShader = re.RegisterShader( picname );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
================
SCR_FillRect

Coordinates are 640*480 virtual values
=================
*/
void SCR_FillRect( float x, float y, float width, float height, const float *color ) {
	re.SetColor( color );

	re.DrawStretchPic( x, y, width, height, 0, 0, 0, 0, cls.whiteShader );

	re.SetColor( NULL );
}


/*
================
SCR_DrawPic

Coordinates are 640*480 virtual values
A width of 0 will draw with the original image width
=================
*/
void SCR_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) {
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
** SCR_DrawBigChar
** big chars are drawn at 640*480 virtual screen size
*/
void SCR_DrawBigChar( int x, int y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;
	float	ax, ay, aw, ah;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -BIGCHAR_HEIGHT ) {
		return;
	}

	ax = x;
	ay = y;
	aw = BIGCHAR_WIDTH;
	ah = BIGCHAR_HEIGHT;

	row = ch>>4;
	col = ch&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;
/*
	re.DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow,
					   fcol + size, frow + size,
					   cls.charSetShader );
*/
	float size2;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.03125;
	size2 = 0.0625;

	re.DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow,
					   fcol + size, frow + size2,
					   cls.charSetShader );

}

/*
==================
SCR_DrawBigString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawBigStringExt( int x, int y, const char *string, float *setColor, qboolean forceColor, qboolean noColorEscape ) {
	vec4_t		color;
	const char	*s;
	int			xx;

	// draw the drop shadow
	color[0] = color[1] = color[2] = 0;
	color[3] = setColor[3];
	re.SetColor( color );
	s = string;
	xx = x;
	while ( *s ) {
		if ( !noColorEscape && Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		SCR_DrawBigChar( xx+2, y+2, *s );
		xx += BIGCHAR_WIDTH;
		s++;
	}


	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				memcpy( color, g_color_table[ColorIndex(*(s+1))], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			if ( !noColorEscape ) {
				s += 2;
				continue;
			}
		}
		SCR_DrawBigChar( xx, y, *s );
		xx += BIGCHAR_WIDTH;
		s++;
	}
	re.SetColor( NULL );
}


void SCR_DrawBigString( int x, int y, const char *s, float alpha, qboolean noColorEscape ) {
	float	color[4];

	color[0] = color[1] = color[2] = 1.0;
	color[3] = alpha;
	SCR_DrawBigStringExt( x, y, s, color, qfalse, noColorEscape );
}

void SCR_DrawBigStringColor( int x, int y, const char *s, vec4_t color, qboolean noColorEscape ) {
	SCR_DrawBigStringExt( x, y, s, color, qtrue, noColorEscape );
}

/*
** SCR_Strlen -- skips color escape codes
*/
static int SCR_Strlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}

/*
** SCR_GetBigStringWidth
*/
int	SCR_GetBigStringWidth( const char *str ) {
	return SCR_Strlen( str ) * BIGCHAR_WIDTH;
}

//===============================================================================


/*
===============================================================================

DEBUG GRAPH

===============================================================================
*/
typedef struct
{
	float	value;
	int		color;
} graphsamp_t;

static	int			current;
static	graphsamp_t	values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph (float value, int color)
{
	values[current&1023].value = value;
	values[current&1023].color = color;
	current++;
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
	int		a, x, y, w, i, h;
	float	v;

	//
	// draw the graph
	//
	w = cls.glconfig.vidWidth;
	x = 0;
	y = cls.glconfig.vidHeight;
	re.SetColor( g_color_table[0] );
	re.DrawStretchPic(x, y - cl_graphheight->integer,
		w, cl_graphheight->integer, 0, 0, 0, 0, 0 );
	re.SetColor( NULL );

	for (a=0 ; a<w ; a++)
	{
		i = (current-1-a+1024) & 1023;
		v = values[i].value;
		v = v * cl_graphscale->integer + cl_graphshift->integer;

		if (v < 0)
			v += cl_graphheight->integer * (1+(int)(-v / cl_graphheight->integer));
		h = (int)v % cl_graphheight->integer;
		re.DrawStretchPic( x+w-1-a, y - h, 1, h, 0, 0, 0, 0, 0 );
	}
}
//=============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init( void ) {
	cl_timegraph = Cvar_Get ("timegraph", "0", CVAR_CHEAT);
	cl_debuggraph = Cvar_Get ("debuggraph", "0", CVAR_CHEAT);
	cl_graphheight = Cvar_Get ("graphheight", "32", CVAR_CHEAT);
	cl_graphscale = Cvar_Get ("graphscale", "1", CVAR_CHEAT);
	cl_graphshift = Cvar_Get ("graphshift", "0", CVAR_CHEAT);

	scr_initialized = qtrue;
}


//=======================================================

void UI_SetActiveMenu( const char* menuname,const char *menuID );
void _UI_Refresh( int realtime );
void UI_DrawConnect( const char *servername, const char * updateInfoString );

/*
==================
SCR_DrawScreenField

This will be called twice if rendering in stereo mode
==================
*/
/*
================
SCR_FillFrameMargins

Black over whatever is outside the interface's frame.

The frame keeps the interface's own proportions and is centred, so on anything
that is not the shape it was drawn at there is a margin. During play that
margin belongs to the scene and must not be painted - the world is rendered to
the whole window, and only the 2D on top of it is framed. When the interface is
the entire picture there is nothing behind it, and the margin is a bar.

Drawn in the head-up display's space, because that is the one whose coordinates
reach the edge of the window.
================
*/
void SCR_FillFrameMargins( void ) {
	const float frame = (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT;
	const float have = ( cls.glconfig.vidHeight > 0 )
		? (float)cls.glconfig.vidWidth / (float)cls.glconfig.vidHeight : frame;

	// The fallback used to be SCREEN_WIDTH, and SCREEN_WIDTH is the frame's own
	// width - so when virtualWidth was not set yet the bar came out
	// (640 - 640) / 2 = 0 and this function drew nothing while reporting
	// nothing. That is the state during a map load, which is when the
	// introduction crawl plays, which is where the grey margins that were
	// supposed to be fixed kept coming back.
	//
	// The virtual width is a definition, not a measurement: it is the frame's
	// height scaled to the window's shape. Computing it here costs a divide and
	// removes a way for this to fail quietly.
	const float virtualWidth = ( cls.glconfig.virtualWidth > 0.0f )
		? cls.glconfig.virtualWidth : ( (float)SCREEN_HEIGHT * have );

	if ( fabsf( have - frame ) < 0.001f ) {
		return;					// the frame covers the window
	}

	re.Set2DSpace( 1, 0.0f );		// SPACE2D_SCREEN
	re.SetColor( colorBlack );

	if ( have > frame ) {
		// Bars at the sides. The frame is SCREEN_HEIGHT * frame units wide in
		// this space, centred.
		const float frameWidth = (float)SCREEN_HEIGHT * frame;
		const float bar = ( virtualWidth - frameWidth ) * 0.5f;

		re.DrawStretchPic( 0, 0, bar, SCREEN_HEIGHT, 0, 0, 0, 0, cls.whiteShader );
		re.DrawStretchPic( virtualWidth - bar, 0, bar, SCREEN_HEIGHT,
			0, 0, 0, 0, cls.whiteShader );
	} else {
		// Bars top and bottom.
		const float frameHeight = virtualWidth / frame;
		const float bar = ( (float)SCREEN_HEIGHT - frameHeight ) * 0.5f;

		re.DrawStretchPic( 0, 0, virtualWidth, bar, 0, 0, 0, 0, cls.whiteShader );
		re.DrawStretchPic( 0, (float)SCREEN_HEIGHT - bar, virtualWidth, bar,
			0, 0, 0, 0, cls.whiteShader );
	}

	re.SetColor( NULL );
	re.Set2DSpace( 0, 0.0f );		// SPACE2D_FRAME
}

void SCR_DrawScreenField( stereoFrame_t stereoFrame ) {

	re.BeginFrame( stereoFrame );

	qboolean uiFullscreen = _UI_IsFullscreen();

	// if the menu is going to cover the entire screen, we
	// don't need to render anything under it
	if ( !uiFullscreen ) {
		switch( cls.state ) {
		default:
			Com_Error( ERR_FATAL, "SCR_DrawScreenField: bad cls.state" );
			break;
		case CA_CINEMATIC:
			SCR_DrawCinematic();
			break;
		case CA_DISCONNECTED:
			// force menu up
			UI_SetActiveMenu( "mainMenu", NULL );
			break;
		case CA_CONNECTING:
		case CA_CHALLENGING:
		case CA_CONNECTED:
			// connecting clients will only show the connection dialog
			UI_DrawConnect( clc.servername, cls.updateInfoString );
			break;
		case CA_LOADING:
		case CA_PRIMED:
			// draw the game information screen and loading progress
			CL_CGameRendering( stereoFrame );
			break;
		case CA_ACTIVE:
			if (CL_IsRunningInGameCinematic() || CL_InGameCinematicOnStandBy())
			{
				SCR_DrawCinematic();
			}
			else
			{
				CL_CGameRendering( stereoFrame );
			}
			break;
		}
	}

	re.ProcessDissolve();

	// draw downloading progress bar

	// Bars first, so the interface draws over them and the scene, when there is
	// one, is not covered by them.
	//
	// CA_CINEMATIC is tested by name rather than by being less than CA_ACTIVE,
	// because it is the last value in connstate_t and therefore greater than
	// it. A video is a picture in the fitted frame like any menu, so without
	// this the margins beside it were never filled - and what showed through
	// was the renderer's clear colour, which is 0.75 grey. Grey bars beside
	// every video, from a comparison that reads as though it covers this case.
	//
	// The other half of the same thing: a video played during a level runs at
	// CA_ACTIVE and is drawn by the branch above that tests these two functions
	// rather than the state. Testing the state alone therefore covered the
	// videos between levels and not the ones inside them, which is where the
	// campaign's opening video is - and it is the one that kept coming back
	// with grey beside it after the fix that was supposed to remove it.
	const qboolean bVideo = (qboolean)( cls.state == CA_CINEMATIC
		|| CL_IsRunningInGameCinematic() || CL_InGameCinematicOnStandBy() );

	if ( uiFullscreen || cls.state < CA_ACTIVE || bVideo ) {
		SCR_FillFrameMargins();
	}

	// the menu draws next
	_UI_Refresh( cls.realtime );

	// console draws next
	Con_DrawConsole ();

	// debug graph can be drawn on top of anything
	if ( cl_debuggraph->integer || cl_timegraph->integer ) {
		SCR_DrawDebugGraph ();
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void ) {
	static int	recursive;

	if ( !scr_initialized ) {
		return;				// not initialized yet
	}

	// load the ref / ui / cgame if needed
	CL_StartHunkUsers();

	if ( ++recursive > 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	recursive = qtrue;

	// If there is no VM, there are also no rendering commands issued. Stop the renderer in
	// that case.
	if ( cls.uiStarted )
	{
		// if running in stereo, we need to draw the frame twice
		if ( cls.glconfig.stereoEnabled ) {
			SCR_DrawScreenField( STEREO_LEFT );
			SCR_DrawScreenField( STEREO_RIGHT );
		} else {
			SCR_DrawScreenField( STEREO_CENTER );
		}

		if ( com_speeds->integer ) {
			re.EndFrame( &time_frontend, &time_backend );
		} else {
			re.EndFrame( NULL, NULL );
		}
	}

	recursive = 0;
}

// this stuff is only used by the savegame (SG) code for screenshots...
//


static byte	bScreenData[SG_SCR_WIDTH * SG_SCR_HEIGHT * 4];
static qboolean screenDataValid = qfalse;
void SCR_UnprecacheScreenshot()
{
	screenDataValid = qfalse;
}


void SCR_PrecacheScreenshot()
{
	// No screenshots unless connected to single player local server...
	//
//	char *psInfo = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
//	int iMaxClients = atoi(Info_ValueForKey( psInfo, "sv_maxclients" ));

	// (no need to check single-player status in voyager, this code base is all singleplayer)
	if ( cls.state != CA_ACTIVE )
	{
		return;
	}

	if (!Key_GetCatcher( ))
	{
		// in-game...
		//
//		SCR_UnprecacheScreenshot();
//		pbScreenData = (byte *)Z_Malloc(SG_SCR_WIDTH * SG_SCR_HEIGHT * 4);
		S_ClearSoundBuffer();	// clear DMA etc because the following glReadPixels() call can take ages
		re.GetScreenShot( (byte *) &bScreenData, SG_SCR_WIDTH, SG_SCR_HEIGHT);
		screenDataValid = qtrue;
	}

}

// Returns a SG_SCR_WIDTH x SG_SCR_HEIGHT screenshot buffer
// Must match color components with DrawStretchRaw: 4
byte *SCR_GetScreenshot(qboolean *qValid)
{
	if (!screenDataValid) {
		SCR_PrecacheScreenshot();
	}
	if (qValid) {
		*qValid = screenDataValid;
	}
	return (byte *)&bScreenData;
}

// called from save-game code to set the lo-res loading screen to be the one from the save file...
//
void SCR_SetScreenshot(const byte *pbData, int w, int h)
{
	if (w == SG_SCR_WIDTH && h == SG_SCR_HEIGHT)
	{
		screenDataValid = qtrue;
		memcpy(&bScreenData, pbData, SG_SCR_WIDTH*SG_SCR_HEIGHT*4);
	}
	else
	{
		screenDataValid = qfalse;
		memset(&bScreenData, 0,      SG_SCR_WIDTH*SG_SCR_HEIGHT*4);
	}
}


// Two client-side wrappers around renderer entry points that are present in
// both games. They used to be Outcast-only, which made the savegame screenshot
// they serve Outcast-only, which made the UI import table a different shape in
// the two builds - and that is the one thing a single binary cannot have.
//
// This is just a client-side wrapper for the function RE_TempRawImage_ReadFromFile() in the renderer code...
//

byte* SCR_TempRawImage_ReadFromFile(const char *psLocalFilename, int *piWidth, int *piHeight, byte *pbReSampleBuffer, qboolean qbVertFlip)
{
	return re.TempRawImage_ReadFromFile(psLocalFilename, piWidth, piHeight, pbReSampleBuffer, qbVertFlip);
}
//
// ditto (sort of)...
//
void  SCR_TempRawImage_CleanUp()
{
	re.TempRawImage_CleanUp();
}


