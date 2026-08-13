/*
===========================================================================
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

// The sound codec layer, against a real compressed file.
//
// Replacing a decoder is the kind of change that compiles, links, runs, and is
// wrong: the headless bench starts the engine and draws frames, but it never
// plays a compressed sound, and there are no retail assets in this repository
// to play. Without something like this, "it builds" would have been the whole
// of the evidence - which is how the two defects in sections 14 and 21 of the
// backlog got in.
//
// The fixture is one second of a 440 Hz sine at 22,050 Hz, mono, 32 kbit/s,
// made by tools/verify/make_test_mp3.sh. It is committed rather than generated
// at test time because generating it needs an encoder that a machine running
// the tests has no other reason to have.
//
// This links the real snd_codec.cpp against stubs for the zone and the
// filesystem, so what is under test is the layer itself: the window
// arithmetic, the sniffing, the seeking and the channel folding.

#include "../code/client/snd_codec.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

/*
===============================================================================

Stubs

===============================================================================
*/

// The zone, with the size header the real one keeps so Z_Size can answer.
void *Z_Malloc( int iSize, memtag_t, qboolean bZeroit, int )
{
	size_t *p = (size_t *)malloc( iSize + sizeof( size_t ) );
	*p = (size_t)iSize;
	void *pv = p + 1;
	if ( bZeroit ) {
		memset( pv, 0, iSize );
	}
	return pv;
}

int Z_Free( void *pv )
{
	size_t *p = (size_t *)pv - 1;
	const int iSize = (int)*p;
	free( p );
	return iSize;
}

int Z_Size( void *pv )
{
	return (int)*( (size_t *)pv - 1 );
}

// The filesystem, over a buffer the test fills.
static const unsigned char *s_file = NULL;
static int s_fileLen = 0;
static int s_filePos = 0;

int FS_Read( void *buffer, int len, fileHandle_t )
{
	if ( s_filePos >= s_fileLen ) {
		return 0;
	}
	int n = len;
	if ( s_filePos + n > s_fileLen ) {
		n = s_fileLen - s_filePos;
	}
	memcpy( buffer, s_file + s_filePos, n );
	s_filePos += n;
	return n;
}

int FS_Seek( fileHandle_t, long offset, int origin )
{
	switch ( origin ) {
	case FS_SEEK_CUR:	s_filePos += (int)offset;			break;
	case FS_SEEK_END:	s_filePos = s_fileLen + (int)offset;	break;
	default:			s_filePos = (int)offset;			break;
	}
	return 0;
}

int FS_FTell( fileHandle_t )
{
	return s_filePos;
}

void QDECL Com_Printf( const char *, ... ) {}

/*
===============================================================================

Harness

===============================================================================
*/

static int s_failures = 0;

#define CHECK( cond, ... ) \
	do { \
		if ( !( cond ) ) { \
			printf( "  FAIL " ); printf( __VA_ARGS__ ); printf( "\n" ); \
			s_failures++; \
		} \
	} while ( 0 )

static const int kRate		= 22050;
static const int kFrames	= 22050;	// one second
static const float kToneHz	= 440.0f;

// A sine survives MP3 as a sine; what it does not survive is a bit-exact
// comparison. Measuring the tone's own frequency by counting zero crossings is
// insensitive to the amplitude and phase error the codec does introduce, and
// catches every way of getting this wrong that matters: the wrong sample rate,
// a stereo buffer read as mono, a seek that lands somewhere else.
static float MeasureFrequency( const short *psPcm, int iFrames, int iRate )
{
	int iCrossings = 0;
	for ( int i = 1; i < iFrames; i++ ) {
		if ( ( psPcm[i - 1] < 0 ) != ( psPcm[i] < 0 ) ) {
			iCrossings++;
		}
	}
	return ( (float)iCrossings * (float)iRate ) / ( 2.0f * (float)iFrames );
}

static float Rms( const short *psPcm, int iFrames )
{
	double sum = 0.0;
	for ( int i = 0; i < iFrames; i++ ) {
		sum += (double)psPcm[i] * (double)psPcm[i];
	}
	return (float)sqrt( sum / (double)iFrames );
}

static int RunFixture( const char *psFixture, soundCodec_t expectCodec )
{
	FILE *f = fopen( psFixture, "rb" );
	if ( !f ) {
		printf( "cannot open fixture %s\n", psFixture );
		return 2;
	}
	fseek( f, 0, SEEK_END );
	const long lLen = ftell( f );
	fseek( f, 0, SEEK_SET );
	std::vector<unsigned char> data( (size_t)lLen );
	if ( fread( data.data(), 1, (size_t)lLen, f ) != (size_t)lLen ) {
		printf( "short read on fixture\n" );
		fclose( f );
		return 2;
	}
	fclose( f );

	printf( "fixture %s, %ld bytes\n", psFixture, lLen );

	// --- sniffing -----------------------------------------------------------

	CHECK( S_CodecSniff( data.data(), (int)lLen ) == expectCodec,
		   "fixture sniffed as %s, expected %s",
		   S_CodecName( S_CodecSniff( data.data(), (int)lLen ) ), S_CodecName( expectCodec ) );
	CHECK( S_CodecSniff( "not audio at all", 16 ) == CODEC_NONE, "text sniffed as audio" );
	CHECK( S_CodecSniff( data.data(), 2 ) == CODEC_NONE, "two bytes sniffed as audio" );
	CHECK( S_CodecSniff( NULL, 100 ) == CODEC_NONE, "null sniffed as audio" );

	// --- frame count --------------------------------------------------------

	const int iCount = S_CodecFrameCount( data.data(), (int)lLen );
	// An encoder pads to a whole number of MP3 frames of 1152 samples each and
	// adds its own start delay, so the count is a little over a second's worth.
	// The check is that it is the right order of magnitude, which is what
	// catches the failure that matters: a rate or channel count read wrongly
	// puts this out by a factor of two.
	CHECK( iCount >= kFrames - 1152 && iCount <= kFrames + 2 * 1152,
		   "frame count %d, expected between %d and %d",
		   iCount, kFrames - 1152, kFrames + 2 * 1152 );

	// --- whole-file decode --------------------------------------------------

	short *psPcm = NULL;
	int iFrames = 0, iDecRate = 0, iChannels = 0;
	const qboolean bDecoded = S_CodecDecodeAll( data.data(), (int)lLen, qfalse,
												&psPcm, &iFrames, &iDecRate, &iChannels );
	CHECK( bDecoded == qtrue, "whole-file decode failed" );

	if ( bDecoded ) {
		CHECK( iDecRate == kRate, "decoded rate %d, expected %d", iDecRate, kRate );
		CHECK( iChannels == 1, "decoded channels %d, expected 1", iChannels );
		CHECK( iFrames == iCount, "decoded %d frames, frame count said %d", iFrames, iCount );

		// Skip the encoder delay at the start, where the signal has not yet
		// settled, before measuring.
		const int iSkip = 2048;
		const float fHz = MeasureFrequency( psPcm + iSkip, iFrames - iSkip * 2, iDecRate );
		CHECK( fabsf( fHz - kToneHz ) < 5.0f, "tone measured at %.1f Hz, expected %.1f", fHz, kToneHz );

		const float fRms = Rms( psPcm + iSkip, iFrames - iSkip * 2 );
		CHECK( fRms > 1000.0f, "decoded signal is near silent, rms %.1f", fRms );

		Z_Free( psPcm );
	}

	// --- streaming from memory ----------------------------------------------

	soundStream_t *pStream = (soundStream_t *)calloc( 1, sizeof( soundStream_t ) );

	CHECK( S_CodecStreamOpen( pStream, data.data(), (int)lLen, qfalse ) == qtrue,
		   "stream open failed" );
	CHECK( pStream->rate == kRate, "stream rate %d, expected %d", pStream->rate, kRate );

	// Read the stream in the pattern the mixer uses: successive blocks, each
	// starting where the last one ended. This is what walks the window past its
	// three-quarter mark and makes it scroll, which is the arithmetic most
	// likely to be wrong.
	{
		const int iBlock = 512;
		const int iBlocks = ( iCount - 4096 ) / iBlock;
		std::vector<short> block( iBlock );
		std::vector<short> whole;
		whole.reserve( (size_t)iBlocks * iBlock );

		for ( int i = 0; i < iBlocks; i++ ) {
			S_CodecStreamRead( pStream, i * iBlock, iBlock, block.data() );
			whole.insert( whole.end(), block.begin(), block.end() );
		}

		const int iSkip = 2048;
		const float fHz = MeasureFrequency( whole.data() + iSkip, (int)whole.size() - iSkip, kRate );
		CHECK( fabsf( fHz - kToneHz ) < 5.0f,
			   "streamed tone measured at %.1f Hz, expected %.1f - the window scrolls wrong",
			   fHz, kToneHz );

		const float fRms = Rms( whole.data() + iSkip, (int)whole.size() - iSkip );
		CHECK( fRms > 1000.0f, "streamed signal is near silent, rms %.1f", fRms );
	}

	// --- seeking ------------------------------------------------------------

	CHECK( S_CodecStreamRewind( pStream ) == qtrue, "rewind failed" );

	{
		// Seek to half a second and read from there. The window has to be
		// positioned at the target, not at zero, or the next read looks like a
		// request from before the start of the stream and returns silence.
		const float fSeek = 0.5f;
		CHECK( S_CodecStreamSeekSeconds( pStream, fSeek ) == qtrue, "seek failed" );

		const int iFirst = (int)( fSeek * kRate );
		const int iWant = 8192;
		std::vector<short> after( iWant );
		const qboolean bOk = S_CodecStreamRead( pStream, iFirst, iWant, after.data() );
		CHECK( bOk == qtrue, "read after seek reported end of stream" );

		const float fRms = Rms( after.data(), iWant );
		CHECK( fRms > 1000.0f, "read after seek is silent, rms %.1f - the window is misplaced", fRms );

		const float fHz = MeasureFrequency( after.data(), iWant, kRate );
		CHECK( fabsf( fHz - kToneHz ) < 5.0f, "tone after seek measured at %.1f Hz", fHz );
	}

	S_CodecStreamClose( pStream );
	CHECK( pStream->open == qfalse, "close left the stream open" );

	// --- streaming from a file handle ---------------------------------------

	s_file = data.data();
	s_fileLen = (int)lLen;
	s_filePos = 0;

	CHECK( S_CodecStreamOpenFile( pStream, (fileHandle_t)1, qtrue ) == qtrue,
		   "file-backed stream open failed" );
	CHECK( pStream->rate == kRate, "file stream rate %d, expected %d", pStream->rate, kRate );

	{
		// Asking for stereo from a mono source gets mono back, and the reader
		// has to agree with the decoder about how wide a frame is.
		CHECK( pStream->channels == 1, "file stream channels %d, expected 1", pStream->channels );

		const int iWant = 8192;
		std::vector<short> buf( iWant );
		S_CodecStreamRead( pStream, 0, iWant, buf.data() );

		const int iSkip = 2048;
		const float fHz = MeasureFrequency( buf.data() + iSkip, iWant - iSkip, kRate );
		CHECK( fabsf( fHz - kToneHz ) < 5.0f, "tone from file stream measured at %.1f Hz", fHz );
	}

	S_CodecStreamClose( pStream );
	free( pStream );

	return 0;
}

int main( int argc, char **argv )
{
	const char *psDir = ( argc > 1 ) ? argv[1] : "tools/verify/fixtures";
	char sPath[512];

	// The same checks against both formats. The point of the codec layer is
	// that nothing above it can tell which one it is holding, so the test
	// should not be able to either.
	snprintf( sPath, sizeof( sPath ), "%s/tone.mp3", psDir );
	if ( RunFixture( sPath, CODEC_MP3 ) != 0 ) {
		return 2;
	}

	snprintf( sPath, sizeof( sPath ), "%s/tone.ogg", psDir );
	if ( RunFixture( sPath, CODEC_VORBIS ) != 0 ) {
		return 2;
	}

	if ( s_failures ) {
		printf( "%d check(s) failed\n", s_failures );
		return 1;
	}

	printf( "OK: both formats - sniffing, whole-file decode, window scrolling, seeking, file streaming\n" );
	return 0;
}
