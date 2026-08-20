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

// q_shared.c -- stateless support routines that are included in each code dll

// This was including game/common_headers.h, which is q_shared.h plus b_local.h,
// cg_local.h, g_navigator.h, g_shared.h and g_functions.h - the whole gamecode
// surface, reached from the file whose own first line calls it "stateless
// support routines that are included in each code dll".
//
// It was the single largest thing holding the engine and the gamecode together.
// The engine is layered below the game, so every include that points the other
// way is a place the two cannot be separated, and this one was worth eleven
// thousand of the thirteen thousand lines that interface measured - from one
// include site, used only to reach q_shared.h the long way round.
//
// GenericParser2.cpp had the same include for the same non-reason and was fixed
// earlier; the note there says so.
#include "q_shared.h"

/*
============
COM_SkipPath
============
*/
char *COM_SkipPath (char *pathname)
{
	char	*last;

	last = pathname;
	while (*pathname)
	{
		if (*pathname=='/')
			last = pathname+1;
		pathname++;
	}
	return last;
}

/*
============
COM_GetExtension
============
*/
const char *COM_GetExtension( const char *name )
{
	const char *dot = strrchr(name, '.'), *slash;
	if (dot && (!(slash = strrchr(name, '/')) || slash < dot))
		return dot + 1;
	else
		return "";
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension( const char *in, char *out, int destsize )
{
	const char *dot = strrchr(in, '.'), *slash;
	if (dot && (!(slash = strrchr(in, '/')) || slash < dot))
		destsize = (destsize < dot-in+1 ? destsize : dot-in+1);

	if ( in == out && destsize > 1 )
		out[destsize-1] = '\0';
	else
		Q_strncpyz(out, in, destsize);
}

/*
============
COM_CompareExtension

string compare the end of the strings and return qtrue if strings match
============
*/
qboolean COM_CompareExtension(const char *in, const char *ext)
{
	int inlen, extlen;

	inlen = strlen(in);
	extlen = strlen(ext);

	if(extlen <= inlen)
	{
		in += inlen - extlen;

		if(!Q_stricmp(in, ext))
			return qtrue;
	}

	return qfalse;
}

/*
==================
COM_DefaultExtension
==================
*/
void COM_DefaultExtension( char *path, int maxSize, const char *extension )
{
	const char *dot = strrchr(path, '.'), *slash;
	if (dot && (!(slash = strrchr(path, '/')) || slash < dot))
		return;
	else
		Q_strcat(path, maxSize, extension);
}

/*
============================================================================

PARSING

============================================================================
*/

static	char	com_token[MAX_TOKEN_CHARS];
//JLFCALLOUT MPNOTUSED
int parseDataCount = 0;   // slot 0 is the floor; see COM_ParseInit
const int MAX_PARSE_DATA = 5;
parseData_t parseData[MAX_PARSE_DATA];

// The session stack has a floor, and slot zero is always on it.
//
// It used to start empty, at -1, and every parser was then obliged to open a
// session before touching COM_ParseExt. Dozens do not - they parse whatever
// buffer they were handed and rely on whichever session someone else left open.
// That worked only because four functions never closed theirs, so there was
// always one lying around; and it stopped working the moment a map load opened
// a fifth, because five is all there are.
//
// Both halves of that are fixed by giving the stack a permanent bottom entry.
// A parser with no session of its own lands on slot zero - its line numbers are
// meaningless, which was already true - and one that opens a session nests
// above it and can close it without leaving anyone parsing on nothing. That is
// what makes closing the four leaks safe, which is what the rest of this change
// then does.
static void COM_ResetParseSlot( int slot )
{
	parseData[slot].com_lines = 1;
	parseData[slot].com_tokenline = 0;
	parseData[slot].fileName[0] = '\0';

	// Cleared so a nested session cannot inherit the buffer of the one below
	// it. PC_ParseExt reads these, and a stale pointer here would be a parse
	// of freed memory rather than an error.
	parseData[slot].bufferStart = NULL;
	parseData[slot].bufferCurrent = NULL;
}

void COM_ParseInit( void )
{
	memset(parseData, 0, sizeof(parseData));
	parseDataCount = 0;
	COM_ResetParseSlot( 0 );
}

void COM_BeginParseSession( void )
{
	// Not under _DEBUG. The line below indexes parseData with this, so running
	// past the end is a write outside the array - which a release build did
	// silently while a debug build stopped with a message.
	if ( parseDataCount + 1 >= MAX_PARSE_DATA )
	{
		Com_Error (ERR_FATAL, "COM_BeginParseSession: cannot nest more than %d parsing sessions.\n", MAX_PARSE_DATA);
		return;
	}

	parseDataCount++;
	COM_ResetParseSlot( parseDataCount );
}

void COM_EndParseSession( void )
{
	// Never pops the floor. A caller that ends a session it did not start is a
	// bug, but the consequence used to be that the next parse anywhere in the
	// engine was fatal - which is a long way from where the mistake was made.
	if ( parseDataCount > 0 )
	{
		parseDataCount--;
	}
#ifdef _DEBUG
	else
	{
		assert (0 && "COM_EndParseSession: called without a starting COM_BeginParseSession.\n");
	}
#endif
}

// The parameter was never used and never matched the declaration in q_shared.h,
// which spells this ( void ). Nothing in the tree called it, so the mismatch
// only surfaced when the renderer did.
int COM_GetCurrentParseLine( void )
{
	// Cannot happen: slot zero is the floor of the stack and COM_EndParseSession
	// will not pop it. Kept as the assertion it now is.
	if(parseDataCount < 0)
		Com_Error(ERR_FATAL, "COM_GetCurrentParseLine: the parse session stack went below its floor");

	if ( parseData[parseDataCount].com_tokenline )
		return parseData[parseDataCount].com_tokenline;

	return parseData[parseDataCount].com_lines;
}

char *COM_Parse( const char **data_p )
{
	return COM_ParseExt( data_p, qtrue );
}

/*
==============
COM_Parse

Parse a token out of a string
Will never return NULL, just empty strings

If "allowLineBreaks" is qtrue then an empty
string will be returned if the next token is
a newline.
==============
*/
const char *SkipWhitespace( const char *data, qboolean *hasNewLines )
{
	int c;

	if(parseDataCount < 0)
		Com_Error(ERR_FATAL, "SkipWhitespace: the parse session stack went below its floor");

	while( (c = *(const unsigned char* /*eurofix*/)data) <= ' ')
	{
		if( !c )
		{
			return NULL;
		}
		if( c == '\n' )
		{
			parseData[parseDataCount].com_lines++;
			*hasNewLines = qtrue;
		}
		data++;
	}

	return data;
}

int COM_Compress( char *data_p ) {
	char *in, *out;
	int c;
	qboolean newline = qfalse, whitespace = qfalse;

	in = out = data_p;
	if (in) {
		while ((c = *in) != 0) {
			// skip double slash comments
			if ( c == '/' && in[1] == '/' ) {
				while (*in && *in != '\n') {
					in++;
				}
			// skip /* */ comments
			} else if ( c == '/' && in[1] == '*' ) {
				while ( *in && ( *in != '*' || in[1] != '/' ) )
					in++;
				if ( *in )
					in += 2;
				// record when we hit a newline
			} else if ( c == '\n' || c == '\r' ) {
				newline = qtrue;
				in++;
				// record when we hit whitespace
			} else if ( c == ' ' || c == '\t') {
				whitespace = qtrue;
				in++;
				// an actual token
			} else {
				// if we have a pending newline, emit it (and it counts as whitespace)
				if (newline) {
					*out++ = '\n';
					newline = qfalse;
					whitespace = qfalse;
				} if (whitespace) {
					*out++ = ' ';
					whitespace = qfalse;
				}

				// copy quoted strings unmolested
				if (c == '"') {
					*out++ = c;
					in++;
					while (1) {
						c = *in;
						if (c && c != '"') {
							*out++ = c;
							in++;
						} else {
							break;
						}
					}
					if (c == '"') {
						*out++ = c;
						in++;
					}
				} else {
					*out = c;
					out++;
					in++;
				}
			}
		}

		*out = 0;
	}
	return out - data_p;
}

char *COM_ParseExt( const char **data_p, qboolean allowLineBreaks )
{
	int c = 0, len;
	qboolean hasNewLines = qfalse;
	const char *data;

	data = *data_p;
	len = 0;
	com_token[0] = 0;
	if(parseDataCount >= 0)
		parseData[parseDataCount].com_tokenline = 0;

	// make sure incoming data is valid
	if ( !data )
	{
		*data_p = NULL;
		return com_token;
	}

	if(parseDataCount < 0)
		Com_Error(ERR_FATAL, "COM_ParseExt: the parse session stack went below its floor");

	while ( 1 )
	{
		// skip whitespace
		data = SkipWhitespace( data, &hasNewLines );
		if ( !data )
		{
			*data_p = NULL;
			return com_token;
		}
		if ( hasNewLines && !allowLineBreaks )
		{
			*data_p = data;
			return com_token;
		}

		c = *data;

		// skip double slash comments
		if ( c == '/' && data[1] == '/' )
		{
			data += 2;
			while (*data && *data != '\n') {
				data++;
			}
		}
		// skip /* */ comments
		else if ( c=='/' && data[1] == '*' )
		{
			data += 2;
			while ( *data && ( *data != '*' || data[1] != '/' ) )
			{
				if ( *data == '\n' )
				{
					parseData[parseDataCount].com_lines++;
				}
				data++;
			}
			if ( *data )
			{
				data += 2;
			}
		}
		else
		{
			break;
		}
	}

	// token starts on this line
	parseData[parseDataCount].com_tokenline = parseData[parseDataCount].com_lines;

	// handle quoted strings
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c=='\"' || !c)
			{
				com_token[len] = 0;
				*data_p = ( char * ) data;
				return com_token;
			}
			if ( c == '\n' )
			{
				parseData[parseDataCount].com_lines++;
			}
			if (len < MAX_TOKEN_CHARS - 1)
			{
				com_token[len] = c;
				len++;
			}
		}
	}

	// parse a regular word
	do
	{
		if (len < MAX_TOKEN_CHARS - 1)
		{
			com_token[len] = c;
			len++;
		}
		data++;
		c = *data;
	} while (c>32);

	com_token[len] = 0;

	*data_p = ( char * ) data;
	return com_token;
}

/*
===============
COM_ParseString
===============
*/
qboolean COM_ParseString( const char **data, const char **s )
{
	*s = COM_ParseExt( data, qfalse );
	if ( s[0] == 0 )
	{
		Com_Printf("unexpected EOF in COM_ParseString\n");
		return qtrue;
	}
	return qfalse;
}

/*
===============
COM_ParseInt
===============
*/
qboolean COM_ParseInt( const char **data, int *i )
{
	const char	*token;

	token = COM_ParseExt( data, qfalse );
	if ( token[0] == 0 )
	{
		Com_Printf( "unexpected EOF in COM_ParseInt\n" );
		return qtrue;
	}

	*i = atoi( token );
	return qfalse;
}

/*
===============
COM_ParseFloat
===============
*/
qboolean COM_ParseFloat( const char **data, float *f )
{
	const char	*token;

	token = COM_ParseExt( data, qfalse );
	if ( token[0] == 0 )
	{
		Com_Printf( "unexpected EOF in COM_ParseFloat\n" );
		return qtrue;
	}

	*f = atof( token );
	return qfalse;
}

/*
===============
COM_ParseVec4
===============
*/
qboolean COM_ParseVec4( const char **buffer, vec4_t *c)
{
	int i;
	float f;

	for (i = 0; i < 4; i++)
	{
		if (COM_ParseFloat(buffer, &f))
		{
			return qtrue;
		}
		(*c)[i] = f;
	}
	return qfalse;
}

/*
==================
COM_MatchToken
==================
*/
void COM_MatchToken( const char **buf_p, const char *match ) {
	const char	*token;

	token = COM_Parse( buf_p );
	if ( strcmp( token, match ) )
	{
		Com_Error( ERR_DROP, "MatchToken: %s != %s", token, match );
	}
}


/*
=================
SkipBracedSection

The next token should be an open brace.
Skips until a matching close brace is found.
Internal brace depths are properly skipped.
=================
*/
qboolean SkipBracedSection ( const char **program) {
	const char			*token;
	int				depth=0;

	if (com_token[0]=='{') {	//for tr_shader which just ate the brace
		depth = 1;
	}
	do {
		token = COM_ParseExt( program, qtrue );
		if( token[1] == 0 ) {
			if( token[0] == '{' ) {
				depth++;

			}
			else if( token[0] == '}' ) {
				depth--;
			}
		}

	} while (depth && *program);

	// Whether the section was balanced. Single-player threw this away; the
	// shader parser wants it, and every existing caller ignores it.
	return (qboolean)( depth == 0 );
}

/*
=================
SkipRestOfLine
=================
*/
void SkipRestOfLine ( const char **data ) {
	const char	*p;
	int		c;

	if(parseDataCount < 0)
		Com_Error(ERR_FATAL, "SkipRestOfLine: the parse session stack went below its floor");

	p = *data;

	if ( !*p )
		return;

	while ( (c = *p++) != 0 ) {
		if ( c == '\n' ) {
			parseData[parseDataCount].com_lines++;
			break;
		}
	}

	*data = p;
}


void Parse1DMatrix ( const char **buf_p, int x, float *m) {
	const char	*token;
	int		i;

	COM_MatchToken( buf_p, "(" );

	for (i = 0 ; i < x ; i++) {
		token = COM_Parse(buf_p);
		m[i] = atof(token);
	}

	COM_MatchToken( buf_p, ")" );
}

void Parse2DMatrix ( const char **buf_p, int y, int x, float *m) {
	int		i;

	COM_MatchToken( buf_p, "(" );

	for (i = 0 ; i < y ; i++) {
		Parse1DMatrix (buf_p, x, m + i * x);
	}

	COM_MatchToken( buf_p, ")" );
}

void Parse3DMatrix ( const char **buf_p, int z, int y, int x, float *m) {
	int		i;

	COM_MatchToken( buf_p, "(" );

	for (i = 0 ; i < z ; i++) {
		Parse2DMatrix (buf_p, y, x, m + i * x*y);
	}

	COM_MatchToken( buf_p, ")" );
}

/*
 ===================
 Com_HexStrToInt
 ===================
 */
int Com_HexStrToInt( const char *str )
{
	if ( !str || !str[ 0 ] )
		return -1;

	// check for hex code
	if( str[ 0 ] == '0' && str[ 1 ] == 'x' )
	{
		size_t i, n = 0;

		for( i = 2; i < strlen( str ); i++ )
		{
			char digit;

			n *= 16;

			digit = tolower( str[ i ] );

			if( digit >= '0' && digit <= '9' )
				digit -= '0';
			else if( digit >= 'a' && digit <= 'f' )
				digit = digit - 'a' + 10;
			else
				return -1;

			n += digit;
		}

		return n;
	}

	return -1;
}


/*
============================================================================

					LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

int QDECL Com_sprintf( char *dest, int size, const char *fmt, ...) {
	int		len;
	va_list		argptr;

	va_start (argptr,fmt);
	len = Q_vsnprintf(dest, size, fmt, argptr);
	va_end (argptr);

	if(len >= size)
		Com_Printf("Com_sprintf: Output length %d too short, require %d bytes.\n", size, len + 1);

	return len;
}

/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
FIXME: make this buffer size safe someday
============
*/
#define	MAX_VA_STRING	32000
#define MAX_VA_BUFFERS 4

char * QDECL va( const char *format, ... )
{
	va_list		argptr;
	static char	string[MAX_VA_BUFFERS][MAX_VA_STRING];	// in case va is called by nested functions
	static int	index = 0;
	char		*buf;

	va_start( argptr, format );
	buf = (char *)&string[index++ & 3];
	Q_vsnprintf( buf, sizeof(*string), format, argptr );
	va_end( argptr );
	return buf;
}

/*
============
Com_TruncateLongString

Assumes buffer is atleast TRUNCATE_LENGTH big
============
*/
void Com_TruncateLongString( char *buffer, const char *s ) {
	int length = strlen( s );

	if ( length <= TRUNCATE_LENGTH )
		Q_strncpyz( buffer, s, TRUNCATE_LENGTH );
	else {
		Q_strncpyz( buffer, s, (TRUNCATE_LENGTH/2) - 3 );
		Q_strcat( buffer, TRUNCATE_LENGTH, " ... " );
		Q_strcat( buffer, TRUNCATE_LENGTH, s + length - (TRUNCATE_LENGTH/2) + 3 );
	}
}

/*
=====================================================================

  INFO STRINGS

=====================================================================
*/

/*
===============
Info_ValueForKey

Searches the string for the given
key and returns the associated value, or an empty string.
FIXME: overflow check?
===============
*/
const char *Info_ValueForKey( const char *s, const char *key ) {
	char	pkey[MAX_INFO_KEY];
	static	char value[2][MAX_INFO_VALUE];	// use two buffers so compares
											// work without stomping on each other
	static	int	valueindex = 0;
	char	*o;

	if ( !s || !key ) {
		return "";
	}

	if ( strlen( s ) >= MAX_INFO_STRING ) {
		Com_Error( ERR_DROP, "Info_ValueForKey: oversize infostring" );
	}

	valueindex ^= 1;
	if (*s == '\\')
		s++;
	while (1)
	{
		o = pkey;
		while (*s != '\\')
		{
			if (!*s)
				return "";
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value[valueindex];

		while (*s != '\\' && *s)
		{
			*o++ = *s++;
		}
		*o = 0;

		if (!Q_stricmp (key, pkey) )
			return value[valueindex];

		if (!*s)
			break;
		s++;
	}

	return "";
}


/*
===================
Info_NextPair

Used to itterate through all the key/value pairs in an info string
===================
*/
void Info_NextPair( const char **head, char key[MAX_INFO_KEY], char value[MAX_INFO_VALUE] ) {
	char	*o;
	const char	*s;

	s = *head;

	if ( *s == '\\' ) {
		s++;
	}
	key[0] = 0;
	value[0] = 0;

	o = key;
	while ( *s != '\\' ) {
		if ( !*s ) {
			*o = 0;
			*head = s;
			return;
		}
		*o++ = *s++;
	}
	*o = 0;
	s++;

	o = value;
	while ( *s != '\\' && *s ) {
		*o++ = *s++;
	}
	*o = 0;

	*head = s;
}


/*
===================
Info_RemoveKey
===================
*/
void Info_RemoveKey( char *s, const char *key ) {
	char	*start;
	char	pkey[MAX_INFO_KEY];
	char	value[MAX_INFO_VALUE];
	char	*o;

	if ( strlen( s ) >= MAX_INFO_STRING ) {
		Com_Error( ERR_DROP, "Info_RemoveKey: oversize infostring" );
	}

	if (strchr (key, '\\')) {
		return;
	}

	while (1)
	{
		start = s;
		if (*s == '\\')
			s++;
		o = pkey;
		while (*s != '\\')
		{
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value;
		while (*s != '\\' && *s)
		{
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;

		//NOTE: static analysis pointed out pkey may not be null-terminated
		if (!strcmp (key, pkey) )
		{
			memmove(start, s, strlen(s) + 1);	// remove this part
			return;
		}

		if (!*s)
			return;
	}
}


/*
==================
Info_Validate

Some characters are illegal in info strings because they
can mess up the server's parsing
==================
*/
qboolean Info_Validate( const char *s ) {
	if ( strchr( s, '\"' ) ) {
		return qfalse;
	}
	if ( strchr( s, ';' ) ) {
		return qfalse;
	}
	return qtrue;
}

/*
==================
Info_SetValueForKey

Changes or adds a key/value pair
==================
*/
void Info_SetValueForKey( char *s, const char *key, const char *value ) {
	char	newi[MAX_INFO_STRING];
	const char* blacklist = "\\;\"";

	if ( strlen( s ) >= MAX_INFO_STRING ) {
		Com_Error( ERR_DROP, "Info_SetValueForKey: oversize infostring" );
	}

	for(; *blacklist; ++blacklist)
	{
		if (strchr (key, *blacklist) || strchr (value, *blacklist))
		{
			Com_Printf (S_COLOR_YELLOW "Can't use keys or values with a '%c': %s = %s\n", *blacklist, key, value);
			return;
		}
	}

	Info_RemoveKey (s, key);
	if (!value || !strlen(value))
		return;

	Com_sprintf (newi, sizeof(newi), "\\%s\\%s", key, value);

	if (strlen(newi) + strlen(s) >= MAX_INFO_STRING)
	{
		Com_Printf ("Info string length exceeded\n");
		return;
	}

	Q_strcat(newi, s);
	// s is a char * and its size is a contract rather than a type: the function
	// opens by erroring out if it is not shorter than MAX_INFO_STRING, and the
	// length check above proves the result fits. That is the size to pass.
	Q_strncpyz(s, newi, MAX_INFO_STRING);
}

/*
==================
Com_CharIsOneOfCharset
==================
*/
static qboolean Com_CharIsOneOfCharset( char c, const char *set ) {
	size_t i;

	for ( i=0; i<strlen( set ); i++ ) {
		if ( set[i] == c )
			return qtrue;
	}

	return qfalse;
}

/*
==================
Com_SkipCharset
==================
*/
char *Com_SkipCharset( char *s, const char *sep ) {
	char *p = s;

	while ( p ) {
		if ( Com_CharIsOneOfCharset( *p, sep ) )
			p++;
		else
			break;
	}

	return p;
}

/*
==================
Com_SkipTokens
==================
*/
char *Com_SkipTokens( char *s, int numTokens, const char *sep ) {
	int sepCount = 0;
	char *p = s;

	while ( sepCount < numTokens ) {
		if ( Com_CharIsOneOfCharset( *p++, sep ) ) {
			sepCount++;
			while ( Com_CharIsOneOfCharset( *p, sep ) )
				p++;
		}
		else if ( *p == '\0' )
			break;
	}

	if ( sepCount == numTokens )
		return p;
	else
		return s;
}


/*
========================================================================

String ID Tables

========================================================================
*/


/*
-------------------------
GetIDForString
-------------------------
*/

int GetIDForString ( const stringID_table_t *table, const char *string )
{
	int	index = 0;

	while ( VALIDSTRING( table[index].name ) )
	{
		if ( !Q_stricmp( table[index].name, string ) )
			return table[index].id;

		index++;
	}

	return -1;
}

/*
-------------------------
GetStringForID
-------------------------
*/

const char *GetStringForID( const stringID_table_t *table, int id )
{
	int	index = 0;

	while ( VALIDSTRING( table[index].name ) )
	{
		if ( table[index].id == id )
			return table[index].name;

		index++;
	}

	return NULL;
}

qboolean Q_InBitflags( const uint32_t *bits, int index, uint32_t bitsPerByte ) {
	return ( bits[index / bitsPerByte] & (1 << (index % bitsPerByte)) ) ? qtrue : qfalse;
}

void Q_AddToBitflags( uint32_t *bits, int index, uint32_t bitsPerByte ) {
	bits[index / bitsPerByte] |= (1 << (index % bitsPerByte));
}

void Q_RemoveFromBitflags( uint32_t *bits, int index, uint32_t bitsPerByte ) {
	bits[index / bitsPerByte] &= ~(1 << (index % bitsPerByte));
}

void *Q_LinearSearch( const void *key, const void *ptr, size_t count,
	size_t size, cmpFunc_t cmp )
{
	size_t i;
	for ( i = 0; i < count; i++ )
	{
		if ( cmp( key, ptr ) == 0 ) return (void *)ptr;
		ptr = (const char *)ptr + size;
	}
	return NULL;
}

/*
============================================================================

WHICH GAME THIS IS

Kept here, in code compiled into the engine AND into both gamecode modules,
because the structures that need to ask are shared between them: the Ghoul2
instance and playerState_t serialise themselves through methods in headers both
sides include, and a method cannot call a function that only one side links.

Each module has its own copy of the flag and each sets it once. The engine sets
it from com_game at startup; a gamecode module sets it from its own identity,
which it still knows at compile time because it is still built per game.

Reading it before it is set answers "Academy", which is the default the engine
also takes - so a module that forgets to set it is wrong about Outcast and right
about Academy, rather than wrong about both.

============================================================================
*/

static qboolean q_isOutcast = qfalse;

void Com_SetOutcast( qboolean isOutcast )
{
	q_isOutcast = isOutcast;
}

qboolean Com_IsOutcast( void )
{
	return q_isOutcast;
}

/*
============================================================================

WHO THIS PROCESS SAYS IT IS

The names are chosen from the flag above rather than from a define, because one
binary can be either game. The home directory is the one that matters: two games
must not share it, or a config and a saved game written by one are found by the
other.

============================================================================
*/

const char *Com_ProductName( void )
{
	return q_isOutcast ? "jkx_jo" : "jkx_ja";
}

const char *Com_WindowTitle( void )
{
	return q_isOutcast ? "JKX: Jedi Outcast" : "JKX: Jedi Academy";
}

const char *Com_ConsoleTitle( void )
{
	return q_isOutcast ? "JKX Console (Outcast)" : "JKX Console (Academy)";
}

const char *Com_HomepathName( void )
{
	// Spelled the way each platform spells such things: a dotted directory in
	// the user's data path on Unix, a capitalised one beside the documents on
	// Windows and macOS.
#ifdef _WIN32
	return q_isOutcast ? "JKX_JO" : "JKX_JA";
#elif defined(MACOS_X)
	return q_isOutcast ? "JKX_JO" : "JKX_JA";
#else
	return q_isOutcast ? "jkx_jo" : "jkx_ja";
#endif
}

const char *Com_ConfigName( void )
{
	static char name[MAX_QPATH];

	Com_sprintf( name, sizeof( name ), "%s.cfg", Com_ProductName() );

	return name;
}
