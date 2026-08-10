/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Text, from signed distance fields.
//
// What this replaces: a font was a .fontdat of metrics and a .tga of glyphs
// rasterised once at sixteen to thirty-two pixels, drawn one quad per letter
// straight out of that raster. At the size it was rasterised for it looked
// right. At any other size it was a stretched bitmap, which on a 1080p screen
// with any interface scale above one is exactly what it looks like.
//
// A distance field stores, per texel, how far that texel is from the glyph's
// outline rather than how much ink covers it. The outline is therefore
// wherever the field crosses one half, and it crosses it just as cleanly
// magnified eight times as at its own size. One atlas serves every point size,
// and the shader is four lines. See tools/fontgen for how the atlas is built.
//
// What else goes with the old file: two thousand lines of which the majority
// were not fonts at all but codepages - Korean KSC5601, Taiwanese Big5,
// Japanese Shift-JIS, Chinese GB, Thai TIS with two external tables - plus
// per-language substitute .fontdat files for Russian and Polish. All of that
// existed because a font had 256 glyphs and the world does not fit in 256
// glyphs. An atlas has as many as it is generated with, the strings are UTF-8,
// and the problem does not arise.

#include "tr_local.h"
#include "tr_font.h"
#include "qcommon/qfiles.h"

// Texels of field either side of the outline. This is the generator's default
// and the constant compiled into msdf.frag; a font whose header disagrees is
// refused rather than drawn slightly wrong, because "slightly wrong" here
// means edges that are hard or soft by an amount nobody would trace back to a
// number in a header.
static const float MSDF_PIXEL_RANGE = 4.0f;

#define JKXFONT_MAGIC		(('F'<<24)+('X'<<16)+('K'<<8)+'J')
#define JKXFONT_VERSION		1

#define MAX_SDF_FONTS		16

#pragma pack(push, 1)

typedef struct {
	int				magic;
	unsigned int	version;
	unsigned short	atlasWidth;
	unsigned short	atlasHeight;
	short			pointSize;
	short			lineHeight;
	short			ascender;
	short			descender;
	float			range;
	unsigned int	glyphCount;
} jkxFontHeader_t;

// The atlas rectangle is in texels and integral; the placement is in the units
// the game lays out with and is not. The field can be generated at 48 texels
// per em and reported at 16 point - that is what keeps atlas resolution a free
// choice - and at that ratio a glyph 41 texels wide is 13.67 points, which does
// not survive being an integer.
typedef struct {
	unsigned int	codePoint;
	unsigned short	x, y, w, h;			// where in the atlas
	float			xoff, baseline;		// where against the pen
	float			gw, gh;				// and how big it is drawn
	float			advance;
} jkxFontGlyph_t;

#pragma pack(pop)

typedef struct {
	char			name[MAX_QPATH];
	qhandle_t		shader;

	unsigned short	atlasWidth;
	unsigned short	atlasHeight;
	short			pointSize;
	short			lineHeight;
	short			ascender;
	short			descender;

	jkxFontGlyph_t	*glyphs;			// sorted by code point
	int				numGlyphs;

	const jkxFontGlyph_t *missing;		// what an unknown code point draws as
} sdfFont_t;

static sdfFont_t	sdfFonts[MAX_SDF_FONTS];
static int			numSdfFonts;

/*
=================
SDF_GetFont

Handles carry the STYLE_ bits in their top byte, so the index has to be masked
out before it means anything. Zero is not a font: it is what RE_RegisterFont
returns when it could not load one, and every caller passes it back to us.
=================
*/
static sdfFont_t *SDF_GetFont( int handle )
{
	const int index = ( handle & SET_MASK ) - 1;

	if ( index < 0 || index >= numSdfFonts ) {
		return NULL;
	}
	if ( sdfFonts[index].numGlyphs == 0 ) {
		return NULL;
	}
	return &sdfFonts[index];
}

/*
=================
SDF_FindGlyph

Binary search. Every character of every string drawn does one of these, which
is why the generator sorts the table rather than leaving it in whatever order
the charset was written in.
=================
*/
static const jkxFontGlyph_t *SDF_FindGlyph( const sdfFont_t *font, unsigned int cp )
{
	int lo = 0;
	int hi = font->numGlyphs - 1;

	while ( lo <= hi ) {
		const int mid = ( lo + hi ) >> 1;
		const unsigned int at = font->glyphs[mid].codePoint;

		if ( at == cp ) {
			return &font->glyphs[mid];
		}
		if ( at < cp ) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return font->missing;
}

/*
===============================================================================

UTF-8

The old reader looked at a byte, decided from the language setting which
codepage it was in, and pulled one or two bytes accordingly. There is one
encoding now, and this is it.

A byte that is not valid UTF-8 is taken as Latin-1 rather than rejected. That
is not tolerance for its own sake: the string packages still shipping in the
game are codepage bytes, and a stray 0xE9 in an untranslated string should
come out as an e-acute and not as a replacement character or a resynchronising
skip that eats the next two letters.

===============================================================================
*/

unsigned int AnyLanguage_ReadCharFromString( char *psText, int *piAdvanceCount, qboolean *pbIsTrailingPunctuation )
{
	const unsigned char *s = (const unsigned char *)psText;
	unsigned int cp;
	int length;

	if ( s[0] < 0x80 ) {
		cp = s[0];
		length = 1;
	} else if ( ( s[0] & 0xE0 ) == 0xC0 && ( s[1] & 0xC0 ) == 0x80 ) {
		cp = ( ( s[0] & 0x1Fu ) << 6 ) | ( s[1] & 0x3Fu );
		length = 2;
		if ( cp < 0x80 ) {			// overlong
			cp = s[0];
			length = 1;
		}
	} else if ( ( s[0] & 0xF0 ) == 0xE0 && ( s[1] & 0xC0 ) == 0x80 && ( s[2] & 0xC0 ) == 0x80 ) {
		cp = ( ( s[0] & 0x0Fu ) << 12 ) | ( ( s[1] & 0x3Fu ) << 6 ) | ( s[2] & 0x3Fu );
		length = 3;
		if ( cp < 0x800 ) {
			cp = s[0];
			length = 1;
		}
	} else if ( ( s[0] & 0xF8 ) == 0xF0 && ( s[1] & 0xC0 ) == 0x80 && ( s[2] & 0xC0 ) == 0x80
			&& ( s[3] & 0xC0 ) == 0x80 ) {
		cp = ( ( s[0] & 0x07u ) << 18 ) | ( ( s[1] & 0x3Fu ) << 12 )
			| ( ( s[2] & 0x3Fu ) << 6 ) | ( s[3] & 0x3Fu );
		length = 4;
		if ( cp < 0x10000 ) {
			cp = s[0];
			length = 1;
		}
	} else {
		// Not UTF-8. Latin-1, then.
		cp = s[0];
		length = 1;
	}

	if ( piAdvanceCount ) {
		*piAdvanceCount = length;
	}
	if ( pbIsTrailingPunctuation ) {
		// Where a line may break after a character. Western punctuation only,
		// which is all the western scripts need; the asian rules that used to
		// live here went with the codepages they were part of.
		*pbIsTrailingPunctuation = (qboolean)( cp == '.' || cp == ',' || cp == '!'
			|| cp == '?' || cp == ';' || cp == ':' || cp == '-' );
	}

	return cp;
}

unsigned int AnyLanguage_ReadCharFromString( char **psText, qboolean *pbIsTrailingPunctuation )
{
	int advance = 0;
	const unsigned int cp = AnyLanguage_ReadCharFromString( *psText, &advance, pbIsTrailingPunctuation );
	*psText += advance;
	return cp;
}

// Both of these were questions about which codepage was in play. There is one
// encoding now and it is not an asian one, so the answers are constants - and
// they stay as functions because the engine asks through the refexport table.
qboolean Language_IsAsian( void )		{ return qfalse; }
qboolean Language_UsesSpaces( void )	{ return qtrue; }

/*
===============================================================================

LOADING

===============================================================================
*/

/*
=================
SDF_UseTextPipeline

The atlas is registered as an ordinary no-mip shader, which is how it gets an
image loaded, a sampler and a place in the 2D draw path. What it must not have
is the ordinary fragment shader, which would sample the field as though it were
a picture of letters and draw a grey smear.

So the stage's pipeline is rebuilt with the text shader type. The 2D pipeline
the draw path derives lazily from this one copies the def, so it inherits the
change; setting it to zero here is what makes it re-derive.
=================
*/
static void SDF_UseTextPipeline( qhandle_t hShader )
{
	shader_t *shader = R_GetShaderByHandle( hShader );

	if ( !shader || !shader->stages[0] || !shader->stages[0]->active ) {
		return;
	}

	shaderStage_t *stage = shader->stages[0];

	Vk_Pipeline_Def def;
	vk_get_pipeline_def( stage->vk_pipeline[0], &def );

	def.shader_type = TYPE_SINGLE_TEXTURE_MSDF;
	def.face_culling = CT_TWO_SIDED;
	def.vk_light_flags = 0;
#ifdef USE_VK_PBR
	def.vk_pbr_flags = 0;
#endif

	// Straight alpha blending. The shader hands back the glyph's coverage in
	// alpha and the requested colour in rgb, so anything else would be a
	// decision about text that belongs to the caller and not to the atlas.
	def.state_bits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;

	stage->vk_pipeline[0] = vk_find_pipeline_ext( 0, &def, qtrue );
	stage->vk_2d_pipeline = 0;
}

/*
=================
SDF_Load
=================
*/
static qboolean SDF_Load( sdfFont_t *font, const char *name )
{
	void *buffer = NULL;
	const long length = FS_ReadFile( va( "fonts/%s.jkxfont", name ), &buffer );

	if ( length < (long)sizeof( jkxFontHeader_t ) || !buffer ) {
		if ( buffer ) {
			FS_FreeFile( buffer );
		}
		return qfalse;
	}

	const jkxFontHeader_t *header = (const jkxFontHeader_t *)buffer;

	if ( header->magic != JKXFONT_MAGIC || header->version != JKXFONT_VERSION ) {
		CL_RefPrintf( PRINT_WARNING, "font %s: not a version %d .jkxfont\n", name, JKXFONT_VERSION );
		FS_FreeFile( buffer );
		return qfalse;
	}

	const long expected = (long)sizeof( jkxFontHeader_t )
		+ (long)header->glyphCount * (long)sizeof( jkxFontGlyph_t );

	if ( length != expected || header->glyphCount == 0 ) {
		CL_RefPrintf( PRINT_WARNING, "font %s: %ld bytes for %u glyphs, expected %ld\n",
			name, length, header->glyphCount, expected );
		FS_FreeFile( buffer );
		return qfalse;
	}

	// The one number that is in two places: here, and as a constant in
	// msdf.frag. If they disagree the antialiasing comes out the wrong width,
	// which is a soft failure nobody would trace back to a font header.
	if ( fabsf( header->range - MSDF_PIXEL_RANGE ) > 0.01f ) {
		CL_RefPrintf( PRINT_WARNING, "font %s: built with a range of %g, the shader is compiled "
			"for %g. Regenerate it with --range %g.\n",
			name, header->range, MSDF_PIXEL_RANGE, MSDF_PIXEL_RANGE );
		FS_FreeFile( buffer );
		return qfalse;
	}

	Q_strncpyz( font->name, name, sizeof( font->name ) );
	font->atlasWidth = header->atlasWidth;
	font->atlasHeight = header->atlasHeight;
	font->pointSize = header->pointSize;
	font->lineHeight = header->lineHeight ? header->lineHeight : header->pointSize;
	font->ascender = header->ascender;
	font->descender = header->descender;
	font->numGlyphs = (int)header->glyphCount;

	const size_t bytes = header->glyphCount * sizeof( jkxFontGlyph_t );
	font->glyphs = (jkxFontGlyph_t *)CL_Malloc( (int)bytes, TAG_HUNKALLOC, qfalse, 4 );
	Com_Memcpy( font->glyphs, (const byte *)buffer + sizeof( jkxFontHeader_t ), bytes );

	FS_FreeFile( buffer );

	// What an unknown code point draws as. A full stop, which is what the old
	// code fell back to, and which is at least visible - a missing glyph that
	// draws nothing looks like a bug in the string rather than in the font.
	font->missing = NULL;
	font->missing = SDF_FindGlyph( font, '.' );

	font->shader = RE_RegisterShaderNoMip( va( "fonts/%s", name ) );
	SDF_UseTextPipeline( font->shader );

	return qtrue;
}

/*
=================
SDF_Fallback

Something to draw with when the font that was asked for is not there.

Returning nothing instead is the worse failure, and it is the one the first
version of this had: RE_RegisterFont returned 0, GetFont returned NULL, and
every string drawn with that handle drew nothing at all. A menu with no text in
it looks like a broken menu, not like a missing file, and there is nothing in
the frame to say which.

ergoec is the body font and the one the interface falls back to elsewhere, so
it is the fallback here too; failing that, whatever loaded first.
=================
*/
static int SDF_Fallback( void )
{
	for ( int i = 0 ; i < numSdfFonts ; i++ ) {
		if ( !Q_stricmp( sdfFonts[i].name, "ergoec" ) ) {
			return i + 1;
		}
	}
	return numSdfFonts > 0 ? 1 : 0;
}

/*
=================
RE_RegisterFont

The names are Raven's - ergoec, arialnb, anewhope, ocr_a - and they stay,
because the .menu files ask for fonts by name and those files are the game's.
What they resolve to is a weight of Libre Franklin; see tools/fontgen.
=================
*/
int RE_RegisterFont( const char *psName )
{
	if ( !psName || !psName[0] ) {
		return 0;
	}

	char name[MAX_QPATH];
	Q_strncpyz( name, psName, sizeof( name ) );
	COM_StripExtension( name, name, sizeof( name ) );

	for ( int i = 0 ; i < numSdfFonts ; i++ ) {
		if ( !Q_stricmp( sdfFonts[i].name, name ) ) {
			return i + 1;
		}
	}

	if ( numSdfFonts >= MAX_SDF_FONTS ) {
		CL_RefPrintf( PRINT_WARNING, "RE_RegisterFont: too many fonts, %s not loaded\n", name );
		return SDF_Fallback();
	}

	sdfFont_t *font = &sdfFonts[numSdfFonts];
	Com_Memset( font, 0, sizeof( *font ) );

	if ( !SDF_Load( font, name ) ) {
		Com_Memset( font, 0, sizeof( *font ) );

		const int fallback = SDF_Fallback();

		// aurabesh is worth naming, because it is the one font that cannot be
		// generated from a TrueType face - it is an invented alphabet, not a
		// typeface - and drawing it in Latin turns alien signage into readable
		// English. That is still better than drawing nothing, but only just,
		// and the way out is one command.
		if ( !Q_stricmp( name, "aurabesh" ) ) {
			CL_RefPrintf( PRINT_WARNING,
				"font aurabesh is missing, so alien text will be drawn in a Latin face. "
				"Generate it from the game's own files: tools/fontgen/msdf.py "
				"--fontdat <base>/fonts/aurabesh.fontdat --fontdat-image "
				"<base>/fonts/aurabesh.tga --out-image assets/fonts/aurabesh.png "
				"--out-meta /tmp/a.json --out-font assets/fonts/aurabesh.jkxfont\n" );
		} else if ( fallback ) {
			CL_RefPrintf( PRINT_WARNING, "font %s is missing, drawing it in %s instead\n",
				name, sdfFonts[fallback - 1].name );
		} else {
			CL_RefPrintf( PRINT_ERROR,
				"font %s is missing and there is no other font loaded, so nothing "
				"will have any text in it. base/fonts should hold a .jkxfont and a "
				".png per font; the build copies them out of assets/fonts, and "
				"tools/fontgen/build_fonts.py makes them.\n", name );
		}

		return fallback;
	}

	numSdfFonts++;
	return numSdfFonts;
}

/*
===============================================================================

MEASURING

===============================================================================
*/

int RE_Font_HeightPixels( const int iFontHandle, const float fScale )
{
	const sdfFont_t *font = SDF_GetFont( iFontHandle );

	if ( !font ) {
		return 0;
	}
	return (int)( font->pointSize * fScale );
}

/*
=================
RE_Font_StrLenPixels

The width of the longest line. This is what every line-breaking loop in the
game calls - CG_ScrollText walks a string adding these until it passes the
screen width, which is how the opening crawl decides where each line ends - so
it has to add exactly what RE_Font_DrawString will move the pen by, and nothing
else.
=================
*/
int RE_Font_StrLenPixels( const char *psText, const int iFontHandle, const float fScale )
{
	const sdfFont_t *font = SDF_GetFont( iFontHandle );

	if ( !font || !psText ) {
		return 0;
	}

	float widest = 0.0f;
	float line = 0.0f;
	char *s = (char *)psText;

	while ( *s ) {
		const unsigned int cp = AnyLanguage_ReadCharFromString( &s );

		if ( cp == '^' && *s >= '0' && *s <= '9' ) {
			s++;
			continue;
		}
		if ( cp == '\n' ) {
			line = 0.0f;
			continue;
		}
		if ( cp == '\r' ) {
			continue;
		}

		const jkxFontGlyph_t *glyph = SDF_FindGlyph( font, cp );
		if ( glyph ) {
			line += glyph->advance * fScale;
		}
		if ( line > widest ) {
			widest = line;
		}
	}

	// Ceiling, because the caller is fitting this into an integer number of
	// pixels and a line that is 100.2 wide does not fit in 100.
	return (int)ceilf( widest );
}

/*
=================
RE_Font_StrLenChars

Characters a reader would count: colour codes and control characters are not
among them.
=================
*/
int RE_Font_StrLenChars( const char *psText )
{
	if ( !psText ) {
		return 0;
	}

	int count = 0;
	char *s = (char *)psText;

	while ( *s ) {
		const unsigned int cp = AnyLanguage_ReadCharFromString( &s );

		if ( cp == '^' && *s >= '0' && *s <= '9' ) {
			s++;
			continue;
		}
		if ( cp == '\n' || cp == '\r' ) {
			continue;
		}
		count++;
	}

	return count;
}

/*
===============================================================================

DRAWING

===============================================================================
*/

void RE_Font_DrawString( int ox, int oy, const char *psText, const float *rgba,
	const int iFontHandle, int iMaxPixelWidth, const float fScale )
{
	static qboolean inShadow = qfalse;

	const sdfFont_t *font = SDF_GetFont( iFontHandle );

	if ( !font || !psText ) {
		return;
	}

	if ( iFontHandle & STYLE_BLINK ) {
		if ( ( Sys_Milliseconds2() >> 7 ) & 1 ) {
			return;
		}
	}

	// The shadow is the same string again, offset and dark. Doing it with a
	// second threshold on the same sample would be one pass instead of two,
	// but it would also be a different shape - a dilated glyph rather than the
	// same glyph moved - and this is what the game has always looked like.
	if ( ( iFontHandle & STYLE_DROPSHADOW ) && !inShadow ) {
		const int offset = (int)( font->pointSize * fScale * 0.075f + 0.5f );
		const vec4_t shadowColour = { 0.15f, 0.15f, 0.15f, rgba ? rgba[3] : 1.0f };

		inShadow = qtrue;
		RE_Font_DrawString( ox + offset, oy + offset, psText, shadowColour,
			iFontHandle & SET_MASK, iMaxPixelWidth, fScale );
		inShadow = qfalse;
	}

	RE_SetColor( rgba );

	const float fox = (float)ox;
	float fx = fox;
	float foy = (float)oy + ( font->lineHeight - ( font->descender >> 1 ) ) * fScale;

	const float invW = 1.0f / (float)font->atlasWidth;
	const float invH = 1.0f / (float)font->atlasHeight;

	char *s = (char *)psText;
	qboolean wouldOverflow = qfalse;

	while ( *s && !wouldOverflow ) {
		const unsigned int cp = AnyLanguage_ReadCharFromString( &s );

		if ( cp == '^' && *s >= '0' && *s <= '9' ) {
			const int index = ColorIndex( *s++ );
			if ( !inShadow ) {
				vec4_t colour;
				Com_Memcpy( colour, g_color_table[index], sizeof( colour ) );
				colour[3] = rgba ? rgba[3] : 1.0f;
				RE_SetColor( colour );
			}
			continue;
		}

		// A caret not followed by a digit is a caret. The old code swallowed it
		// either way, so "3^2" printed as "32" and nobody could type one.

		if ( cp == '\n' ) {
			fx = fox;
			foy += font->pointSize * fScale;
			continue;
		}

		if ( cp == '\r' ) {
			continue;
		}

		const jkxFontGlyph_t *glyph = SDF_FindGlyph( font, cp );
		if ( !glyph ) {
			continue;
		}

		const float advance = glyph->advance * fScale;

		wouldOverflow = (qboolean)( iMaxPixelWidth != -1
			&& ( ( fx + advance ) - fox ) > (float)iMaxPixelWidth );

		if ( !wouldOverflow && glyph->w && glyph->h ) {
			RE_StretchPic(
				fx + glyph->xoff * fScale,
				foy - glyph->baseline * fScale,
				glyph->gw * fScale,
				glyph->gh * fScale,
				glyph->x * invW,
				glyph->y * invH,
				( glyph->x + glyph->w ) * invW,
				( glyph->y + glyph->h ) * invH,
				font->shader );
		}

		if ( !wouldOverflow ) {
			fx += advance;
		}
	}

	RE_SetColor( NULL );
}

/*
===============================================================================

CONSOLE

===============================================================================
*/

void R_FontList_f( void )
{
	CL_RefPrintf( PRINT_ALL, "%-24s %-6s %-6s %s\n", "font", "size", "glyphs", "atlas" );

	for ( int i = 0 ; i < numSdfFonts ; i++ ) {
		const sdfFont_t *font = &sdfFonts[i];
		CL_RefPrintf( PRINT_ALL, "%-24s %-6d %-6d %dx%d\n",
			font->name, font->pointSize, font->numGlyphs,
			font->atlasWidth, font->atlasHeight );
	}
	CL_RefPrintf( PRINT_ALL, "%d of %d\n", numSdfFonts, MAX_SDF_FONTS );
}

void R_InitFonts( void )
{
	numSdfFonts = 0;
	Com_Memset( sdfFonts, 0, sizeof( sdfFonts ) );

	// "fontlist" is registered by tr_init.cpp along with every other renderer
	// command; registering it here as well is how the first version of this
	// announced itself, one line of "already defined" per start.
}

void R_ShutdownFonts( void )
{
	for ( int i = 0 ; i < numSdfFonts ; i++ ) {
		if ( sdfFonts[i].glyphs ) {
			Z_Free( sdfFonts[i].glyphs );
		}
	}

	numSdfFonts = 0;
	Com_Memset( sdfFonts, 0, sizeof( sdfFonts ) );
}
