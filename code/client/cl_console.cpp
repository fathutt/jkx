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

// console.c

#include "server/exe_headers.h"

#include "client.h"
#include "qcommon/stringed_ingame.h"
#include "qcommon/game_version.h"

int g_console_field_width = 78;

console_t	con;

cvar_t		*con_conspeed;
cvar_t		*con_notifytime;
cvar_t		*con_opacity; // background alpha multiplier
cvar_t		*con_autoclear;
cvar_t		*con_height;
cvar_t		*con_scale;
cvar_t		*con_timestamps;

#define	DEFAULT_CONSOLE_WIDTH	78

#define CON_BLANK_CHAR			' '
#define CON_SCROLL_L_CHAR		'$'
#define CON_SCROLL_R_CHAR		'$'
#define CON_TIMESTAMP_LEN		11 // "[13:37:00] "
#define CON_MIN_WIDTH			20


static const conChar_t CON_WRAP = { { ColorIndex(COLOR_GREY), '\\' } };
static const conChar_t CON_BLANK = { { ColorIndex(COLOR_WHITE), CON_BLANK_CHAR } };

/*
===============================================================================

PROPORTIONAL METRICS

The console used to be drawn out of a 16x16 grid of characters in a texture,
one quad per cell, at a fixed pitch. That is why every measurement in here was
a column: a line was 78 characters because 78 cells fitted, and the width of a
character was a constant.

It is drawn with the font system now, and the font is proportional. An i is not
a W. So the two things that used to be arithmetic on con.charWidth - where a
line wraps, and where the cursor sits - are sums of advances instead.

===============================================================================
*/

// The font system speaks in 640x480 virtual pixels. The console's own layout
// is in real ones, because that is what it has always been and what con_scale
// scales. These convert.
static float Con_ToVirtualX( float x ) { return x * con.xadjust; }
static float Con_ToVirtualY( float y ) { return y * con.yadjust; }

/*
================
Con_UpdateFontMetrics

The scale that makes the font as tall as a console row, and the width a line of
text has to live in. Both change with the resolution and with con_scale, so
this runs from Con_CheckResize.
================
*/
// The console prints long before the renderer exists - it is where the messages
// about starting the renderer go. Until it does, there is nothing to ask how
// wide a letter is, and the fixed width the console used to be built on is the
// answer. Nothing measured in that state is cached, so the first real
// measurement replaces it rather than living on as a guess.
static qboolean Con_FontReady( void )
{
	return (qboolean)( cls.rendererStarted && re.Font_HeightPixels != NULL
		&& re.Font_StrLenPixels != NULL );
}

static void Con_UpdateFontMetrics( void )
{
	const int font = cls.consoleFont;

	if ( !Con_FontReady() ) {
		con.fontScale = 1.0f;
		con.lineHeight = (float)con.charHeight;
		con.textWidth = Con_ToVirtualX( (float)( con.linewidth * con.charWidth ) );
		con.advanceFont = -1;
		return;
	}

	const float rowHeight = Con_ToVirtualY( (float)con.charHeight );
	const int unscaled = re.Font_HeightPixels( font, 1.0f );

	con.fontScale = ( unscaled > 0 ) ? rowHeight / (float)unscaled : 1.0f;
	con.lineHeight = (float)con.charHeight;

	// Two characters of indent on the left, one of margin on the right: the
	// same room the grid left, so the console keeps its shape.
	con.textWidth = Con_ToVirtualX( (float)( cls.glconfig.vidWidth - 3 * con.charWidth ) );

	if ( con.advanceFont != font || con.advanceScale != con.fontScale ) {
		con.advanceFont = font;
		con.advanceScale = con.fontScale;
		memset( con.advance, 0, sizeof( con.advance ) );
	}
}

/*
================
Con_Advance

How far the pen moves for one character, in virtual pixels.
================
*/
static float Con_Advance( unsigned char ch )
{
	if ( !Con_FontReady() ) {
		return Con_ToVirtualX( (float)con.charWidth );
	}

	if ( con.advance[ch] == 0.0f ) {
		const char s[2] = { (char)ch, '\0' };
		float width = (float)re.Font_StrLenPixels( s, con.advanceFont, con.advanceScale );

		// A glyph the font has nothing for still has to move the pen, or a run
		// of them would pile up in one place and never wrap.
		if ( width <= 0.0f ) {
			width = Con_ToVirtualX( (float)con.charWidth );
		}
		con.advance[ch] = width;
	}
	return con.advance[ch];
}

/*
================
Con_RowToString

One row of cells as a string, with the colour changes put back as ^n codes so
the whole row goes to the font in one call. Trailing blanks are dropped: they
would cost advances and draw nothing.
================
*/
static void Con_RowToString( const conChar_t *text, int count, char *out, int outSize )
{
	int len = 0;
	int colour = -1;

	while ( count > 0 && text[count - 1].f.character == CON_BLANK_CHAR ) {
		count--;
	}

	for ( int i = 0 ; i < count && len < outSize - 3 ; i++ ) {
		if ( text[i].f.color != colour ) {
			colour = text[i].f.color;
			out[len++] = '^';
			out[len++] = (char)( '0' + ( colour & 7 ) );
		}
		out[len++] = text[i].f.character;
	}
	out[len] = '\0';
}

vec4_t	console_color = {0.509f, 0.609f, 0.847f, 1.0f};

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void) {
	// closing a full screen console restarts the demo loop
	if ( cls.state == CA_DISCONNECTED && Key_GetCatcher( ) == KEYCATCH_CONSOLE ) {
//		CL_StartDemoLoop();
		return;
	}

	if( con_autoclear->integer )
		Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;

	Con_ClearNotify ();
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_CONSOLE );
}

/*
===================
Con_ToggleMenu_f
===================
*/
void Con_ToggleMenu_f( void ) {
	CL_KeyEvent( A_ESCAPE, qtrue, Sys_Milliseconds() );
	CL_KeyEvent( A_ESCAPE, qfalse, Sys_Milliseconds() );
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void) {
	int		i;

	for ( i = 0 ; i < CON_TEXTSIZE ; i++ ) {
		con.text[i] = CON_BLANK;
	}

	Con_Bottom();		// go to end
}

/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
void Con_Dump_f (void)
{
	char			filename[MAX_QPATH];
	qboolean		empty;
	int				l, i, j;
	int				line;
	int				lineLen;
	fileHandle_t	f;
#ifdef WIN32
	char			buffer[CON_TIMESTAMP_LEN + MAXPRINTMSG + 2];
#else
	char			buffer[CON_TIMESTAMP_LEN + MAXPRINTMSG + 1];
#endif

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("%s\n", SE_GetString("CON_TEXT_DUMP_USAGE"));
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".txt" );

	if(!COM_CompareExtension(filename, ".txt"))
	{
		Com_Printf( "Con_Dump_f: Only the \".txt\" extension is supported by this command!\n" );
		return;
	}

	f = FS_FOpenFileWrite( filename );
	if (!f)
	{
		Com_Printf ("ERROR: couldn't open %s.\n", filename);
		return;
	}

	Com_Printf ("Dumped console text to %s.\n", filename );

	// skip empty lines
	for (l = 1, empty = qtrue ; l < con.totallines && empty ; l++)
	{
		line = ((con.current + l) % con.totallines) * con.rowwidth;

		for (j = CON_TIMESTAMP_LEN ; j < con.rowwidth - 1 ; j++)
			if (con.text[line + j].f.character != CON_BLANK_CHAR)
				empty = qfalse;
	}

	for ( ; l < con.totallines ; l++)
	{
		lineLen = 0;
		i = 0;

		// Print timestamp
		if (con_timestamps->integer) {
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (i = 0; i < CON_TIMESTAMP_LEN; i++)
				buffer[i] = con.text[line + i].f.character;

			lineLen = CON_TIMESTAMP_LEN;
		}

		// Concatenate wrapped lines
		for ( ; l < con.totallines ; l++)
		{
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1 && i < (int)sizeof(buffer) - 1; j++, i++) {
				buffer[i] = con.text[line + j].f.character;

				if (con.text[line + j].f.character != CON_BLANK_CHAR)
					lineLen = i + 1;
			}

			if (i == sizeof(buffer) - 1)
				break;

			if (con.text[line + j].compare != CON_WRAP.compare)
				break;
		}

#ifdef WIN32 // I really don't like this inconsistency, but OpenJK has been doing this since April 2013
		buffer[lineLen] = '\r';
		buffer[lineLen+1] = '\n';
		FS_Write(buffer, lineLen + 2, f);
#else
		buffer[lineLen] = '\n';
		FS_Write(buffer, lineLen + 1, f);
#endif
	}

	FS_FCloseFile( f );
}


/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;

	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		con.times[i] = 0;
	}
}

/*
================
Con_Initialize

Initialize console for the first time.
================
*/
void Con_Initialize(void)
{
	int	i;

	VectorCopy4(colorWhite, con.color);
	con.charWidth = SMALLCHAR_WIDTH;
	con.charHeight = SMALLCHAR_HEIGHT;
	con.linewidth = DEFAULT_CONSOLE_WIDTH;
	con.rowwidth = CON_TIMESTAMP_LEN + con.linewidth + 1;
	con.totallines = CON_TEXTSIZE / con.rowwidth;
	con.current = con.totallines - 1;
	con.display = con.current;
	con.xadjust = 1.0f;
	con.yadjust = 1.0f;

	// Both of these have to be sane before the first CL_ConsolePrint, which
	// happens long before Con_CheckResize ever runs: the console is where the
	// messages about starting up go. Left at zero, every line wraps after one
	// character, and the whole of the startup log ends up one letter per row -
	// which is exactly what the first version of this did, and which is
	// invisible on screen because by the time anyone looks the recent lines
	// have been printed with a real width.
	con.textWidth = (float)( con.linewidth * con.charWidth );
	con.fontScale = 1.0f;
	con.advanceFont = -1;
	for(i=0; i<CON_TEXTSIZE; i++)
	{
		con.text[i] = CON_BLANK;
	}

	con.initialized = qtrue;
}



/*
================
Con_Resize

Reformat the buffer for new row width
================
*/
static void Con_Resize(int rowwidth)
{
	static conChar_t tbuf[CON_TEXTSIZE];
	int		i, j;
	int		oldrowwidth;
	int		oldtotallines;

	oldrowwidth = con.rowwidth;
	oldtotallines = con.totallines;

	con.rowwidth = rowwidth;
	con.totallines = CON_TEXTSIZE / rowwidth;

	memcpy (tbuf, con.text, sizeof(tbuf));
	for(i=0; i<CON_TEXTSIZE; i++)
		con.text[i] = CON_BLANK;

	int oi = 0;
	int ni = 0;

	while (oi < oldtotallines)
		{
			conChar_t	line[MAXPRINTMSG];
			conChar_t	timestamp[CON_TIMESTAMP_LEN];
			int		lineLen = 0;
			int		oldline = ((con.current + oi) % oldtotallines) * oldrowwidth;
			int		newline = (ni % con.totallines) * con.rowwidth;

			// Store timestamp
			for (i = 0; i < CON_TIMESTAMP_LEN; i++)
				timestamp[i] = tbuf[oldline + i];

			// Store whole line concatenating on CON_WRAP
			for (i = 0; oi < oldtotallines; oi++)
				{
					oldline = ((con.current + oi) % oldtotallines) * oldrowwidth;

					for (j = CON_TIMESTAMP_LEN; j < oldrowwidth - 1 && i < (int)ARRAY_LEN(line); j++, i++) {
						line[i] = tbuf[oldline + j];

						if (line[i].f.character != CON_BLANK_CHAR)
							lineLen = i + 1;
					}

					if (i == ARRAY_LEN(line))
						break;

					if (tbuf[oldline + j].compare != CON_WRAP.compare)
						break;
				}

			oi++;

			// Print stored line to a new text buffer, re-wrapping it. The old
			// wrap points came from the old width and are gone; where the
			// text breaks now is decided the same way it is decided when it
			// is printed, by adding up advances.
			for (i = 0; ; ni++) {
				float used = 0.0f;

				newline = (ni % con.totallines) * con.rowwidth;

				// Print timestamp at the begining of each line
				for (j = 0; j < CON_TIMESTAMP_LEN; j++)
					con.text[newline + j] = timestamp[j];

				for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1 && i < lineLen; j++, i++) {
					const float advance = Con_Advance( (unsigned char)line[i].f.character );
					if ( con.textWidth > 0.0f && used + advance > con.textWidth
							&& j > CON_TIMESTAMP_LEN ) {
						break;
					}
					used += advance;
					con.text[newline + j] = line[i];
				}

				if (i == lineLen) {
					// Erase remaining chars in case newline wrapped
					for (; j < con.rowwidth - 1; j++)
						con.text[newline + j] = CON_BLANK;

					ni++;
					break;
				}

				con.text[newline + j] = CON_WRAP;
			}
		}

	con.current = ni;

	// Erase con.current line for next CL_ConsolePrint
	int newline = (con.current % con.totallines) * con.rowwidth;
	for (j = 0; j < con.rowwidth; j++)
		con.text[newline + j] = CON_BLANK;

	Con_ClearNotify ();

	con.display = con.current;
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int		charWidth, rowwidth, width;
	float	scale;

	assert(SMALLCHAR_HEIGHT >= SMALLCHAR_WIDTH);

	scale = ((con_scale->value > 0.0f) ? con_scale->value : 1.0f);
	charWidth = scale * SMALLCHAR_WIDTH;

	if (charWidth < 1) {
		charWidth = 1;
		scale = (float)charWidth / SMALLCHAR_WIDTH;
	}

	width = (cls.glconfig.vidWidth / charWidth) - 2;

	if (width < 20) {
		width = 20;
		charWidth = cls.glconfig.vidWidth / 22;
		scale = (float)charWidth / SMALLCHAR_WIDTH;
	}

	if (charWidth < 1) {
		Com_Error(ERR_FATAL, "Con_CheckResize: Window too small to draw a console");
	}

	rowwidth = width + 1 + (con_timestamps->integer ? 0 : CON_TIMESTAMP_LEN);

	con.charWidth = charWidth;
	con.charHeight = scale * SMALLCHAR_HEIGHT;
	con.linewidth = width;
	con.xadjust = ((float)SCREEN_WIDTH) / cls.glconfig.vidWidth;
	con.yadjust = ((float)SCREEN_HEIGHT) / cls.glconfig.vidHeight;

	const float wasTextWidth = con.textWidth;
	Con_UpdateFontMetrics();

	// The input field still scrolls by characters, because field_t counts
	// them. Sizing it by the widest glyph rather than an average one means it
	// scrolls a little early on ordinary text and never runs off the edge on a
	// line of W's - which is the failure worth avoiding of the two.
	{
		float widest = 1.0f;
		for ( int ch = 33 ; ch < 127 ; ch++ ) {
			const float advance = Con_Advance( (unsigned char)ch );
			if ( advance > widest ) {
				widest = advance;
			}
		}
		int fits = (int)( con.textWidth / widest );
		if ( fits < CON_MIN_WIDTH ) {
			fits = CON_MIN_WIDTH;
		}
		g_consoleField.widthInChars = fits - 1;	// Command prompt
	}

	if (con.rowwidth != rowwidth)
	{
		Con_Resize(rowwidth);
	}
	else if (con.initialized && fabsf(con.textWidth - wasTextWidth) > 0.5f)
	{
		// Same number of cells, different number of pixels: the buffer is
		// still the right shape but every wrap point in it is now wrong.
		Con_Resize(rowwidth);
	}
}


/*
==================
Cmd_CompleteTxtName
==================
*/
void Cmd_CompleteTxtName( char *args, int argNum ) {
	if ( argNum == 2 )
		Field_CompleteFilename( "", "txt", qfalse, qtrue );
}

/*
================
Con_Init
================
*/
void Con_Init (void) {
	int		i;

	con_notifytime = Cvar_Get ("con_notifytime", "3", 0);
	con_conspeed = Cvar_Get ("scr_conspeed", "3", 0);
	Cvar_CheckRange (con_conspeed, 1.0f, 100.0f, qfalse);

	con_opacity = Cvar_Get ("con_opacity", "0.8", CVAR_ARCHIVE_ND);
	con_autoclear = Cvar_Get ("con_autoclear", "1", CVAR_ARCHIVE_ND);
	con_height = Cvar_Get ("con_height", "0.5", CVAR_ARCHIVE_ND);

	con_scale = Cvar_Get ("con_scale", "1", CVAR_ARCHIVE_ND);
	con_timestamps = Cvar_Get ("con_timestamps", "0", CVAR_ARCHIVE_ND);

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;
	for ( i = 0 ; i < COMMAND_HISTORY ; i++ ) {
		Field_Clear( &historyEditLines[i] );
		historyEditLines[i].widthInChars = g_console_field_width;
	}

	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("togglemenu", Con_ToggleMenu_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f);
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );

	//Initialize values on first print
	con.initialized = qfalse;
}


/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed (void)
{
	int		i;
	int		line = (con.current % con.totallines) * con.rowwidth;

	// print timestamp on the PREVIOUS line
	{
		time_t t = time( NULL );
		struct tm *tms = localtime( &t );
		char timestamp[CON_TIMESTAMP_LEN + 1];
		const unsigned char color = ColorIndex(COLOR_GREY);

		Com_sprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d] ",
			tms->tm_hour, tms->tm_min, tms->tm_sec);

		for ( i = 0; i < CON_TIMESTAMP_LEN; i++ ) {
			con.text[line + i].f = { color, timestamp[i] };
		}
	}

	// mark time for transparent overlay
	if (con.current >= 0)
		con.times[con.current % NUM_CON_TIMES] = cls.realtime;

	con.x = 0;
	con.xPixels = 0.0f;

	if (con.display == con.current)
		con.display++;
	con.current++;

	line = (con.current % con.totallines) * con.rowwidth;

	for ( i = 0; i < con.rowwidth; i++ )
		con.text[line + i] = CON_BLANK;
}

/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( const char *txt) {
	int		y;
	char			c;
	unsigned char	color;

	// for some demos we don't want to ever show anything on the console
	if ( cl_noprint && cl_noprint->integer ) {
		return;
	}

	if (!con.initialized) {
		Con_Initialize();
	}

	color = ColorIndex(COLOR_WHITE);

	while ( (c = (unsigned char) *txt) != 0 ) {
		if ( Q_IsColorString( (unsigned char*) txt ) ) {
			color = ColorIndex( *(txt+1) );
			txt += 2;
			continue;
		}

		txt++;

		switch (c)
		{
		case '\n':
			Con_Linefeed ();
			break;
		case '\r':
			con.x = 0;
			con.xPixels = 0.0f;
			break;
		default:	// display character and advance
			{
			y = con.current % con.totallines;

			// Wrap on whichever comes first: the line is as wide as it may be
			// drawn, or the row is out of cells. The first is what the reader
			// sees; the second is what keeps this inside the buffer, and with
			// a narrow font it is reached first surprisingly often.
			const float advance = Con_Advance( (unsigned char)c );
			const qboolean tooWide = (qboolean)( con.textWidth > 0.0f
				&& con.xPixels + advance > con.textWidth );
			const qboolean tooMany = (qboolean)( con.x == con.rowwidth - CON_TIMESTAMP_LEN - 1 );

			if ( ( tooWide && con.x > 0 ) || tooMany ) {
				con.text[y * con.rowwidth + CON_TIMESTAMP_LEN + con.x] = CON_WRAP;
				Con_Linefeed();
				y = con.current % con.totallines;
			}

			con.text[y * con.rowwidth + CON_TIMESTAMP_LEN + con.x].f = { color, c };
			con.x++;
			con.xPixels += advance;
			break;
			}
		}
	}


	// mark time for transparent overlay

	if (con.current >= 0)
		con.times[con.current % NUM_CON_TIMES] = cls.realtime;
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
void Con_DrawInput (void) {
	int		y;

	if ( cls.state != CA_DISCONNECTED && !(Key_GetCatcher( ) & KEYCATCH_CONSOLE ) ) {
		return;
	}

	y = con.vislines - con.charHeight * 2;

	const float vy = Con_ToVirtualY( (float)y );
	const int font = cls.consoleFont;

	// The prompt.
	char prompt[2] = { CONSOLE_PROMPT_CHAR, '\0' };
	re.Font_DrawString( (int)Con_ToVirtualX( (float)con.charWidth ), (int)vy, prompt,
		con.color, font, -1, con.fontScale );

	// What has been typed, from wherever the field has scrolled to.
	int drawLen = g_consoleField.widthInChars - 1;
	const int len = (int)strlen( g_consoleField.buffer );
	int prestep = ( len <= drawLen ) ? 0 : g_consoleField.scroll;

	if ( prestep + drawLen > len ) {
		drawLen = len - prestep;
	}
	if ( drawLen < 0 ) {
		drawLen = 0;
	}

	char text[MAX_EDIT_LINE];
	Q_strncpyz( text, g_consoleField.buffer + prestep, drawLen + 1 );

	const float vx = Con_ToVirtualX( (float)( 2 * con.charWidth ) );
	re.Font_DrawString( (int)vx, (int)vy, text, con.color, font, -1, con.fontScale );

	// And the cursor, at the width of everything left of it rather than at a
	// multiple of a character width - there is no such multiple now.
	if ( ( (int)( cls.realtime >> 8 ) & 1 ) == 0 ) {
		char left[MAX_EDIT_LINE];
		int cursor = g_consoleField.cursor - prestep;
		if ( cursor < 0 ) {
			cursor = 0;
		}
		if ( cursor > drawLen ) {
			cursor = drawLen;
		}
		Q_strncpyz( left, text, cursor + 1 );

		// The old cursor was glyph 10 or 11 of the character grid - a block and
		// an overstrike block, which existed in that texture and exist in no
		// font. In a font, 10 is a line feed, and drawing it moved the pen down
		// a row instead of drawing anything.
		const char *caret = kg.key_overstrikeMode ? "_" : "|";
		re.Font_DrawString( (int)( vx + re.Font_StrLenPixels( left, font, con.fontScale ) ),
			(int)vy, caret, con.color, font, -1, con.fontScale );
	}

	// Arrows saying the field has more to either side than is shown.
	const char scrollChar[2] = { CON_SCROLL_L_CHAR, '\0' };
	const float *grey = g_color_table[ColorIndex(COLOR_GREY)];

	if ( g_consoleField.scroll > 0 ) {
		re.Font_DrawString( 0, (int)vy, scrollChar, grey, font, -1, con.fontScale );
	}

	const int pos = Q_PrintStrLenTo( g_consoleField.buffer, g_consoleField.scroll, NULL );
	if ( pos + g_consoleField.widthInChars < Q_PrintStrlen( g_consoleField.buffer ) ) {
		re.Font_DrawString( (int)Con_ToVirtualX( (float)( cls.glconfig.vidWidth - con.charWidth ) ),
			(int)vy, scrollChar, grey, font, -1, con.fontScale );
	}
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	int		v;
	conChar_t		*text;
	int		i;
	int		time;

	const int font = cls.consoleFont;
	const float vx = Con_ToVirtualX( (float)con.charWidth );

	v = 0;
	for (i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if (time > con_notifytime->value*1000)
			continue;
		text = con.text + (i % con.totallines)*con.rowwidth;
		int lineLimit = con.rowwidth;
		if (con_timestamps->integer == 0 || con_timestamps->integer == 2) {
			// don't show timestamps in the notify lines. This used to subtract
			// from a limit declared outside the loop, so by the fourth row it
			// was negative and the rest of the overlay drew nothing.
			text += CON_TIMESTAMP_LEN;
			lineLimit -= CON_TIMESTAMP_LEN;
		}

		// One call for the row. The colour changes ride along as ^n codes,
		// which is what the font system reads anyway, so the run of quads it
		// builds is the same run the per-cell loop used to build one at a time.
		char line[MAX_STRING_CHARS];
		Con_RowToString( text, lineLimit, line, sizeof( line ) );

		re.Font_DrawString( (int)vx, (int)Con_ToVirtualY( (float)v ), line,
			g_color_table[ColorIndex(COLOR_WHITE)], font, -1, con.fontScale );

		v += con.charHeight;
	}

	re.SetColor( NULL );
}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
void Con_DrawSolidConsole( float frac )
{
	int				i, y;
	int				rows;
	conChar_t		*text;
	int				row;
	int				lines;

	lines = cls.glconfig.vidHeight * frac;
	if (lines <= 0)
		return;

	if (lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	// draw the background
	y = frac * SCREEN_HEIGHT - 2;
	if ( y < 1 ) {
		y = 0;
	}
	else {
		// draw the background at full opacity only if fullscreen
		if (frac < 1.0f)
		{
			vec4_t con_color;
			MAKERGBA(con_color, 1.0f, 1.0f, 1.0f, Com_Clamp(0.0f, 1.0f, con_opacity->value));
			re.SetColor(con_color);
		}
		else
		{
			re.SetColor(NULL);
		}
		SCR_DrawPic( 0, 0, SCREEN_WIDTH, y, cls.consoleShader );
	}

	// draw the bottom bar and version number

	re.SetColor( console_color );
	re.DrawStretchPic( 0, y, SCREEN_WIDTH, 2, 0, 0, 0, 0, cls.whiteShader );

	{
		const float versionWidth = re.Font_StrLenPixels( JK_VERSION, cls.consoleFont, con.fontScale );
		re.Font_DrawString(
			(int)( Con_ToVirtualX( (float)( cls.glconfig.vidWidth - con.charWidth ) ) - versionWidth ),
			(int)Con_ToVirtualY( (float)( lines - ( con.charHeight + con.charHeight / 2 ) ) ),
			JK_VERSION, console_color, cls.consoleFont, -1, con.fontScale );
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();

	// draw the text
	con.vislines = lines;
	rows = (lines-con.charHeight)/con.charHeight;		// rows of text to draw

	y = lines - (con.charHeight*3);

	// draw from the bottom up
	if (con.display != con.current)
	{
	// draw arrows to show the buffer is backscrolled
		char arrows[MAX_STRING_CHARS];
		int n = 0;
		for ( float used = 0.0f ; used < con.textWidth && n < (int)sizeof( arrows ) - 5 ; ) {
			arrows[n++] = '^';
			arrows[n++] = '^';		// the font system eats one of a pair
			used += Con_Advance( '^' );
			for ( int k = 0 ; k < 3 && used < con.textWidth ; k++ ) {
				arrows[n++] = ' ';
				used += Con_Advance( ' ' );
			}
		}
		arrows[n] = '\0';

		re.Font_DrawString( (int)Con_ToVirtualX( (float)con.charWidth ),
			(int)Con_ToVirtualY( (float)y ), arrows, console_color,
			cls.consoleFont, -1, con.fontScale );

		y -= con.charHeight;
		rows--;
	}

	row = con.display;

	if ( con.x == 0 ) {
		row--;
	}

	const int font = cls.consoleFont;
	const float vx = Con_ToVirtualX( (float)con.charWidth );

	for (i=0 ; i<rows ; i++, y -= con.charHeight, row--)
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines) {
			// past scrollback wrap point
			continue;
		}

		text = con.text + (row % con.totallines)*con.rowwidth;
		int count = con.rowwidth;
		if (!con_timestamps->integer) {
			text += CON_TIMESTAMP_LEN;
			count -= CON_TIMESTAMP_LEN;
		}

		// One call for the row. The colour changes ride along as ^n codes,
		// which is what the font system reads anyway, so it builds the same run
		// of quads the per-cell loop used to build one at a time.
		char line[MAX_STRING_CHARS];
		Con_RowToString( text, count, line, sizeof( line ) );

		re.Font_DrawString( (int)vx, (int)Con_ToVirtualY( (float)y ), line,
			g_color_table[ColorIndex(COLOR_WHITE)], font, -1, con.fontScale );
	}

	re.SetColor( NULL );
}



/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {
	// check for console width changes from a vid mode change
	Con_CheckResize ();

	// if disconnected, render console full screen
	if ( cls.state == CA_DISCONNECTED ) {
		if ( !( Key_GetCatcher( ) & KEYCATCH_UI) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE ) {
			Con_DrawNotify ();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole (void) {
	// decide on the destination height of the console
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
		con.finalFrac = con_height->value;
	else
		con.finalFrac = 0;				// none visible

	// scroll towards the destination height
	if (con.finalFrac < con.displayFrac)
	{
		con.displayFrac -= con_conspeed->value*cls.realFrametime*0.001;
		if (con.finalFrac > con.displayFrac)
			con.displayFrac = con.finalFrac;

	}
	else if (con.finalFrac > con.displayFrac)
	{
		con.displayFrac += con_conspeed->value*cls.realFrametime*0.001;
		if (con.finalFrac < con.displayFrac)
			con.displayFrac = con.finalFrac;
	}

}


void Con_PageUp( void ) {
	con.display -= 2;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_PageDown( void ) {
	con.display += 2;
	if (con.display > con.current) {
		con.display = con.current;
	}
}

void Con_Top( void ) {
	con.display = con.totallines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_Bottom( void ) {
	con.display = con.current;
}


void Con_Close( void ) {
	Field_Clear( &g_consoleField );
	Con_ClearNotify ();
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CONSOLE );
	con.finalFrac = 0;				// none visible
	con.displayFrac = 0;
}
