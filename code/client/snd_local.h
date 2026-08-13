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

// snd_local.h -- private sound definations

#ifndef SND_LOCAL_H
#define SND_LOCAL_H

#include "../qcommon/q_shared.h"

// Declarations only - the implementation is compiled once, in snd_codec.cpp.
#define DR_MP3_NO_STDIO
#include "dr_libs/dr_mp3.h"
#include "../qcommon/qcommon.h"
#include "snd_public.h"

// Added for Open AL to know when to mute all sounds (e.g when app. loses focus)
void S_AL_MuteAllSounds(qboolean bMute);


//from SND_AMBIENT
extern void AS_Init( void );
extern void AS_Free( void );


#define	PAINTBUFFER_SIZE	1024


// !!! if this is changed, the asm code must change !!!
typedef struct {
	int			left;	// the final values will be clamped to +/- 0x00ffff00 and shifted down
	int			right;
} portable_samplepair_t;


// keep this enum in sync with the table "sSoundCompressionMethodStrings"	-ste
//
typedef enum
{
	ct_16 = 0,		// formerly ct_NONE in EF1, now indicates 16-bit samples (the default)
	ct_MP3,
	//
	ct_NUMBEROF		// used only for array sizing

} SoundCompressionMethod_t;


typedef struct sfx_s {
	short			*pSoundData;
	bool			bDefaultSound;			// couldn't be loaded, so use buzz
	bool			bInMemory;				// not in Memory, set qtrue when loaded, and qfalse when its buffers are freed up because of being old, so can be reloaded
	short			iLastLevelUsedOn;		// used for cacheing purposes
	SoundCompressionMethod_t eSoundCompressionMethod;
	// Non-zero only when eSoundCompressionMethod is ct_MP3, in which case
	// pSoundData holds the compressed file rather than PCM and this is how
	// many bytes of it there are. Each channel that plays this sound opens its
	// own decoder over those bytes; there is no shared decoder state any more,
	// which is what pMP3StreamHeader used to be.
	int				iCompressedDataLen;
	int 			iSoundLengthInSamples;	// length in samples, always kept as 16bit now so this is #shorts (watch for stereo later for music?)
	char 			sSoundName[MAX_QPATH];
	int				iLastTimeUsed;
	float			fVolRange;				// used to set the highest volume this sample has at load time - used for lipsynching

	struct sfx_s	*next;					// only used because of hash table when registering
} sfx_t;

typedef struct {
	int			channels;
	int			samples;				// mono samples in buffer
	int			submission_chunk;		// don't mix less than this #
	int			samplebits;
	int			speed;
	byte		*buffer;
} dma_t;


#define START_SAMPLE_IMMEDIATE	0x7fffffff

// Open AL specific

#define NUM_STREAMING_BUFFERS	4
#define STREAMING_BUFFER_SIZE	4608		// 4 decoded MP3 frames

#define QUEUED		1
#define UNQUEUED	2


// Decoding state for one streamed sound.
//
// This used to live inside channel_t, which meant every one of the 32 mixer
// channels carried a whole decoder - 26,656 bytes of it - plus a 50,000-byte
// sliding window, whether it ever streamed anything or not. channel_t was
// 76,720 bytes and S_PaintChannels walks the array of them every frame, so a
// channel was several cache lines apart from the next one for no reason.
//
// Out of line it is also the only shape a second codec can fit: a decoder's
// state is its own size, and Ogg Vorbis wants considerably more of it than
// MP3 does.
typedef struct soundStream_s
{
	int			codec;			// soundCodec_t; int so this header need not include snd_codec.h
	qboolean	open;
	qboolean	wantStereo;
	int			channels;		// what the source has, not what the caller asked for
	int			rate;
	drmp3		mp3;			// valid while open and codec == CODEC_MP3

	// The decode window. Typical back-request is -3072, so roughly double that
	// is 6000 for safety, then doubled again so the 6K position sits in the
	// middle. Byte offsets, not frames: writePos is how much of the window is
	// filled, windowPos is where the window starts in the whole stream.
	byte		window[50000];
	int			writePos;
	int			windowPos;
} soundStream_t;

typedef struct
{
// back-indented fields new in TA codebase, will re-format when MP3 code finished -ste
// note: field missing in TA: qboolean	loopSound;		// from an S_AddLoopSound call, cleared each frame
//
	int				startSample;	// START_SAMPLE_IMMEDIATE = set immediately on next mix
	int				entnum;			// to allow overriding a specific sound
	soundChannel_t	entchannel;		// to allow overriding a specific sound
	int				leftvol;		// 0-255 volume after spatialization
	int				rightvol;		// 0-255 volume after spatialization
	int				master_vol;		// 0-255 volume before spatialization


	vec3_t		origin;			// only use if fixed_origin is set

	qboolean	fixed_origin;	// use origin instead of fetching entnum's origin
	sfx_t		*thesfx;		// sfx structure
	qboolean	loopSound;		// from an S_AddLoopSound call, cleared each frame
	//
	// Decoding state, out of line on purpose - see soundStream_t above. NULL
	// only for a channel whose owner never wired one; every channel in
	// s_channels and the music channels have theirs for the life of the
	// process, so this is a permanent association rather than an allocation.
	soundStream_t *stream;

} channel_t;


#define	WAV_FORMAT_PCM		1
#define WAV_FORMAT_ADPCM	2	// not actually implemented, but is the value that you get in a header
#define WAV_FORMAT_MP3		3	// not actually used this way, but just ensures we don't match one of the legit formats


typedef struct {
	int			format;
	int			rate;
	int			width;
	int			channels;
	int			samples;
	int			dataofs;		// chunk starts this many bytes from file start
} wavinfo_t;

//====================================================================

#define	MAX_CHANNELS			32
extern	channel_t   s_channels[MAX_CHANNELS];

extern	int		s_paintedtime;
extern	int		s_rawend;
extern	vec3_t	listener_origin;
extern	dma_t	dma;

#define	MAX_RAW_SAMPLES	16384
extern	portable_samplepair_t	s_rawsamples[MAX_RAW_SAMPLES];
portable_samplepair_t *S_GetRawSamplePointer();	// TA added this, but it just returns the s_rawsamples[] array above. Oh well...

extern cvar_t *s_allowDynamicMusic;
extern cvar_t *s_initsound;
extern cvar_t *s_khz;
extern cvar_t *s_mixahead;
extern cvar_t *s_nosound;
extern cvar_t *s_separation;
extern cvar_t *s_show;
extern cvar_t *s_testsound;
extern cvar_t *s_volume;
extern cvar_t *s_volumeVoice;

wavinfo_t GetWavinfo (const char *name, byte *wav, int wavlength);

qboolean S_LoadSound( sfx_t *sfx );


void S_PaintChannels(int endtime);

// picks a channel based on priorities, empty slots, number of channels
channel_t *S_PickChannel(int entnum, int entchannel);

// spatializes a channel
void S_Spatialize(channel_t *ch);


//////////////////////////////////
//
// new stuff from TA codebase

byte	*SND_malloc(int iSize, sfx_t *sfx);
void	 SND_setup();
int		 SND_FreeOldestSound(sfx_t *pButNotThisOne = NULL);
void	 SND_TouchSFX(sfx_t *sfx);

void S_DisplayFreeMemory(void);
void S_memoryLoad(sfx_t *sfx);
//
//////////////////////////////////


#endif	// #ifndef SND_LOCAL_H

