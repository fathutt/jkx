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

// snd_mem.c: sound caching
#include "../server/exe_headers.h"

#include "snd_local.h"
#include "snd_codec.h"

#include <string>

/*
===============================================================================

WAV loading

===============================================================================
*/

byte	*data_p;
byte 	*iff_end;
byte 	*last_chunk;
byte 	*iff_data;
int 	iff_chunk_len;
extern sfx_t		s_knownSfx[];
extern	int			s_numSfx;

extern cvar_t		*s_lip_threshold_1;
extern cvar_t		*s_lip_threshold_2;
extern cvar_t		*s_lip_threshold_3;
extern cvar_t		*s_lip_threshold_4;

short GetLittleShort(void)
{
	short val = 0;
	val = *data_p;
	val = (short)(val + (*(data_p+1)<<8));
	data_p += 2;
	return val;
}

int GetLittleLong(void)
{
	int val = 0;
	val = *data_p;
	val = val + (*(data_p+1)<<8);
	val = val + (*(data_p+2)<<16);
	val = val + (*(data_p+3)<<24);
	data_p += 4;
	return val;
}

void FindNextChunk(const char *name)
{
	while (1)
	{
		data_p=last_chunk;

		if (data_p >= iff_end)
		{	// didn't find the chunk
			data_p = NULL;
			return;
		}

		data_p += 4;
		iff_chunk_len = GetLittleLong();
		if (iff_chunk_len < 0)
		{
			data_p = NULL;
			return;
		}
		data_p -= 8;
		last_chunk = data_p + 8 + ( (iff_chunk_len + 1) & ~1 );
		if (!strncmp((char *)data_p, name, 4))
			return;
	}
}

void FindChunk(const char *name)
{
	last_chunk = iff_data;
	FindNextChunk (name);
}


void DumpChunks(void)
{
	char	str[5];

	str[4] = 0;
	data_p=iff_data;
	do
	{
		memcpy (str, data_p, 4);
		data_p += 4;
		iff_chunk_len = GetLittleLong();
		Com_Printf ("0x%x : %s (%d)\n", (intptr_t)(data_p - 4), str, iff_chunk_len);
		data_p += (iff_chunk_len + 1) & ~1;
	} while (data_p < iff_end);
}

/*
============
GetWavinfo
============
*/
wavinfo_t GetWavinfo (const char *name, byte *wav, int wavlength)
{
	wavinfo_t	info;
	int		samples;

	memset (&info, 0, sizeof(info));

	if (!wav)
		return info;

	iff_data = wav;
	iff_end = wav + wavlength;

// find "RIFF" chunk
	FindChunk("RIFF");
	if (!(data_p && !strncmp((char *)data_p+8, "WAVE", 4)))
	{
		Com_Printf("Missing RIFF/WAVE chunks\n");
		return info;
	}

// get "fmt " chunk
	iff_data = data_p + 12;
// DumpChunks ();

	FindChunk("fmt ");
	if (!data_p)
	{
		Com_Printf("Missing fmt chunk\n");
		return info;
	}
	data_p += 8;
	info.format = GetLittleShort();
	info.channels = GetLittleShort();
	info.rate = GetLittleLong();
	data_p += 4+2;
	info.width = GetLittleShort() / 8;

	if (info.format != 1)
	{
		Com_Printf("Microsoft PCM format only\n");
		return info;
	}


// find data chunk
	FindChunk("data");
	if (!data_p)
	{
		Com_Printf("Missing data chunk\n");
		return info;
	}

	data_p += 4;
	samples = GetLittleLong () / info.width;

	if (info.samples)
	{
		if (samples < info.samples)
			Com_Error (ERR_DROP, "Sound %s has a bad loop length", name);
	}
	else
		info.samples = samples;

	info.dataofs = data_p - wav;


	return info;
}


/*
================
ResampleSfx

resample / decimate to the current source rate
================
*/
void ResampleSfx (sfx_t *sfx, int iInRate, int iInWidth, byte *pData)
{
	int		iOutCount;
	int		iSrcSample;
	float	fStepScale;
	int		i;
	int		iSample;
	unsigned int uiSampleFrac, uiFracStep;	// uiSampleFrac MUST be unsigned, or large samples (eg music tracks) crash

	fStepScale = (float)iInRate / dma.speed;	// this is usually 0.5, 1, or 2

	// When stepscale is > 1 (we're downsampling), we really ought to run a low pass filter on the samples

	iOutCount = (int)(sfx->iSoundLengthInSamples / fStepScale);
	sfx->iSoundLengthInSamples = iOutCount;

	sfx->pSoundData = (short *) SND_malloc( sfx->iSoundLengthInSamples*2 ,sfx );

	sfx->fVolRange	= 0;
	uiSampleFrac	= 0;
	uiFracStep		= (int)(fStepScale*256);

	for (i=0 ; i<sfx->iSoundLengthInSamples ; i++)
	{
		iSrcSample = uiSampleFrac >> 8;
		uiSampleFrac += uiFracStep;
		if (iInWidth == 2) {
			iSample = LittleShort ( ((short *)pData)[iSrcSample] );
		} else {
			iSample = (unsigned int)( (unsigned char)(pData[iSrcSample]) - 128) << 8;
		}

		sfx->pSoundData[i] = (short)iSample;

		// work out max vol for this sample...
		//
		if (iSample < 0)
			iSample = -iSample;
		if (sfx->fVolRange < (iSample >> 8) )
		{
			sfx->fVolRange =  iSample >> 8;
		}
	}
}


//=============================================================================


void S_LoadSound_Finalize(wavinfo_t	*info, sfx_t *sfx, byte *data)
{
	//float	stepscale	= (float)info->rate / dma.speed;
	//int		len			= (int)(info->samples / stepscale);

	//len *= info->width;

	sfx->eSoundCompressionMethod = ct_16;
	sfx->iSoundLengthInSamples	 = info->samples;
	ResampleSfx( sfx, info->rate, info->width, data + info->dataofs );
}





// maybe I'm re-inventing the wheel, here, but I can't see any functions that already do this, so...
//
char *Filename_WithoutPath(const char *psFilename)
{
	static char sString[MAX_QPATH];	// !!
	const char *p = strrchr(psFilename,'\\');

  	if (!p++)
		p=psFilename;

	Q_strncpyz(sString,p);

	return sString;

}

// returns (eg) "\dir\name" for "\dir\name.bmp"
//
char *Filename_WithoutExt(const char *psFilename)
{
	static char sString[MAX_QPATH];	// !

	Q_strncpyz(sString,psFilename);

	char *p = strrchr(sString,'.');
	char *p2= strrchr(sString,'\\');

	// special check, make sure the first suffix we found from the end wasn't just a directory suffix (eg on a path'd filename with no extension anyway)
	//
	if (p && (p2==0 || (p2 && p>p2)))
		*p=0;

	return sString;

}



// Keeping a sound compressed trades memory for CPU, and is only worth it when
// the compressed file plus the decoder's own state is smaller than the PCM
// would be. s_mp3overhead is what the decoder costs; the comparison is the same
// one the old code made, with a real number in place of sizeof(MP3STREAM).
cvar_t *s_compressedOverhead = NULL;

void S_CodecInitCvars( void )
{
	s_compressedOverhead = Cvar_Get( "s_mp3overhead",
									 va( "%d", (int)( sizeof( soundStream_t ) + 5 * 1024 ) ),
									 CVAR_ARCHIVE );
}

static qboolean S_KeepCompressed( sfx_t *sfx, byte *pbSrcData, int iSrcDataLen, int iRawPCMDataSize )
{
	if ( !s_compressedOverhead || iSrcDataLen + s_compressedOverhead->integer >= iRawPCMDataSize ) {
		return qfalse;
	}

	sfx->eSoundCompressionMethod	= ct_MP3;
	sfx->iCompressedDataLen			= iSrcDataLen;
	// 128 is the peak-volume default the old code used when a file carried no
	// tag saying otherwise. Every file lacks that tag now, so it is simply the
	// value: lip synching on a compressed sound uses the mixer's own amplitude
	// (see S_CheckAmplitude), not this.
	sfx->fVolRange					= 128;
	sfx->iSoundLengthInSamples		= ( iRawPCMDataSize / (int)sizeof( short ) ) / ( 44100 / dma.speed );

	sfx->pSoundData = (short *) SND_malloc( iSrcDataLen, sfx );
	memcpy( sfx->pSoundData, pbSrcData, iSrcDataLen );
	return qtrue;
}

// R_CheckMP3s and S_MP3_CalcVols_f lived here: a console command that walked
// sound/ and rewrote every .mp3 in place to carry an ID3v1 comment holding its
// peak volume and unpacked size. Raven's own tool wrote that tag and this
// re-created it, and nothing outside this project has ever read it. The
// replacement decoder answers both questions directly, so the tag is gone and
// so is the command that maintained it.





// adjust filename for foreign languages and WAV/MP3 issues.
//
// returns qfalse if failed to load, else fills in *pData
//
extern	cvar_t	*com_buildScript;
// The extension swaps below write three characters and a terminator over the
// last three characters of the name, so the destination is four bytes wide and
// the size is sizeof("mp3") rather than anything about psFilename. That is
// worth spelling out: this function takes a char * and cannot know how big the
// buffer is, which is why these are the only six string copies in the tree that
// the size-carrying overloads in q_string.h could not take. The arithmetic is
// what makes them safe, not the buffer.
static qboolean S_LoadSound_FileLoadAndNameAdjuster(char *psFilename, byte **pData, int *piSize, int iNameStrlen)
{
	char *psVoice = strstr(psFilename,"chars");
	if (psVoice)
	{
		// cache foreign voices...
		//
		if (com_buildScript->integer)
		{
			fileHandle_t hFile;
			//German
			strncpy(psVoice,"chr_d",5);	// same number of letters as "chars"
			FS_FOpenFileRead(psFilename, &hFile, qfalse);		//cache the wav
			if (!hFile)
			{
				Q_strncpyz(&psFilename[iNameStrlen-3],"mp3",sizeof("mp3"));		//not there try mp3
				FS_FOpenFileRead(psFilename, &hFile, qfalse);	//cache the mp3
			}
			if (hFile)
			{
				FS_FCloseFile(hFile);
			}
			Q_strncpyz(&psFilename[iNameStrlen-3],"wav",sizeof("wav"));	//put it back to wav

			//French
			strncpy(psVoice,"chr_f",5);	// same number of letters as "chars"
			FS_FOpenFileRead(psFilename, &hFile, qfalse);		//cache the wav
			if (!hFile)
			{
				Q_strncpyz(&psFilename[iNameStrlen-3],"mp3",sizeof("mp3"));		//not there try mp3
				FS_FOpenFileRead(psFilename, &hFile, qfalse);	//cache the mp3
			}
			if (hFile)
			{
				FS_FCloseFile(hFile);
			}
			Q_strncpyz(&psFilename[iNameStrlen-3],"wav",sizeof("wav"));	//put it back to wav

			//Spanish
			strncpy(psVoice,"chr_e",5);	// same number of letters as "chars"
			FS_FOpenFileRead(psFilename, &hFile, qfalse);		//cache the wav
			if (!hFile)
			{
				Q_strncpyz(&psFilename[iNameStrlen-3],"mp3",sizeof("mp3"));		//not there try mp3
				FS_FOpenFileRead(psFilename, &hFile, qfalse);	//cache the mp3
			}
			if (hFile)
			{
				FS_FCloseFile(hFile);
			}
			Q_strncpyz(&psFilename[iNameStrlen-3],"wav",sizeof("wav"));	//put it back to wav

			strncpy(psVoice,"chars",5);	//put it back to chars
		}

		// account for foreign voices...
		//
		extern cvar_t* s_language;
		if (s_language && Q_stricmp("DEUTSCH",s_language->string)==0)
		{
			strncpy(psVoice,"chr_d",5);	// same number of letters as "chars"
		}
		else if (s_language && Q_stricmp("FRANCAIS",s_language->string)==0)
		{
			strncpy(psVoice,"chr_f",5);	// same number of letters as "chars"
		}
		else if (s_language && Q_stricmp("ESPANOL",s_language->string)==0)
		{
			strncpy(psVoice,"chr_e",5);	// same number of letters as "chars"
		}
		else
		{
			psVoice = NULL;	// use this ptr as a flag as to whether or not we substituted with a foreign version
		}
	}

	*piSize = FS_ReadFile( psFilename, (void **)pData );	// try WAV
	if ( !*pData ) {
		psFilename[iNameStrlen-3] = 'm';
		psFilename[iNameStrlen-2] = 'p';
		psFilename[iNameStrlen-1] = '3';
		*piSize = FS_ReadFile( psFilename, (void **)pData );	// try MP3

		if ( !*pData )
		{
			//hmmm, not found, ok, maybe we were trying a foreign noise ("arghhhhh.mp3" that doesn't matter?) but it
			// was missing?   Can't tell really, since both types are now in sound/chars. Oh well, fall back to English for now...

			if (psVoice)	// were we trying to load foreign?
			{
				// yep, so fallback to re-try the english...
				//
#ifndef FINAL_BUILD
				Com_Printf(S_COLOR_YELLOW "Foreign file missing: \"%s\"! (using English...)\n",psFilename);
#endif

				strncpy(psVoice,"chars",5);

				psFilename[iNameStrlen-3] = 'w';
				psFilename[iNameStrlen-2] = 'a';
				psFilename[iNameStrlen-1] = 'v';
				*piSize = FS_ReadFile( psFilename, (void **)pData );	// try English WAV
				if ( !*pData )
				{
					psFilename[iNameStrlen-3] = 'm';
					psFilename[iNameStrlen-2] = 'p';
					psFilename[iNameStrlen-1] = '3';
					*piSize = FS_ReadFile( psFilename, (void **)pData );	// try English MP3
				}
			}

			if (!*pData)
			{
				return qfalse;	// sod it, give up...
			}
		}
	}

	return qtrue;
}

// returns qtrue if this dir is allowed to keep loaded MP3s, else qfalse if they should be WAV'd instead...
//
// note that this is passed the original, un-language'd name
//
// (I was going to remove this, but on kejim_post I hit an assert because someone had got an ambient sound when the
//	perimter fence goes online that was an MP3, then it tried to get added as looping. Presumably it sounded ok or
//	they'd have noticed, but we therefore need to stop other levels using those. "sound/ambience" I can check for,
//	but doors etc could be anything. Sigh...)
//
#define SOUND_CHARS_DIR "sound/chars/"
#define SOUND_CHARS_DIR_LENGTH 12 // strlen( SOUND_CHARS_DIR )
static qboolean S_LoadSound_DirIsAllowedToKeepMP3s( const char *psFilename )
{
	if ( Q_stricmpn( psFilename, SOUND_CHARS_DIR, SOUND_CHARS_DIR_LENGTH ) == 0 )
		return qtrue;	// found a dir that's allowed to keep MP3s

	return qfalse;
}

/*
==============
S_LoadSound

The filename may be different than sfx->name in the case
of a forced fallback of a player specific sound	(or of a wav/mp3 substitution now -Ste)
==============
*/
qboolean gbInsideLoadSound = qfalse;
static qboolean S_LoadSound_Actual( sfx_t *sfx )
{
	byte	*data;
	short	*samples;
	wavinfo_t	info;
	int		size;
	char	*psExt;
	char	sLoadName[MAX_QPATH];

	int		len = strlen(sfx->sSoundName);
	if (len<5)
	{
		return qfalse;
	}

	// player specific sounds are never directly loaded...
	//
	if ( sfx->sSoundName[0] == '*') {
		return qfalse;
	}
	// make up a local filename to try wav/mp3 substitutes...
	//
	Q_strncpyz(sLoadName, sfx->sSoundName, sizeof(sLoadName));
	Q_strlwr( sLoadName );
	//
	// Ensure name has an extension (which it must have, but you never know), and get ptr to it...
	//
	psExt = &sLoadName[strlen(sLoadName)-4];
	if (*psExt != '.')
	{
		//Com_Printf( "WARNING: soundname '%s' does not have 3-letter extension\n",sLoadName);
		COM_DefaultExtension(sLoadName,sizeof(sLoadName),".wav");	// so psExt below is always valid
		psExt = &sLoadName[strlen(sLoadName)-4];
		len = strlen(sLoadName);
	}

	if (!S_LoadSound_FileLoadAndNameAdjuster(sLoadName, &data, &size, len))
	{
		return qfalse;
	}

	SND_TouchSFX(sfx);
//=========
	if (S_CodecSniff(data, size) != CODEC_NONE)
	{
		// A compressed file. The extension is not consulted: it is what
		// someone typed, and the header is what the data is.
		//
		{
			const int iFrames = S_CodecFrameCount(data, size);
			const int iRawPCMDataSize = iFrames * (int)sizeof(short);

			if (iFrames == 0)
			{
				Com_Printf(S_COLOR_YELLOW"S_LoadSound: could not read \"%s\" as %s\n",
							sLoadName, S_CodecName(S_CodecSniff(data, size)));
				FS_FreeFile (data);
				return qfalse;
			}

			if (S_LoadSound_DirIsAllowedToKeepMP3s(sfx->sSoundName)	// NOT sLoadName, this uses original un-languaged name
				&&
				S_KeepCompressed(sfx, data, size, iRawPCMDataSize)
				)
			{
//				Com_DPrintf("(Keeping file \"%s\" compressed)\n",sLoadName);

			}
			else
			{
				// small file, not worth keeping as MP3 since it would increase in size (with MP3 header etc)...
				//
				Com_DPrintf("S_LoadSound: Unpacking \"%s\" to wav.\n",sLoadName);
				//
				// unpack and convert into WAV...
				//
				{
					short *psPcm = NULL;
					int iDecodedFrames = 0, iDecodedRate = 0, iDecodedChannels = 0;

					if (!S_CodecDecodeAll(data, size, qfalse, &psPcm, &iDecodedFrames, &iDecodedRate, &iDecodedChannels))
					{
						Com_Printf(S_COLOR_YELLOW"S_LoadSound: failed to decode \"%s\"\n",sLoadName);
						FS_FreeFile (data);
						return qfalse;
					}

					byte *pbUnpackBuffer = (byte *) psPcm;

					{
						// A wavinfo_t so the rest of the load path - resampling
						// and the peak-volume scan lip synching needs - does not
						// have to know this arrived compressed.
						info.format		= WAV_FORMAT_PCM;
						info.dataofs	= 0;
						info.rate		= iDecodedRate;
						info.width		= (int)sizeof(short);
						info.channels	= iDecodedChannels;
						info.samples	= iDecodedFrames;

						S_LoadSound_Finalize(&info,sfx,pbUnpackBuffer);

#ifdef Q3_BIG_ENDIAN
						// the MP3 decoder returns the samples in the correct endianness, but ResampleSfx byteswaps them,
						// so we have to swap them again...
						sfx->fVolRange	= 0;

						for (int i = 0; i < sfx->iSoundLengthInSamples; i++)
						{
							sfx->pSoundData[i] = LittleShort(sfx->pSoundData[i]);
							// C++11 defines double abs(short) which is not what we want here,
							// because double >> int is not defined. Force interpretation as int
							if (sfx->fVolRange < (abs(static_cast<int>(sfx->pSoundData[i])) >> 8))
							{
								sfx->fVolRange = abs(static_cast<int>(sfx->pSoundData[i])) >> 8;
							}
						}
#endif

						Z_Free(pbUnpackBuffer);
					}
				}
			}
		}
	}
	else
	{
		// loading a WAV, presumably...

//=========

		info = GetWavinfo( sLoadName, data, size );
		if ( info.channels != 1 ) {
			Com_Printf ("%s is a stereo wav file\n", sLoadName);
			FS_FreeFile (data);
			return qfalse;
		}

/*		if ( info.width == 1 ) {
			Com_Printf(S_COLOR_YELLOW "WARNING: %s is a 8 bit wav file\n", sLoadName);
		}

		if ( info.rate != 22050 ) {
			Com_Printf(S_COLOR_YELLOW "WARNING: %s is not a 22kHz wav file\n", sLoadName);
		}
*/
		samples = (short *)Z_Malloc(info.samples * sizeof(short) * 2, TAG_TEMP_WORKSPACE, qfalse);

		sfx->eSoundCompressionMethod = ct_16;
		sfx->iSoundLengthInSamples	 = info.samples;
		sfx->pSoundData = NULL;
		ResampleSfx( sfx, info.rate, info.width, data + info.dataofs );

		// Open AL

		Z_Free(samples);
	}

	FS_FreeFile( data );

	return qtrue;
}


// wrapper function for above so I can guarantee that we don't attempt any audio-dumping during this call because
//	of a z_malloc() fail recovery...
//
qboolean S_LoadSound( sfx_t *sfx )
{
	gbInsideLoadSound = qtrue;	// !!!!!!!!!!!!!

		qboolean bReturn = S_LoadSound_Actual( sfx );

	gbInsideLoadSound = qfalse;	// !!!!!!!!!!!!!

	return bReturn;
}

