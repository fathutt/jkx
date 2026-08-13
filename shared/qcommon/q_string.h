#pragma once

#include "q_platform.h"

#if defined(__cplusplus)
extern "C" {
#endif

int Q_isprint( int c );
int Q_isprintext( int c );
int Q_isgraph( int c );
int Q_islower( int c );
int Q_isupper( int c );
int Q_isalpha( int c );
qboolean Q_isanumber( const char *s );
qboolean Q_isintegral( float f );

// portable case insensitive compare
int Q_stricmp(const char *s1, const char *s2);
int	Q_strncmp(const char *s1, const char *s2, int n);
int	Q_stricmpn(const char *s1, const char *s2, int n);
char *Q_strlwr( char *s1 );
char *Q_strupr( char *s1 );
char *Q_strrchr( const char* string, int c );

// buffer size safe library replacements
void Q_strncpyz( char *dest, const char *src, int destsize );
void Q_strcat( char *dest, int size, const char *src );

#if defined(__cplusplus)
}   // extern "C" - the overloads below are C++ and cannot have C linkage

// The same two, with the size taken from the type instead of from the caller.
//
// "Buffer size safe" is what the comment above says, and it is true only as far
// as the number the caller passes. Nothing checks that the number describes the
// buffer, and the two ways of getting it wrong are the two that happen:
// sizeof() of a pointer rather than the array, and a literal that was right
// when it was written.
//
// Where the destination is an array - which it is at most of the call sites
// this tree has - the compiler knows how big it is. These overloads ask it.
// They are not a new API to learn: the call is the same one with the size left
// off, and if the destination is a pointer rather than an array there is no
// overload to match and the size has to be passed as before.
//
// This exists so that replacing a strcpy is not itself a place to introduce a
// bug. There were 91 of those outside the vendored trees when this was written.
template<size_t N>
inline void Q_strncpyz( char (&dest)[N], const char *src ) {
    Q_strncpyz( dest, src, static_cast<int>( N ) );
}

template<size_t N>
inline void Q_strcat( char (&dest)[N], const char *src ) {
    Q_strcat( dest, static_cast<int>( N ), src );
}

extern "C" {
#endif

const char *Q_stristr( const char *s, const char *find);

// strlen that discounts Quake color sequences
int Q_PrintStrlen( const char *string );
int Q_PrintStrLenTo(const char *str, int chars, char *color);

// removes color sequences from string
char *Q_CleanStr( char *string );
void Q_StripColor(char *text);
const char *Q_strchrs( const char *string, const char *search );

void Q_strstrip( char *string, const char *strip, const char *repl );

#if defined (_MSC_VER)
	// vsnprintf is ISO/IEC 9899:1999
	// abstracting this to make it portable
	int Q_vsnprintf( char *str, size_t size, const char *format, va_list args );
#else // not using MSVC
	#define Q_vsnprintf vsnprintf
#endif

#if defined(__cplusplus)
} // extern "C"
#endif
