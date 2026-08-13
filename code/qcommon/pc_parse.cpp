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

// pc_parse.cpp -- a file read into a parse session, and the session read back
//
// These eight functions are a text parser: open a file, take the next token, an
// int, a float, a colour, close it. They are not a menu, and they were in the
// menu system - ui_shared.cpp, six thousand lines in - which had two
// consequences and neither of them was local to the interface.
//
// The state they work on was never the interface's. parseData and
// parseDataCount are declared in q_shared.h and defined in q_shared.cpp, beside
// COM_BeginParseSession which pushes the stack these index; every one of these
// functions is a thin wrapper over a COM_Parse* call on the current slot. The
// data was in qcommon and the wrappers were four directories away.
//
// And the client called them. cgame parses its own files - the head-up display
// among them - through system calls that cl_cgame.cpp forwards, so the engine
// needed declarations for a parser it could only reach through the interface's
// private header. That is the whole of why ui_shared.h was included by the
// client for twenty years, and the reason survived the three system calls that
// actually needed the menus being given proper entry points.
//
// So: here, in qcommon, next to the file system it reads through and the parse
// session it advances. The interface calls them like anyone else.
//
// This is engine-side rather than in q_shared.cpp because it reads files, and
// q_shared.cpp is compiled into the game library too, where FS_ReadFile is not
// linked - the game asks through gi. Hence a separate file rather than eight
// more functions beside the state they use.

#include "../server/exe_headers.h"

/*
===============
PC_StartParseSession
===============
*/
int PC_StartParseSession( const char *fileName, char **buffer )
{
	int	len;

	// Try to open file and read it in.
	len = FS_ReadFile( fileName, (void **)buffer );

	// Not there?
	if ( len > 0 )
	{
		COM_BeginParseSession();

		Q_strncpyz( parseData[parseDataCount].fileName, fileName, sizeof (parseData[0].fileName) );
		parseData[parseDataCount].bufferStart = *buffer;
		parseData[parseDataCount].bufferCurrent = *buffer;
	}

	return len;
}

/*
===============
PC_EndParseSession
===============
*/
void PC_EndParseSession( char *buffer )
{
	COM_EndParseSession();
	FS_FreeFile( buffer );	//let go of the buffer
}

/*
===============
PC_ParseWarning
===============
*/
void PC_ParseWarning( const char *message )
{
	Com_Printf( S_COLOR_YELLOW "WARNING: %s Line #%d of File '%s'\n",
		message, parseData[parseDataCount].com_lines, parseData[parseDataCount].fileName );
}

// These used to test parseDataCount < 0 to mean "PC_StartParseSession has not
// run". The session stack has a floor now, so the counter is never negative and
// that test says nothing. What they actually depend on is the buffer that
// PC_StartParseSession puts in the slot, so that is what is checked - and
// COM_BeginParseSession clears it on the way in, so a session opened by anyone
// else cannot be mistaken for one of these.
char *PC_ParseExt( void )
{
	if ( !parseData[parseDataCount].bufferCurrent )
		Com_Error( ERR_FATAL, "PC_ParseExt: no buffer (be sure to call PC_StartParseSession!)" );
	return COM_ParseExt( &parseData[parseDataCount].bufferCurrent, qtrue );
}

qboolean PC_ParseString( const char **string )
{
	int	hold;

	if ( !parseData[parseDataCount].bufferCurrent )
		Com_Error( ERR_FATAL, "PC_ParseString: no buffer (be sure to call PC_StartParseSession!)" );

	hold = COM_ParseString( &parseData[parseDataCount].bufferCurrent, string );

	while ( hold == 0 && **string == 0 )
	{
		hold = COM_ParseString( &parseData[parseDataCount].bufferCurrent, string );
	}

	return (qboolean)(hold != 0);
}

qboolean PC_ParseInt( int *number )
{
	if ( !parseData[parseDataCount].bufferCurrent )
		Com_Error( ERR_FATAL, "PC_ParseInt: no buffer (be sure to call PC_StartParseSession!)" );

	return COM_ParseInt( &parseData[parseDataCount].bufferCurrent, number );
}

qboolean PC_ParseFloat( float *number )
{
	if ( !parseData[parseDataCount].bufferCurrent )
		Com_Error( ERR_FATAL, "PC_ParseFloat: no buffer (be sure to call PC_StartParseSession!)" );

	return COM_ParseFloat( &parseData[parseDataCount].bufferCurrent, number );
}

qboolean PC_ParseColor( vec4_t *color )
{
	if ( !parseData[parseDataCount].bufferCurrent )
		Com_Error( ERR_FATAL, "PC_ParseColor: no buffer (be sure to call PC_StartParseSession!)" );

	return COM_ParseVec4( &parseData[parseDataCount].bufferCurrent, color );
}
