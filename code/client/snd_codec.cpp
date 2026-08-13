/*
===========================================================================
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

// The one translation unit that compiles a decoder, and the only place in the
// engine that knows an MP3 from anything else.

#include "snd_local.h"
#include "snd_codec.h"

#define DR_MP3_IMPLEMENTATION
// We do our own file access through the filesystem, so the decoder must not
// reach for stdio, and float output is not wanted - the mixer is 16-bit.
#define DR_MP3_NO_STDIO
#include "dr_libs/dr_mp3.h"

// Declarations only; third_party/stb/stb_vorbis.c is compiled as its own C
// translation unit, because it is a .c file and there is no reason to make it
// pretend otherwise.
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb/stb_vorbis.c"

/*
===============================================================================

Allocation

dr_mp3 allocates once when a decoder opens and once more if it builds a seek
table; never per frame. Routing it through the zone rather than malloc is what
makes those bytes show up in /meminfo beside everything else, under the tag
that used to hold the hand-written decoder's state.

===============================================================================
*/

static void *S_Codec_Malloc( size_t sz, void * )
{
	return Z_Malloc( (int)sz, TAG_SND_MP3STREAMHDR, qfalse );
}

static void *S_Codec_Realloc( void *p, size_t sz, void * )
{
	// The zone has no realloc. These are rare - dr_mp3 grows its read buffer
	// while it hunts for the first frame - so allocate, copy what we can and
	// free, rather than adding a zone primitive for one caller.
	void *pNew = Z_Malloc( (int)sz, TAG_SND_MP3STREAMHDR, qfalse );
	if ( p ) {
		const size_t old = (size_t)Z_Size( p );
		memcpy( pNew, p, ( old < sz ) ? old : sz );
		Z_Free( p );
	}
	return pNew;
}

static void S_Codec_Free( void *p, void * )
{
	if ( p ) {
		Z_Free( p );
	}
}

static const drmp3_allocation_callbacks s_codecAlloc = {
	NULL, S_Codec_Malloc, S_Codec_Realloc, S_Codec_Free
};

/*
===============================================================================

Format identification

===============================================================================
*/

static qboolean S_Codec_IsOgg( const void *pvData, int iDataLen );

soundCodec_t S_CodecSniff( const void *pvData, int iDataLen )
{
	const byte *p = (const byte *)pvData;

	if ( !p || iDataLen < 4 ) {
		return CODEC_NONE;
	}

	if ( S_Codec_IsOgg( p, iDataLen ) ) {
		return CODEC_VORBIS;
	}

	// An ID3v2 tag can sit in front of the audio; its header carries the size
	// of the rest, so step over it before looking for a frame.
	if ( p[0] == 'I' && p[1] == 'D' && p[2] == '3' && iDataLen > 10 ) {
		// Syncsafe integer: seven bits per byte, high bit always clear.
		const int iTagSize = 10 + ( ( p[6] & 0x7F ) << 21 | ( p[7] & 0x7F ) << 14
								  | ( p[8] & 0x7F ) << 7  | ( p[9] & 0x7F ) );
		if ( iTagSize > 0 && iTagSize + 2 < iDataLen ) {
			p += iTagSize;
			iDataLen -= iTagSize;
		}
	}

	// MPEG audio frame sync: eleven set bits, and a version and layer that are
	// not the reserved values.
	if ( iDataLen >= 2 && p[0] == 0xFF && ( p[1] & 0xE0 ) == 0xE0
		 && ( p[1] & 0x18 ) != 0x08 && ( p[1] & 0x06 ) != 0x00 ) {
		return CODEC_MP3;
	}

	return CODEC_NONE;
}

// Ogg is checked before the ID3 skip above, because an Ogg page starts with
// its own four-byte capture pattern and nothing else in it can look like one.
static qboolean S_Codec_IsOgg( const void *pvData, int iDataLen )
{
	const byte *p = (const byte *)pvData;
	return (qboolean)( p && iDataLen >= 4
					   && p[0] == 'O' && p[1] == 'g' && p[2] == 'g' && p[3] == 'S' );
}

const char *S_CodecName( soundCodec_t codec )
{
	switch ( codec ) {
	case CODEC_MP3:		return "MP3";
	case CODEC_VORBIS:	return "Ogg Vorbis";
	default:			return "unrecognised";
	}
}

/*
===============================================================================

Channel conversion

Everything except music is positioned by the mixer and has to be mono. A stereo
source is folded down here rather than in the caller, so that no caller has to
know which formats can be stereo.

===============================================================================
*/

static void S_Codec_DownmixInPlace( short *psPcm, drmp3_uint64 iFrames )
{
	for ( drmp3_uint64 i = 0; i < iFrames; i++ ) {
		psPcm[i] = (short)( ( (int)psPcm[i * 2] + (int)psPcm[i * 2 + 1] ) / 2 );
	}
}

/*
===============================================================================

Whole-file decoding

===============================================================================
*/

static qboolean S_Codec_DecodeAllVorbis( const void *pvData, int iDataLen, qboolean bWantStereo,
										short **ppPcm, int *piFrames, int *piRate, int *piChannels )
{
	int iChannels = 0, iRate = 0;
	short *psRaw = NULL;

	// stb_vorbis allocates this with malloc, not through our callbacks, so it
	// is copied into the zone and released again rather than handed upwards.
	const int iFrames = stb_vorbis_decode_memory( (const unsigned char *)pvData, iDataLen,
												  &iChannels, &iRate, &psRaw );
	if ( iFrames <= 0 || !psRaw ) {
		if ( psRaw ) {
			free( psRaw );
		}
		return qfalse;
	}

	const qboolean bDownmix = (qboolean)( iChannels == 2 && !bWantStereo );
	const int iOutChannels = bDownmix ? 1 : iChannels;
	const int iBytes = iFrames * iOutChannels * (int)sizeof( short );

	short *psPcm = (short *)Z_Malloc( iBytes, TAG_SND_RAWDATA, qfalse );
	if ( bDownmix ) {
		for ( int i = 0; i < iFrames; i++ ) {
			psPcm[i] = (short)( ( (int)psRaw[i * 2] + (int)psRaw[i * 2 + 1] ) / 2 );
		}
	} else {
		memcpy( psPcm, psRaw, iBytes );
	}
	free( psRaw );

	*ppPcm		= psPcm;
	*piFrames	= iFrames;
	*piRate		= iRate;
	*piChannels	= iOutChannels;
	return qtrue;
}

qboolean S_CodecDecodeAll( const void *pvData, int iDataLen, qboolean bWantStereo,
						   short **ppPcm, int *piFrames, int *piRate, int *piChannels )
{
	const soundCodec_t codec = S_CodecSniff( pvData, iDataLen );

	if ( codec == CODEC_VORBIS ) {
		return S_Codec_DecodeAllVorbis( pvData, iDataLen, bWantStereo,
										ppPcm, piFrames, piRate, piChannels );
	}

	if ( codec != CODEC_MP3 ) {
		return qfalse;
	}

	drmp3_config config = {};
	drmp3_uint64 iFrames = 0;
	short *psPcm = drmp3_open_memory_and_read_pcm_frames_s16( pvData, (size_t)iDataLen,
															  &config, &iFrames, &s_codecAlloc );
	if ( !psPcm || iFrames == 0 ) {
		if ( psPcm ) {
			Z_Free( psPcm );
		}
		return qfalse;
	}

	int iChannels = (int)config.channels;
	if ( iChannels == 2 && !bWantStereo ) {
		S_Codec_DownmixInPlace( psPcm, iFrames );
		iChannels = 1;
	}

	*ppPcm		= psPcm;
	*piFrames	= (int)iFrames;
	*piRate		= (int)config.sampleRate;
	*piChannels	= iChannels;
	return qtrue;
}

int S_CodecFrameCount( const void *pvData, int iDataLen )
{
	const soundCodec_t codec = S_CodecSniff( pvData, iDataLen );

	if ( codec == CODEC_VORBIS ) {
		int iError = 0;
		stb_vorbis *pv = stb_vorbis_open_memory( (const unsigned char *)pvData, iDataLen,
												 &iError, NULL );
		if ( !pv ) {
			return 0;
		}
		const int iFrames = (int)stb_vorbis_stream_length_in_samples( pv );
		stb_vorbis_close( pv );
		return iFrames;
	}

	if ( codec != CODEC_MP3 ) {
		return 0;
	}

	drmp3 dec;
	if ( !drmp3_init_memory( &dec, pvData, (size_t)iDataLen, &s_codecAlloc ) ) {
		return 0;
	}

	// Every .mp3 the retail games ship carries a Xing header whose frame count
	// is zero. dr_mp3 reads that header at init and believes it, so asking for
	// the length gives zero, and a sound of zero length is one that does not
	// load - which is what happened to all four hundred and eighty seven of
	// them, including every line of dialogue.
	//
	// A file with no audio in it is not a file anyone shipped, so a zero here
	// means the header is wrong rather than the file empty. Clearing the field
	// puts dr_mp3 on the branch it takes when there is no header at all: a walk
	// over the frames, which reads the headers without decoding and gets the
	// real answer. The hand-written decoder this replaced never looked at Xing,
	// which is why the defect arrived with us rather than being found earlier.
	drmp3_uint64 iFrames;

	if ( dec.totalPCMFrameCount == 0 ) {
		dec.totalPCMFrameCount = DRMP3_UINT64_MAX;

		// The walk counts every frame the encoder wrote, and the encoder wrote
		// more than there is audio: a start-up delay at the front and padding to
		// a whole frame at the back. dr_mp3 skips the delay whatever it knows
		// about the file, but it can only drop the padding when it has a length
		// to subtract it from - which here it does not. So the delay comes off
		// and the padding stays, and this answers the same question the decode
		// answers rather than one eleven hundred frames away from it.
		iFrames = drmp3_get_pcm_frame_count( &dec );

		const drmp3_uint64 iDelay = (drmp3_uint64)dec.delayInPCMFrames;
		iFrames = ( iFrames > iDelay ) ? iFrames - iDelay : 0;
	} else {
		// A truthful header, so this is arithmetic and dr_mp3 does the trimming.
		iFrames = drmp3_get_pcm_frame_count( &dec );
	}

	drmp3_uninit( &dec );

	return (int)iFrames;
}

/*
===============================================================================

Streaming

The window below is the same policy the hand-written code had: decode forward
into a buffer, serve requests out of it, and scroll it by a quarter when the
write position passes three quarters. What changed is that the thing being
asked for a packet of PCM no longer has to be an MP3.

===============================================================================
*/

qboolean S_CodecStreamOpen( soundStream_t *pStream, const void *pvData, int iDataLen,
							qboolean bWantStereo )
{
	S_CodecStreamClose( pStream );

	const soundCodec_t codec = S_CodecSniff( pvData, iDataLen );

	if ( codec == CODEC_VORBIS ) {
		int iError = 0;
		pStream->vorbis = stb_vorbis_open_memory( (const unsigned char *)pvData, iDataLen,
												  &iError, NULL );
		if ( !pStream->vorbis ) {
			return qfalse;
		}

		const stb_vorbis_info info = stb_vorbis_get_info( pStream->vorbis );
		pStream->codec		= CODEC_VORBIS;
		pStream->open		= qtrue;
		pStream->wantStereo	= bWantStereo;
		pStream->channels	= info.channels;
		pStream->rate		= (int)info.sample_rate;
		pStream->writePos	= 0;
		pStream->windowPos	= 0;
		return qtrue;
	}

	if ( codec != CODEC_MP3 ) {
		return qfalse;
	}

	if ( !drmp3_init_memory( &pStream->mp3, pvData, (size_t)iDataLen, &s_codecAlloc ) ) {
		return qfalse;
	}

	// The same lying Xing header as in S_CodecFrameCount, and here it costs a
	// length rather than a load: S_CodecStreamLengthSeconds reads this field, so
	// every streamed line of dialogue would report as zero seconds long. Counted
	// once, at open, because the walk restores the read position and nothing
	// else in the stream's life is a good moment for a pass over the file.
	if ( pStream->mp3.totalPCMFrameCount == 0 ) {
		pStream->mp3.totalPCMFrameCount = DRMP3_UINT64_MAX;
		pStream->mp3.totalPCMFrameCount = drmp3_get_pcm_frame_count( &pStream->mp3 );
	}

	pStream->codec		= CODEC_MP3;
	pStream->open		= qtrue;
	pStream->wantStereo	= bWantStereo;
	pStream->channels	= (int)pStream->mp3.channels;
	pStream->rate		= (int)pStream->mp3.sampleRate;
	pStream->writePos	= 0;
	pStream->windowPos	= 0;
	return qtrue;
}

static size_t S_Codec_FileRead( void *pUserData, void *pBufferOut, size_t bytesToRead )
{
	const fileHandle_t f = (fileHandle_t)(intptr_t)pUserData;
	const int iRead = FS_Read( pBufferOut, (int)bytesToRead, f );
	return ( iRead < 0 ) ? 0 : (size_t)iRead;
}

static drmp3_bool32 S_Codec_FileSeek( void *pUserData, int offset, drmp3_seek_origin origin )
{
	const fileHandle_t f = (fileHandle_t)(intptr_t)pUserData;

	int iWhere;
	switch ( origin ) {
	case DRMP3_SEEK_SET:	iWhere = FS_SEEK_SET;	break;
	case DRMP3_SEEK_CUR:	iWhere = FS_SEEK_CUR;	break;
	case DRMP3_SEEK_END:	iWhere = FS_SEEK_END;	break;
	default:				return DRMP3_FALSE;
	}

	return ( FS_Seek( f, offset, iWhere ) == 0 ) ? DRMP3_TRUE : DRMP3_FALSE;
}

static drmp3_bool32 S_Codec_FileTell( void *pUserData, drmp3_int64 *pCursor )
{
	const fileHandle_t f = (fileHandle_t)(intptr_t)pUserData;
	*pCursor = FS_FTell( f );
	return DRMP3_TRUE;
}

qboolean S_CodecStreamOpenFile( soundStream_t *pStream, fileHandle_t f, qboolean bWantStereo )
{
	S_CodecStreamClose( pStream );

	// Sniff from the front of the file and put the cursor back.
	byte header[4] = {};
	FS_Seek( f, 0, FS_SEEK_SET );
	FS_Read( header, sizeof( header ), f );
	FS_Seek( f, 0, FS_SEEK_SET );

	if ( S_Codec_IsOgg( header, sizeof( header ) ) ) {
		// stb_vorbis decodes from memory and has no pull interface, so a file
		// stream reads the whole track in and owns it. Music is the only thing
		// streamed from a file and dynamic music is already held in memory, so
		// this is the same order of cost as what is there.
		FS_Seek( f, 0, FS_SEEK_END );
		const int iLen = FS_FTell( f );
		FS_Seek( f, 0, FS_SEEK_SET );

		if ( iLen <= 0 ) {
			return qfalse;
		}

		// Deliberately a local until the stream is open. S_CodecStreamOpen
		// begins by closing whatever was there, and closing frees ownedData -
		// so assigning it first hands the open call a pointer that has already
		// been released, and the eventual close frees it a second time.
		byte *pOwned = (byte *)Z_Malloc( iLen, TAG_SND_DYNAMICMUSIC, qfalse );
		if ( FS_Read( pOwned, iLen, f ) != iLen ) {
			Z_Free( pOwned );
			return qfalse;
		}

		if ( !S_CodecStreamOpen( pStream, pOwned, iLen, bWantStereo ) ) {
			Z_Free( pOwned );
			return qfalse;
		}

		pStream->ownedData = pOwned;
		return qtrue;
	}

	if ( !drmp3_init( &pStream->mp3, S_Codec_FileRead, S_Codec_FileSeek, S_Codec_FileTell,
					  NULL, (void *)(intptr_t)f, &s_codecAlloc ) ) {
		return qfalse;
	}

	pStream->codec		= CODEC_MP3;
	pStream->open		= qtrue;
	pStream->wantStereo	= bWantStereo;
	pStream->channels	= (int)pStream->mp3.channels;
	pStream->rate		= (int)pStream->mp3.sampleRate;
	pStream->writePos	= 0;
	pStream->windowPos	= 0;
	return qtrue;
}

void S_CodecStreamClose( soundStream_t *pStream )
{
	if ( pStream->open ) {
		if ( pStream->codec == CODEC_VORBIS ) {
			if ( pStream->vorbis ) {
				stb_vorbis_close( pStream->vorbis );
			}
		} else {
			drmp3_uninit( &pStream->mp3 );
		}
		pStream->open = qfalse;
	}
	pStream->vorbis		= NULL;
	if ( pStream->ownedData ) {
		Z_Free( pStream->ownedData );
		pStream->ownedData = NULL;
	}
	pStream->codec		= CODEC_NONE;
	pStream->writePos	= 0;
	pStream->windowPos	= 0;
}

// One packet forward into the window. Returns bytes written, 0 at end of
// stream. Output is always the number of channels the caller asked for.
static int S_Codec_DecodePacket( soundStream_t *pStream, short *psOut, int iMaxFrames )
{
	if ( !pStream->open ) {
		return 0;
	}

	const qboolean bDownmix = (qboolean)( pStream->channels == 2 && !pStream->wantStereo );
	const int iOutChannels = bDownmix ? 1 : pStream->channels;

	drmp3_uint64 iFrames;
	if ( pStream->codec == CODEC_VORBIS ) {
		// stb_vorbis counts shorts, not frames, and interleaves however many
		// channels it is asked for - so ask for the source's own count and fold
		// afterwards, the same way the MP3 path does.
		iFrames = (drmp3_uint64)stb_vorbis_get_samples_short_interleaved(
						pStream->vorbis, pStream->channels, psOut,
						iMaxFrames * pStream->channels );
	} else {
		iFrames = drmp3_read_pcm_frames_s16( &pStream->mp3,
											 (drmp3_uint64)iMaxFrames, psOut );
	}

	if ( iFrames == 0 ) {
		return 0;
	}

	if ( bDownmix ) {
		S_Codec_DownmixInPlace( psOut, iFrames );
	}

	return (int)iFrames * iOutChannels * (int)sizeof( short );
}

qboolean S_CodecStreamRead( soundStream_t *pStream, int iFirstFrame, int iFrames, short *psOut )
{
	const int iQuarter		=  (int)sizeof( pStream->window ) / 4;
	const int iThreeQuarters= ( (int)sizeof( pStream->window ) * 3 ) / 4;

	const qboolean bStereoOut = (qboolean)( pStream->wantStereo && pStream->channels == 2 );
	const int iBytesPerFrame = (int)sizeof( short ) * ( bStereoOut ? 2 : 1 );

	int iCount = iFrames * iBytesPerFrame;
	int iStart = iFirstFrame * iBytesPerFrame;

	if ( iStart < pStream->windowPos ) {
		// A request from before the window has already been scrolled past. The
		// old code called this time travel and gave up; so do we, but silently,
		// because it happens at the seam of a seek and is not an error.
		memset( psOut, 0, iCount );
		return qfalse;
	}

	qboolean bStillGoing = qtrue;

	while ( !( iStart >= pStream->windowPos
			   && iStart + iCount < pStream->windowPos + pStream->writePos ) )
	{
		// One packet at a time, not "however much room is left".
		//
		// This is not a tuning choice, it is a correctness one, and the test
		// found it: filling the whole window in one call pushes writePos past
		// three quarters immediately, the scroll below then moves windowPos
		// forward by a quarter, and windowPos can overtake the very offset the
		// caller asked for. The old decoder produced 1152 frames per call
		// because that is what an MP3 granule pair is, and the window was sized
		// around that; keeping the same step keeps the same invariant.
		const int iPacketFrames = 1152;
		const int iRoom = (int)sizeof( pStream->window ) - pStream->writePos;
		if ( iRoom < iPacketFrames * iBytesPerFrame ) {
			break;
		}

		const int iBytes = S_Codec_DecodePacket( pStream,
												 (short *)( pStream->window + pStream->writePos ),
												 iPacketFrames );
		if ( iBytes == 0 ) {
			// Out of source data: zero the rest so the mixer reads silence
			// rather than the previous track.
			memset( pStream->window + pStream->writePos, 0,
					sizeof( pStream->window ) - pStream->writePos );
			bStillGoing = qfalse;
			break;
		}

		pStream->writePos += iBytes;

		if ( pStream->writePos > iThreeQuarters ) {
			memmove( pStream->window, pStream->window + iQuarter, iThreeQuarters );
			pStream->writePos  -= iQuarter;
			pStream->windowPos += iQuarter;
		}
	}

	assert( iStart >= pStream->windowPos );
	memcpy( psOut, pStream->window + ( iStart - pStream->windowPos ), iCount );

	return bStillGoing;
}

qboolean S_CodecStreamRewind( soundStream_t *pStream )
{
	if ( !pStream->open ) {
		return qfalse;
	}

	pStream->writePos	= 0;
	pStream->windowPos	= 0;

	if ( pStream->codec == CODEC_VORBIS ) {
		return (qboolean)( stb_vorbis_seek_start( pStream->vorbis ) != 0 );
	}

	return (qboolean)( drmp3_seek_to_pcm_frame( &pStream->mp3, 0 ) != 0 );
}

qboolean S_CodecStreamSeekSeconds( soundStream_t *pStream, float fSeconds )
{
	if ( !pStream->open ) {
		return qfalse;
	}

	// The decoder seeks by frame. This used to be a loop that decoded forward
	// in fast-forward mode until it was within three seconds of the target and
	// then decoded normally, which is what you write when the format cannot
	// tell you where anything is.
	const drmp3_uint64 iFrame = (drmp3_uint64)( fSeconds * (float)pStream->rate );

	if ( pStream->codec == CODEC_VORBIS ) {
		if ( !stb_vorbis_seek( pStream->vorbis, (unsigned int)iFrame ) ) {
			return qfalse;
		}
	} else if ( !drmp3_seek_to_pcm_frame( &pStream->mp3, iFrame ) ) {
		return qfalse;
	}

	// The window restarts at zero, exactly as it does after a rewind, because
	// the offsets a caller passes to S_CodecStreamRead are counted from the
	// last seek and not from the beginning of the file.
	//
	// This used to leave the window standing at the seek target, on the
	// reasoning that the next read would otherwise look like a request from the
	// past. The one caller is MusicInfo_t::SeekTo, and the line after the seek
	// resets its own counter to the start of the track - so every read after a
	// state change in the dynamic music asked for sample zero while the stream
	// said it was thirty seconds in, S_CodecStreamRead saw a request from
	// before its window, and the music stopped. MusicInfo_t::Rewind is the same
	// pair of operations and has always agreed on zero; this is the seek being
	// brought in line with it.
	pStream->writePos	= 0;
	pStream->windowPos	= 0;
	return qtrue;
}

int S_CodecStreamRate( const soundStream_t *pStream )
{
	return pStream->open ? pStream->rate : 0;
}

int S_CodecStreamChannels( const soundStream_t *pStream )
{
	return pStream->open ? pStream->channels : 0;
}

float S_CodecStreamLengthSeconds( const soundStream_t *pStream )
{
	if ( !pStream->open || pStream->rate <= 0 ) {
		return 0.0f;
	}

	if ( pStream->codec == CODEC_VORBIS ) {
		return (float)stb_vorbis_stream_length_in_samples( pStream->vorbis )
			 / (float)pStream->rate;
	}

	// totalPCMFrameCount is what drmp3_get_pcm_frame_count computed when the
	// stream opened, so this costs nothing.
	drmp3 *pMutable = (drmp3 *)&pStream->mp3;
	const drmp3_uint64 iTotal = pMutable->totalPCMFrameCount;
	if ( iTotal == DRMP3_UINT64_MAX ) {
		return 0.0f;
	}

	return (float)iTotal / (float)pStream->rate;
}

float S_CodecStreamRemainingSeconds( const soundStream_t *pStream )
{
	if ( !pStream->open || pStream->rate <= 0 ) {
		return 0.0f;
	}

	if ( pStream->codec == CODEC_VORBIS ) {
		const int iTotal = (int)stb_vorbis_stream_length_in_samples( pStream->vorbis );
		const int iAt = stb_vorbis_get_sample_offset( pStream->vorbis );
		return ( iAt >= iTotal ) ? 0.0f : (float)( iTotal - iAt ) / (float)pStream->rate;
	}

	drmp3 *pMutable = (drmp3 *)&pStream->mp3;
	const drmp3_uint64 iTotal = pMutable->totalPCMFrameCount;
	if ( iTotal == DRMP3_UINT64_MAX || pMutable->currentPCMFrame >= iTotal ) {
		return 0.0f;
	}

	return (float)( iTotal - pMutable->currentPCMFrame ) / (float)pStream->rate;
}
