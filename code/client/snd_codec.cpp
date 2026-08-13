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

soundCodec_t S_CodecSniff( const void *pvData, int iDataLen )
{
	const byte *p = (const byte *)pvData;

	if ( !p || iDataLen < 4 ) {
		return CODEC_NONE;
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

const char *S_CodecName( soundCodec_t codec )
{
	switch ( codec ) {
	case CODEC_MP3:		return "MP3";
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

qboolean S_CodecDecodeAll( const void *pvData, int iDataLen, qboolean bWantStereo,
						   short **ppPcm, int *piFrames, int *piRate, int *piChannels )
{
	if ( S_CodecSniff( pvData, iDataLen ) != CODEC_MP3 ) {
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
	if ( S_CodecSniff( pvData, iDataLen ) != CODEC_MP3 ) {
		return 0;
	}

	drmp3 dec;
	if ( !drmp3_init_memory( &dec, pvData, (size_t)iDataLen, &s_codecAlloc ) ) {
		return 0;
	}

	// For a constant bitrate file this is arithmetic. For a variable one with
	// no Xing header dr_mp3 walks the frames, which costs a pass over the file
	// but no decoding, and is still far cheaper than unpacking it.
	const drmp3_uint64 iFrames = drmp3_get_pcm_frame_count( &dec );
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

	if ( S_CodecSniff( pvData, iDataLen ) != CODEC_MP3 ) {
		return qfalse;
	}

	if ( !drmp3_init_memory( &pStream->mp3, pvData, (size_t)iDataLen, &s_codecAlloc ) ) {
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
		drmp3_uninit( &pStream->mp3 );
		pStream->open = qfalse;
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

	const drmp3_uint64 iFrames = drmp3_read_pcm_frames_s16( &pStream->mp3,
														   (drmp3_uint64)iMaxFrames, psOut );
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

	if ( !drmp3_seek_to_pcm_frame( &pStream->mp3, iFrame ) ) {
		return qfalse;
	}

	const int iBytesPerFrame = (int)sizeof( short )
							 * ( ( pStream->wantStereo && pStream->channels == 2 ) ? 2 : 1 );

	// The window is now positioned at the seek target rather than at zero, so
	// that the next read does not look like a request from the past.
	pStream->writePos	= 0;
	pStream->windowPos	= (int)iFrame * iBytesPerFrame;
	return qtrue;
}

float S_CodecStreamLengthSeconds( const soundStream_t *pStream )
{
	if ( !pStream->open || pStream->rate <= 0 ) {
		return 0.0f;
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

	drmp3 *pMutable = (drmp3 *)&pStream->mp3;
	const drmp3_uint64 iTotal = pMutable->totalPCMFrameCount;
	if ( iTotal == DRMP3_UINT64_MAX || pMutable->currentPCMFrame >= iTotal ) {
		return 0.0f;
	}

	return (float)( iTotal - pMutable->currentPCMFrame ) / (float)pStream->rate;
}
