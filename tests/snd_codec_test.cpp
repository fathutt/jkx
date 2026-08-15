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

	// --- length, which is a different question from the frame count ---------

	// The frame count above is answered by walking the file. The length a
	// STREAM reports is answered from a field the decoder filled in at open,
	// and the two had drifted apart without anything noticing, because nothing
	// asked the second question.
	//
	// dr_mp3 has two ways of saying it does not know: zero, when a Xing header
	// carries a frame count of zero, and DRMP3_UINT64_MAX when there is no
	// usable header at all. The open code handled the first and not the second,
	// so every file without a Xing header - which is most of them, and all of
	// the retail music - reported a length of zero seconds while decoding
	// perfectly well. The dynamic music driver loops a track when fewer than two
	// seconds of it remain, so a track that always had zero remaining was rewound
	// on every frame and never became audible.
	//
	// Both fixtures that lack a header would have failed this from the day they
	// were committed.
	{
		soundStream_t *pLen = (soundStream_t *)calloc( 1, sizeof( soundStream_t ) );

		CHECK( S_CodecStreamOpen( pLen, data.data(), (int)lLen, qfalse ) == qtrue,
			   "stream open failed while checking length" );

		const float fLength		= S_CodecStreamLengthSeconds( pLen );
		const float fExpected	= (float)iCount / (float)kRate;

		CHECK( fLength > 0.0f, "the stream reports a length of %.3f s - "
			   "the decoder was not asked to count the frames it could not read "
			   "from a header", fLength );
		CHECK( fabsf( fLength - fExpected ) < 0.15f,
			   "stream length %.3f s, frame count says %.3f s", fLength, fExpected );

		// And the same for a stream reading off a file handle, which is a
		// separate open and had never been asked at all.
		S_CodecStreamClose( pLen );
		s_file		= data.data();
		s_fileLen	= (int)lLen;
		s_filePos	= 0;
		CHECK( S_CodecStreamOpenFile( pLen, (fileHandle_t)1, qfalse ) == qtrue,
			   "file stream open failed while checking length" );
		CHECK( fabsf( S_CodecStreamLengthSeconds( pLen ) - fExpected ) < 0.15f,
			   "file stream length %.3f s, frame count says %.3f s",
			   S_CodecStreamLengthSeconds( pLen ), fExpected );

		S_CodecStreamClose( pLen );
		free( pLen );
	}

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
		// A seek restarts the offsets the caller passes, exactly as a rewind
		// does. This is asserted from zero rather than from the seek target
		// because getting it the other way round is what stopped the music:
		// MusicInfo_t::SeekTo seeks the stream and then resets its own counter
		// to the start of the track, so every read after a dynamic-music state
		// change asked for sample zero while the stream believed it was thirty
		// seconds in. A request from before the window returns silence, and the
		// track went quiet on the first state change and stayed quiet.
		const float fSeek = 0.5f;
		CHECK( S_CodecStreamSeekSeconds( pStream, fSeek ) == qtrue, "seek failed" );

		const int iWant = 8192;
		std::vector<short> after( iWant );
		const qboolean bOk = S_CodecStreamRead( pStream, 0, iWant, after.data() );
		CHECK( bOk == qtrue, "the first read after a seek reported end of stream" );

		const float fRms = Rms( after.data(), iWant );
		CHECK( fRms > 1000.0f,
			   "the first read after a seek is silent, rms %.1f - the window and the "
			   "caller disagree about where zero is", fRms );

		const float fHz = MeasureFrequency( after.data(), iWant, kRate );
		CHECK( fabsf( fHz - kToneHz ) < 5.0f, "tone after seek measured at %.1f Hz", fHz );
	}

	{
		// A seek that cannot be done still has to leave the stream somewhere the
		// caller can read from.
		//
		// It used to return early, leaving the window where it already was -
		// while MusicInfo_t::SeekTo, which ignores the result, went back to
		// counting from zero. Every read after that was a request from before
		// the window, which is answered with silence, so one entry time in
		// dms.dat that is past the end of the track it names cost the rest of
		// the level's music. Being at the wrong end of a track is recoverable;
		// being permanently silent is not.
		// This does not yet discriminate, and it is worth saying why rather than
		// leaving a check that looks like one. The damage a failed seek does is
		// to leave the window somewhere other than where the caller thinks it
		// is - and these fixtures are one second long, which is 47 kilobytes of
		// mono, which fits inside the 50-kilobyte decode window. The window
		// never scrolls, so it is always at zero, so a seek that leaves it alone
		// and one that resets it are the same thing here.
		//
		// A fixture long enough to scroll the window would make this real, and
		// would also make the "window scrolling" this test claims in its last
		// line true. Until then this checks the return value and that the stream
		// is still readable, which is the half that can be checked.
		CHECK( S_CodecStreamSeekSeconds( pStream, 9999.0f ) == qfalse,
			   "a seek past the end of the stream reported success" );

		const int iWant = 8192;
		std::vector<short> after( iWant );
		const qboolean bOk = S_CodecStreamRead( pStream, 0, iWant, after.data() );
		CHECK( bOk == qtrue, "the stream is unreadable after a seek that failed" );

		const float fRms = Rms( after.data(), iWant );
		CHECK( fRms > 1000.0f,
			   "the stream is silent after a seek that failed, rms %.1f", fRms );
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

// Music, which is the only stereo thing the engine streams and the only thing
// that gets cloned.
//
// Everything above ran against one second of 22 kHz mono, and a mono output
// frame is two bytes while a stereo one is four. Every offset the sliding window
// works in is a byte count derived from a frame count, so a test that only ever
// sees two-byte frames cannot distinguish frames from samples anywhere - and the
// buffer it hands the reader is half the size a stereo read needs, which is a
// heap overflow the moment a real music file is put through it. That is what
// happened: the first retail .mp3 handed to this test overran its output buffer
// before it could report anything about the audio.
static void RunStereo( const char *psFixture )
{
	static const int kStereoRate	= 44100;
	static const float kLeftHz		= 440.0f;
	static const float kRightHz		= 660.0f;

	FILE *f = fopen( psFixture, "rb" );
	if ( !f ) {
		printf( "cannot open fixture %s\n", psFixture );
		s_failures++;
		return;
	}
	fseek( f, 0, SEEK_END );
	const long lLen = ftell( f );
	fseek( f, 0, SEEK_SET );
	std::vector<unsigned char> data( (size_t)lLen );
	if ( fread( data.data(), 1, (size_t)lLen, f ) != (size_t)lLen ) {
		printf( "short read on fixture\n" );
		fclose( f );
		s_failures++;
		return;
	}
	fclose( f );

	printf( "fixture %s, %ld bytes\n", psFixture, lLen );

	soundStream_t *pStream	= (soundStream_t *)calloc( 1, sizeof( soundStream_t ) );
	soundStream_t *pClone	= (soundStream_t *)calloc( 1, sizeof( soundStream_t ) );

	CHECK( S_CodecStreamOpen( pStream, data.data(), (int)lLen, qtrue ) == qtrue,
		   "stereo stream open failed" );
	CHECK( pStream->channels == 2, "stereo fixture reports %d channel(s)", pStream->channels );
	CHECK( pStream->rate == kStereoRate, "stereo fixture rate %d, expected %d",
		   pStream->rate, kStereoRate );

	// The music path asks for 1024 frames at a time and counts frames from the
	// start of the track, which is what this imitates. 4096 frames of stereo is
	// 16,384 bytes, so the 50,000-byte window scrolls on the way through and
	// keeps scrolling.
	const int	iBlock		= 1024;
	const int	iBlocks		= 128;
	std::vector<short> left, right;
	std::vector<short> block( (size_t)iBlock * 2 );

	for ( int i = 0; i < iBlocks; i++ ) {
		const qboolean bOk = S_CodecStreamRead( pStream, i * iBlock, iBlock, block.data() );
		CHECK( bOk == qtrue, "stereo read %d of %d reported end of stream", i, iBlocks );
		if ( !bOk ) {
			break;
		}
		for ( int j = 0; j < iBlock; j++ ) {
			left.push_back( block[j * 2] );
			right.push_back( block[j * 2 + 1] );
		}
	}

	if ( left.size() == (size_t)iBlock * iBlocks ) {
		const int iSkip = 2048;
		const float fLeft = MeasureFrequency( left.data() + iSkip,
											  (int)left.size() - iSkip, kStereoRate );
		const float fRight = MeasureFrequency( right.data() + iSkip,
											   (int)right.size() - iSkip, kStereoRate );

		// Named channels, so the failure says which way it went wrong: both at
		// 440 is a left channel copied over the right, both at 550 is a downmix,
		// half of each is a frame read as a sample.
		CHECK( fabsf( fLeft - kLeftHz ) < 5.0f,
			   "left channel measured at %.1f Hz, expected %.1f", fLeft, kLeftHz );
		CHECK( fabsf( fRight - kRightHz ) < 5.0f,
			   "right channel measured at %.1f Hz, expected %.1f", fRight, kRightHz );
	}

	// --- cloning ------------------------------------------------------------

	// What the crossfader needs and what it used to do by copying the struct
	// that holds the decoder. The two have to be independent in all three ways
	// that matter, and each one of them was broken by the copy.
	const int iAt = iBlock * iBlocks;

	CHECK( S_CodecStreamClone( pClone, pStream ) == qtrue, "clone failed" );

	std::vector<short> fromClone( (size_t)iBlock * 2 );
	std::vector<short> fromSource( (size_t)iBlock * 2 );

	// One: the clone continues from where the source was, sample for sample.
	CHECK( S_CodecStreamRead( pClone, iAt, iBlock, fromClone.data() ) == qtrue,
		   "the clone reported end of stream on its first read" );
	CHECK( S_CodecStreamRead( pStream, iAt, iBlock, fromSource.data() ) == qtrue,
		   "the source reported end of stream after being cloned" );
	CHECK( memcmp( fromClone.data(), fromSource.data(), fromClone.size() * sizeof( short ) ) == 0,
		   "the clone and the source disagree about the same frames" );

	// Two: rewinding the source does not move the clone. This is the exact
	// shape of the defect - the fader kept playing the end of a track while the
	// track it was copied from went back to the beginning - and with a shared
	// window the read below came from before the window and returned silence.
	CHECK( S_CodecStreamRewind( pStream ) == qtrue, "rewind of the source failed" );

	std::vector<short> afterRewind( (size_t)iBlock * 2 );
	CHECK( S_CodecStreamRead( pClone, iAt + iBlock, iBlock, afterRewind.data() ) == qtrue,
		   "the clone stopped when the source was rewound" );
	CHECK( Rms( afterRewind.data(), iBlock * 2 ) > 1000.0f,
		   "the clone went silent when the source was rewound, rms %.1f",
		   Rms( afterRewind.data(), iBlock * 2 ) );

	// Three: closing one does not free anything the other is using. The check is
	// that the read below works and that the sanitiser stage says nothing; a
	// struct copy of a decoder fails this by freeing the same buffers twice.
	S_CodecStreamClose( pStream );

	CHECK( S_CodecStreamRead( pClone, iAt + iBlock * 2, iBlock, afterRewind.data() ) == qtrue,
		   "the clone stopped when the source was closed" );

	S_CodecStreamClose( pClone );

	// A stream that is not backed by memory the caller owns cannot be cloned,
	// and says so rather than handing back a pointer with someone else's
	// lifetime.
	s_file		= data.data();
	s_fileLen	= (int)lLen;
	s_filePos	= 0;
	CHECK( S_CodecStreamOpenFile( pStream, (fileHandle_t)1, qtrue ) == qtrue,
		   "file-backed stereo open failed" );
	CHECK( S_CodecStreamClone( pClone, pStream ) == qfalse,
		   "a file-backed stream reported that it could be cloned" );
	S_CodecStreamClose( pStream );

	// --- downmix ------------------------------------------------------------

	// The same frames asked for as mono. This is exact rather than approximate:
	// the mixer positions every non-music sound itself and cannot be handed
	// stereo, so the fold is defined, and comparing against the stereo pair the
	// test already has proves the mono path reads the same place in the file.
	CHECK( S_CodecStreamOpen( pStream, data.data(), (int)lLen, qfalse ) == qtrue,
		   "downmixed stream open failed" );

	{
		std::vector<short> mono( (size_t)iBlock );
		CHECK( S_CodecStreamRead( pStream, 0, iBlock, mono.data() ) == qtrue,
			   "downmixed read reported end of stream" );

		int iWrong = 0;
		for ( int j = 0; j < iBlock; j++ ) {
			const short sExpect = (short)( ( (int)left[j] + (int)right[j] ) / 2 );
			if ( mono[j] != sExpect ) {
				iWrong++;
			}
		}
		CHECK( iWrong == 0, "%d of %d downmixed frames are not the mean of the pair",
			   iWrong, iBlock );
	}

	S_CodecStreamClose( pStream );

	free( pClone );
	free( pStream );
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

	// The same tone again, carrying a Xing header whose frame count says zero.
	// Every .mp3 the retail games ship is like this, and the decoder reads that
	// header and believes it - so the length came back as zero and the sound did
	// not load. Four hundred and eighty seven of them in one run of the campaign,
	// including every line of dialogue.
	//
	// It runs the whole fixture rather than just the count, because a length
	// that has to be recovered by walking the frames is a length the streaming
	// path reports too, and that is the half that decides how long a line of
	// dialogue is on screen.
	snprintf( sPath, sizeof( sPath ), "%s/tone_zerocount.mp3", psDir );
	if ( RunFixture( sPath, CODEC_MP3 ) != 0 ) {
		return 2;
	}

	// Four seconds of 44 kHz stereo, which is the shape music actually has, and
	// the only shape in which the window scrolls with four-byte frames.
	snprintf( sPath, sizeof( sPath ), "%s/tone_stereo.mp3", psDir );
	RunStereo( sPath );

	// And in the other format, which is where cloning stops being a convenience.
	// A memory-backed MP3 decoder happens to survive being copied byte for byte
	// - it holds no allocation of its own - so the copy the crossfader used to
	// make was wrong in principle and worked in practice, which is why it lived
	// this long. A Vorbis decoder is a pointer to a heap object: copy the struct
	// and two readers share one decoder, and closing either frees it under the
	// other. Running this fixture is what makes the three independence checks
	// below discriminate rather than describe.
	snprintf( sPath, sizeof( sPath ), "%s/tone_stereo.ogg", psDir );
	RunStereo( sPath );

	if ( s_failures ) {
		printf( "%d check(s) failed\n", s_failures );
		return 1;
	}

	printf( "OK: both formats - sniffing, whole-file decode, window scrolling, seeking, "
			"file streaming, stereo, cloning, downmix\n" );
	return 0;
}
