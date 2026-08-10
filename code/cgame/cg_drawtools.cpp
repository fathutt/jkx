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

#include "cg_headers.h"

#include "cg_media.h"

/*
================
CG_DrawSides

Coords are virtual 640x480
================
*/
void CG_DrawSides(float x, float y, float w, float h, float size) {
	//size *= cgs.screenXScale;
	cgi_R_DrawStretchPic( x, y, size, h, 0, 0, 0, 0, cgs.media.whiteShader );
	cgi_R_DrawStretchPic( x + w - size, y, size, h, 0, 0, 0, 0, cgs.media.whiteShader );
}

void CG_DrawTopBottom(float x, float y, float w, float h, float size) {
	//size *= cgs.screenYScale;
	cgi_R_DrawStretchPic( x, y, w, size, 0, 0, 0, 0, cgs.media.whiteShader );
	cgi_R_DrawStretchPic( x, y + h - size, w, size, 0, 0, 0, 0, cgs.media.whiteShader );
}

/*
================
CG_DrawRect

Coordinates are 640*480 virtual values
=================
*/
void CG_DrawRect( float x, float y, float width, float height, float size, const float *color ) {
	cgi_R_SetColor( color );

	CG_DrawTopBottom(x, y, width, height, size);
	CG_DrawSides(x, y, width, height, size);

	cgi_R_SetColor( NULL );
}

/*
================
CG_FillRect

Coordinates are 640*480 virtual values
=================
*/
void CG_FillRect( float x, float y, float width, float height, const float *color ) {
	cgi_R_SetColor( color );
	cgi_R_DrawStretchPic( x, y, width, height, 0, 0, 0, 0, cgs.media.whiteShader);
	cgi_R_SetColor( NULL );
}


/*
================
CG_Scissor

Coordinates are 640*480 virtual values
=================
*/
void CG_Scissor( float x, float y, float width, float height)
{

	cgi_R_Scissor( x, y, width, height);

}


/*
================
CG_DrawPic

Coordinates are 640*480 virtual values
A width of 0 will draw with the original image width
=================
*/
void CG_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) {
	cgi_R_DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}

/*
================
CG_DrawPic2

Coordinates are 640*480 virtual values
A width of 0 will draw with the original image width
Can also specify the exact texture coordinates
=================
*/
void CG_DrawPic2( float x, float y, float width, float height, float s1, float t1, float s2, float t2, qhandle_t hShader )
{
	cgi_R_DrawStretchPic( x, y, width, height, s1, t1, s2, t2, hShader );
}

/*
================
CG_DrawRotatePic

Coordinates are 640*480 virtual values
A width of 0 will draw with the original image width
rotates around the upper right corner of the passed in point
=================
*/
void CG_DrawRotatePic( float x, float y, float width, float height,float angle, qhandle_t hShader ) {
	cgi_R_DrawRotatePic( x, y, width, height, 0, 0, 1, 1, angle, hShader );
}

/*
================
CG_DrawRotatePic2

Coordinates are 640*480 virtual values
A width of 0 will draw with the original image width
Actually rotates around the center point of the passed in coordinates
=================
*/
void CG_DrawRotatePic2( float x, float y, float width, float height,float angle, qhandle_t hShader ) {
	cgi_R_DrawRotatePic2( x, y, width, height, 0, 0, 1, 1, angle, hShader );
}

/*
===============
CG_DrawChar

Coordinates and size in 640*480 virtual screen size
===============
*/
void CG_DrawChar( int x, int y, int width, int height, int ch ) {
	int row, col;
	float frow, fcol;
	float size;
	float	ax, ay, aw, ah;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	ax = x;
	ay = y;
	aw = width;
	ah = height;

	row = ch>>4;
	col = ch&15;
/*
	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	cgi_R_DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow,
					   fcol + size, frow + size,
					   cgs.media.charsetShader );
*/

	float size2;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.03125;
	size2 = 0.0625;

	cgi_R_DrawStretchPic( ax, ay, aw, ah, fcol, frow, fcol + size, frow + size2,
		cgs.media.charsetShader );

}


/*
==================
CG_DrawStringExt

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void CG_DrawStringExt( int x, int y, const char *string, const float *setColor,
		qboolean forceColor, qboolean shadow, int charWidth, int charHeight ) {
	vec4_t		color;
	const char	*s;
	int			xx;

	// draw the drop shadow
	if (shadow) {
		color[0] = color[1] = color[2] = 0;
		color[3] = setColor[3];
		cgi_R_SetColor( color );
		s = string;
		xx = x;
		while ( *s ) {
			if ( Q_IsColorString( s ) ) {
				s += 2;
				continue;
			}
			CG_DrawChar( xx + 2, y + 2, charWidth, charHeight, *s );
			xx += charWidth;
			s++;
		}
	}

	// draw the colored text
	s = string;
	xx = x;
	cgi_R_SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				memcpy( color, g_color_table[ColorIndex(*(s+1))], sizeof( color ) );
				color[3] = setColor[3];
				cgi_R_SetColor( color );
			}
			s += 2;
			continue;
		}
		CG_DrawChar( xx, y, charWidth, charHeight, *s );
		xx += charWidth;
		s++;
	}
	cgi_R_SetColor( NULL );
}


void CG_DrawSmallStringColor( int x, int y, const char *s, vec4_t color ) {
	CG_DrawStringExt( x, y, s, color, qtrue, qfalse, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT );
}

/*
=================
CG_DrawStrlen

Returns character count, skiping color escape codes
=================
*/
int CG_DrawStrlen( const char *str ) {
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
=============
CG_TileClearBox

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
static void CG_TileClearBox( int x, int y, int w, int h, qhandle_t hShader ) {
	float	s1, t1, s2, t2;

	s1 = x/64.0;
	t1 = y/64.0;
	s2 = (x+w)/64.0;
	t2 = (y+h)/64.0;
	cgi_R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, hShader );
}



/*
==============
CG_TileClear

Clear around a sized down screen
==============
*/
void CG_TileClear( void ) {
	int		top, bottom, left, right;
	int		w, h;

	w = cgs.glconfig.vidWidth;
	h = cgs.glconfig.vidHeight;

	if ( cg.refdef.x == 0 && cg.refdef.y == 0 &&
		cg.refdef.width == w && cg.refdef.height == h ) {
		return;		// full screen rendering
	}

	top = cg.refdef.y;
	bottom = top + cg.refdef.height-1;
	left = cg.refdef.x;
	right = left + cg.refdef.width-1;

	// clear above view screen
	CG_TileClearBox( 0, 0, w, top, cgs.media.backTileShader );

	// clear below view screen
	CG_TileClearBox( 0, bottom, w, h - bottom, cgs.media.backTileShader );

	// clear left of view screen
	CG_TileClearBox( 0, top, left, bottom - top + 1, cgs.media.backTileShader );

	// clear right of view screen
	CG_TileClearBox( right, top, w - right, bottom - top + 1, cgs.media.backTileShader );
}



/*
================
CG_FadeColor
================
*/
float *CG_FadeColor( int startMsec, int totalMsec ) {
	static vec4_t		color;
	int			t;

	if ( startMsec == 0 ) {
		return NULL;
	}

	t = cg.time - startMsec;

	if ( t >= totalMsec ) {
		return NULL;
	}

	// fade out
	if ( totalMsec - t < FADE_TIME ) {
		color[3] = ( totalMsec - t ) * 1.0/FADE_TIME;
	} else {
		color[3] = 1.0;
	}
	color[0] = color[1] = color[2] = 1;

	return color;
}

/*
==============
CG_DrawNumField

Take x,y positions as if 640 x 480 and scales them to the proper resolution

==============
*/
void CG_DrawNumField (int x, int y, int width, int value,int charWidth,int charHeight,int style,qboolean zeroFill)
{
	char	num[16], *ptr;
	int		l;
	int		frame;
	int		xWidth;

	if (width < 1) {
		return;
	}

	// draw number string
	if (width > 5) {
		width = 5;
	}

	switch ( width ) {
	case 1:
		value = value > 9 ? 9 : value;
		value = value < 0 ? 0 : value;
		break;
	case 2:
		value = value > 99 ? 99 : value;
		value = value < -9 ? -9 : value;
		break;
	case 3:
		value = value > 999 ? 999 : value;
		value = value < -99 ? -99 : value;
		break;
	case 4:
		value = value > 9999 ? 9999 : value;
		value = value < -999 ? -999 : value;
		break;
	}

	Com_sprintf (num, sizeof(num), "%i", value);
	l = strlen(num);
	if (l > width)
		l = width;

	// FIXME: Might need to do something different for the chunky font??
	switch(style)
	{
	case NUM_FONT_SMALL:
		xWidth = charWidth;
		break;
	case NUM_FONT_CHUNKY:
		xWidth = (charWidth/1.2f) + 2;
		break;
	default:
	case NUM_FONT_BIG:
		xWidth = (charWidth/2) + 7;//(charWidth/6);
		break;
	}

	if ( zeroFill )
	{
		for (int i = 0; i < (width - l); i++ )
		{
			switch(style)
			{
			case NUM_FONT_SMALL:
				CG_DrawPic( x,y, charWidth, charHeight, cgs.media.smallnumberShaders[0] );
				break;
			case NUM_FONT_CHUNKY:
				CG_DrawPic( x,y, charWidth, charHeight, cgs.media.chunkyNumberShaders[0] );
				break;
			default:
			case NUM_FONT_BIG:
				CG_DrawPic( x,y, charWidth, charHeight, cgs.media.numberShaders[0] );
				break;
			}
			x += 2 + (xWidth);
		}
	}
	else
	{
		x += 2 + (xWidth)*(width - l);
	}

	ptr = num;
	while (*ptr && l)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr -'0';

		switch(style)
		{
		case NUM_FONT_SMALL:
			CG_DrawPic( x,y, charWidth, charHeight, cgs.media.smallnumberShaders[frame] );
			x++;	// For a one line gap
			break;
		case NUM_FONT_CHUNKY:
			CG_DrawPic( x,y, charWidth, charHeight, cgs.media.chunkyNumberShaders[frame] );
			break;
		default:
		case NUM_FONT_BIG:
			CG_DrawPic( x,y, charWidth, charHeight, cgs.media.numberShaders[frame] );
			break;
		}

		x += (xWidth);
		ptr++;
		l--;
	}

}

/*
=================
CG_DrawProportionalString
=================
*/
void CG_DrawProportionalString( int x, int y, const char* str, int style, vec4_t color )
{
	//assert(!style);//call this directly if you need style (OR it into the font handle)
	cgi_R_Font_DrawString (x, y, str, color, cgs.media.qhFontMedium, -1, 1.0f);
}

/*
================================================================================

 WHERE THE EDGE OF THE SCREEN IS

 Everything in here is positioned in a space 480 units tall. It used to be 640
 units wide as well, always, and stretched to whatever the window was: on 16:9
 that is eleven per cent wider than it was drawn, on 32:9 it is a smear.

 There are two spaces now. Menus and anything else composed as a picture keep
 their proportions and are fitted into the window with margins - that is
 SPACE2D_FRAME, and it is the default. The head-up display is not a picture: it
 is a set of things pinned to the corners of the monitor the player has, so it
 works in SPACE2D_SCREEN, where the height is still 480 and the width is
 whatever the aspect calls for.

 An element that says nothing stays where it was, measured from the left edge,
 which is what most of the display wants. The ones that do not - the right-hand
 corner, anything centred - say so by asking for their x through CG_AnchorX.

================================================================================
*/

/*
================
CG_UIScale

r_uiScale, the size the head-up display is drawn at relative to how it was
authored. It magnifies by shrinking the space it is drawn in - half as many
units across the same pixels is twice the size - which is why it moves the
edges, and why CG_ScreenWidth and CG_ScreenHeight both have to know about it.

An element positioned absolutely from the top left corner is already right
under any scale: it grows away from that corner and stays attached to it. The
ones that belong to the other edges are right when they are measured from these
two and not from the constants.
================
*/
float CG_UIScale( void )
{
	// Registered here rather than read every call: cgame reaches cvars through
	// a vmCvar_t that the engine refreshes, and r_uiScale belongs to the
	// renderer, so this is a second handle on the same cvar.
	static vmCvar_t uiScale;
	static qboolean registered = qfalse;

	if ( !registered ) {
		cgi_Cvar_Register( &uiScale, "r_uiScale", "1", CVAR_ARCHIVE );
		registered = qtrue;
	}
	cgi_Cvar_Update( &uiScale );

	if ( uiScale.value < 0.5f ) return 1.0f;		// unset or nonsense
	if ( uiScale.value > 2.0f ) return 2.0f;
	return uiScale.value;
}

float CG_ScreenWidth( void )
{
	const float width = ( cgs.glconfig.virtualWidth > 0.0f )
		? cgs.glconfig.virtualWidth : (float)SCREEN_WIDTH;

	return width / CG_UIScale();
}

float CG_ScreenHeight( void )
{
	return (float)SCREEN_HEIGHT / CG_UIScale();
}

float CG_AnchorX( float x, hudAnchor_t anchor )
{
	const float slack = CG_ScreenWidth() - (float)SCREEN_WIDTH;

	switch ( anchor ) {
	case HUD_RIGHT:		return x + slack;
	case HUD_CENTRE:	return x + slack * 0.5f;
	default:			return x;
	}
}

void CG_HudSpace( void )
{
	cgi_R_Set2DSpace( 1, 0.0f );		// SPACE2D_SCREEN
}

void CG_FrameSpace( void )
{
	cgi_R_Set2DSpace( 0, 0.0f );		// SPACE2D_FRAME
}

/*
================
CG_HudAnchorRight

Shift everything drawn until the next call to the right-hand edge.

This is what anchors a block whose insides cannot be told about anchors - the
right half of the head-up display is a menu, laid out in absolute coordinates
in a .menu file and painted by code that takes no offset. Moving the space it
is drawn in costs two numbers in a matrix; teaching the menu system about
anchors would cost the menu system.
================
*/
void CG_HudAnchorRight( void )
{
	cgi_R_Set2DSpace( 1, CG_ScreenWidth() - (float)SCREEN_WIDTH );
}
