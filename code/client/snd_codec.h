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

// Compressed audio, without the engine having to know which format.
//
// What the sound code actually needs from a compressed file is three things:
// how long it is, all of it decoded at once, or a window of it decoded on
// demand and seekable by sample. None of those questions mention MP3, and the
// old interface mentioned nothing else - fourteen functions with MP3 in the
// name, over a decoder whose state lived inside channel_t.
//
// Everything below is in whole PCM frames. A frame is one sample per channel,
// so a stereo frame is four bytes and a mono frame is two. Getting that wrong
// is the traditional way to produce sound at half or double speed, so the unit
// is in the parameter names.

#ifndef SND_CODEC_H
#define SND_CODEC_H

#include "snd_local.h"

typedef enum {
	CODEC_NONE = 0,
	CODEC_MP3,
} soundCodec_t;

// What the first few bytes say this is. Never guesses from the file extension:
// the extension is what someone typed, the header is what the data is.
// Registers s_mp3overhead, which decides whether a sound is worth keeping
// compressed. Defined in snd_mem.cpp beside the decision it feeds.
void			S_CodecInitCvars( void );

soundCodec_t	S_CodecSniff( const void *pvData, int iDataLen );

const char *	S_CodecName( soundCodec_t codec );

// Decode the whole thing into a buffer this allocates with Z_Malloc, which the
// caller owns and must Z_Free. Returns qfalse and touches nothing on failure.
//
// bWantStereo asks for two channels; a mono source is not upmixed and reports
// what it has. Without it a stereo source is downmixed, because everything
// except music is positioned by the mixer and cannot be stereo.
qboolean		S_CodecDecodeAll( const void *pvData, int iDataLen, qboolean bWantStereo,
								  short **ppPcm, int *piFrames, int *piRate, int *piChannels );

// How many PCM frames the whole file decodes to, without decoding it where the
// format can say so cheaply. Returns 0 if it cannot be determined.
int				S_CodecFrameCount( const void *pvData, int iDataLen );

// Streaming. The decoder state lives in the soundStream_t the caller owns - see
// snd_local.h for why it is out of line - and the window inside it is this
// layer's business, not the mixer's.
qboolean		S_CodecStreamOpen( soundStream_t *pStream, const void *pvData, int iDataLen,
								   qboolean bWantStereo );

// The same, reading from an open file rather than from memory. The decoder pulls
// what it needs through the filesystem, which replaces the second sliding window
// the music code used to keep over the compressed bytes. The handle must outlive
// the stream and is not closed here.
qboolean		S_CodecStreamOpenFile( soundStream_t *pStream, fileHandle_t f, qboolean bWantStereo );
void			S_CodecStreamClose( soundStream_t *pStream );

// Reads iFrames whole frames starting at iFirstFrame, which may be anywhere at
// or after the start of the window. Returns qfalse at end of stream, having
// zeroed whatever it could not fill.
qboolean		S_CodecStreamRead( soundStream_t *pStream, int iFirstFrame, int iFrames, short *psOut );

// Back to the beginning, or to a time. Both discard the window.
qboolean		S_CodecStreamRewind( soundStream_t *pStream );
qboolean		S_CodecStreamSeekSeconds( soundStream_t *pStream, float fSeconds );

float			S_CodecStreamLengthSeconds( const soundStream_t *pStream );
float			S_CodecStreamRemainingSeconds( const soundStream_t *pStream );

#endif	// SND_CODEC_H
