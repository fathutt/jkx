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

// tr_init.c -- functions that are not called every frame

#include "tr_local.h"

#include <algorithm>
#include "tr_common.h"
#include "tr_WorldEffects.h"
#include "qcommon/MiniHeap.h"
#include "ghoul2/g2_local.h"
#include "tr_cache.h"

glconfig_t	glConfig;
glconfigExt_t glConfigExt;
glstate_t	glState;
window_t	vidWindow;
glstatic_t	gls;

cvar_t	*r_verbose;
cvar_t	*r_ignore;

cvar_t	*r_detailTextures;

cvar_t	*r_znear;
cvar_t	*r_zproj;

cvar_t	*r_skipBackEnd;

cvar_t	*r_measureOverdraw;

cvar_t	*r_inGameVideo;
cvar_t	*r_fastsky;
cvar_t	*r_drawSun;
cvar_t	*r_dynamiclight;
// rjr - removed for hacking
cvar_t	*r_dlightBacks;

cvar_t	*r_lodbias;
cvar_t	*r_lodscale;
cvar_t	*r_autolodscalevalue;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_drawfog;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_facePlaneCull;
cvar_t	*r_cullRoofFaces; //attempted smart method of culling out upwards facing surfaces on roofs for automap shots -rww
cvar_t	*r_roofCullCeilDist; //ceiling distance cull tolerance -rww
cvar_t	*r_roofCullFloorDist; //floor distance cull tolerance -rww
cvar_t	*r_showcluster;
cvar_t	*r_nocurves;

cvar_t	*r_autoMap; //automap renderside toggle for debugging -rww
cvar_t	*r_autoMapBackAlpha; //alpha of automap bg -rww
cvar_t	*r_autoMapDisable; //don't calc it (since it's slow in debug) -rww

cvar_t	*r_dlightStyle;
cvar_t	*r_surfaceSprites;
cvar_t	*r_surfaceWeather;

cvar_t	*r_windSpeed;
cvar_t	*r_windAngle;
cvar_t	*r_windGust;
cvar_t	*r_windDampFactor;
cvar_t	*r_windPointForce;
cvar_t	*r_windPointX;
cvar_t	*r_windPointY;

cvar_t	*r_allowExtensions;

cvar_t	*r_ext_compressed_textures;
cvar_t	*r_ext_compressed_lightmaps;
cvar_t	*r_ext_preferred_tc_method;
cvar_t	*r_ext_gamma_control;
cvar_t	*r_ext_multitexture;
cvar_t	*r_ext_compiled_vertex_array;
cvar_t	*r_ext_texture_env_add;
cvar_t	*r_ext_texture_filter_anisotropic;
cvar_t	*r_gammaShaders;

cvar_t	*r_environmentMapping;

cvar_t	*r_DynamicGlow;
cvar_t	*r_DynamicGlowAllStages;
cvar_t	*r_DynamicGlowPasses;
cvar_t	*r_DynamicGlowDelta;
cvar_t	*r_DynamicGlowIntensity;
cvar_t	*r_DynamicGlowSoft;
cvar_t	*r_DynamicGlowWidth;
cvar_t	*r_DynamicGlowHeight;
cvar_t	*r_DynamicGlowScale;

cvar_t	*r_smartpicmip;

cvar_t	*r_ignoreGLErrors;
cvar_t	*r_logFile;

cvar_t	*r_primitives;
cvar_t	*r_texturebits;
cvar_t	*r_texturebitslm;

cvar_t	*r_lightmap;
cvar_t	*r_distanceCull;
cvar_t	*r_vertexLight;
cvar_t	*r_uiFullScreen;
cvar_t	*r_shadows;
cvar_t	*r_shadowRange;


cvar_t	*r_flares;
//cvar_t	*r_flareSize;
//cvar_t	*r_flareFade;
//cvar_t	*r_flareCoeff;

cvar_t	*r_nobind;
cvar_t	*r_singleShader;
cvar_t	*r_colorMipLevels;
cvar_t	*r_picmip;
cvar_t	*r_showtris;
cvar_t	*r_showsky;
cvar_t	*r_dissolveType;
cvar_t	*r_dissolveFreeze;
cvar_t	*r_depthPrepass;
cvar_t	*r_shownormals;
cvar_t	*r_finish;
cvar_t	*r_clear;
cvar_t	*r_markcount;
cvar_t	*r_textureMode;
cvar_t	*r_offsetFactor;
cvar_t	*r_offsetUnits;
cvar_t	*r_gamma;
cvar_t	*r_intensity;
cvar_t	*r_lockpvs;
cvar_t	*r_noportals;
cvar_t	*r_portalOnly;

cvar_t	*r_subdivisions;
cvar_t	*r_lodCurveError;



cvar_t	*r_overBrightBits;
cvar_t	*r_mapOverBrightBits;

cvar_t	*r_debugSurface;
cvar_t	*r_simpleMipMaps;

cvar_t	*r_showImages;

cvar_t	*r_ambientScale;
cvar_t	*r_directedScale;
cvar_t	*r_debugLight;
cvar_t	*r_debugView;
cvar_t	*r_skyCubemap;
cvar_t	*r_debugSort;

cvar_t	*r_marksOnTriangleMeshes;

cvar_t	*r_aspectCorrectFonts;
cvar_t	*cl_ratioFix;
cvar_t	*r_patchStitching;

// Vulkan
cvar_t	*r_defaultImage;
cvar_t	*r_device;
//cvar_t	*r_stencilbits;
// Defined by the platform layer, which reads it when it makes the window. Two
// definitions were fine while this was a module; inside the engine there is one,
// and R_Register still fetches it - Cvar_Get on an existing name hands back the
// same cvar.
extern cvar_t	*r_ext_multisample;
cvar_t	*r_ext_supersample;
cvar_t	*r_ext_alpha_to_coverage;
cvar_t	*r_hdr;
cvar_t	*r_ext_max_anisotropy;
cvar_t	*r_mapGreyScale;
cvar_t	*r_greyscale;
cvar_t	*r_dither;
cvar_t	*r_presentBits;
cvar_t	*r_bloom;
cvar_t	*r_bloom_threshold;
cvar_t	*r_bloom_intensity;
cvar_t	*r_bloom_threshold_mode;
cvar_t	*r_bloom_modulate; 
cvar_t	*r_renderWidth;
cvar_t	*r_renderHeight;
cvar_t	*r_renderScale;
extern cvar_t	*r_ignorehwgamma;

#ifdef HDR_DELUXE_LIGHTMAP
cvar_t	*r_deluxeMapping;
cvar_t	*r_deluxeSpecular;
#endif

#ifdef VK_DLIGHT_GPU
cvar_t	*r_dlightMethod;
#endif
#ifdef USE_PMLIGHT
cvar_t	*r_dlightMode;
cvar_t	*r_dlightScale;
cvar_t	*r_dlightIntensity;
#endif
cvar_t	*r_dlightSaturation;
cvar_t	*r_roundImagesDown;
cvar_t	*r_nomip;
#ifdef USE_VK_PBR
cvar_t  *r_normalMapping;
cvar_t	*r_refraction;
cvar_t	*r_refractionScale;
cvar_t	*r_refractionChromatic;
cvar_t  *r_specularMapping;
cvar_t  *r_baseNormalX;
cvar_t  *r_baseNormalY;
cvar_t  *r_baseParallax;
cvar_t  *r_baseSpecular;
#ifdef VK_CUBEMAP
cvar_t	*r_cubeMapping;
#endif
#ifdef VK_COMPUTE_NORMALMAP
cvar_t	*r_genNormalMaps;
#endif
#endif


// the limits apply to the sum of all scenes in a frame --
// the main view, all the 3D icons, etc
#define	DEFAULT_MAX_POLYS		600
#define	DEFAULT_MAX_POLYVERTS	3000
static cvar_t	*r_maxpolys;
static cvar_t	*r_maxpolyverts;
int		max_polys;
int		max_polyverts;

cvar_t	*r_modelpoolmegs; // unused

/*
Ghoul2 Insert Start
*/
#ifdef _DEBUG
cvar_t	*r_noPrecacheGLA;
#endif

cvar_t	*r_noServerGhoul2;
cvar_t	*r_Ghoul2AnimSmooth=0;
cvar_t	*r_Ghoul2UnSqashAfterSmooth=0;
cvar_t	*r_uiScale=0;
cvar_t	*r_Ghoul2UnSqash=0;
cvar_t	*r_Ghoul2NoLerp=0;
cvar_t	*r_Ghoul2NoBlend=0;
cvar_t	*r_Ghoul2BlendMultiplier=0;

cvar_t	*broadsword=0;
cvar_t	*broadsword_kickbones=0;
cvar_t	*broadsword_kickorigin=0;
cvar_t	*broadsword_playflop=0;
cvar_t	*broadsword_dontstopanim=0;
cvar_t	*broadsword_waitforshot=0;
cvar_t	*broadsword_smallbbox=0;
cvar_t	*broadsword_extra1=0;
cvar_t	*broadsword_extra2=0;

cvar_t	*broadsword_effcorr=0;
cvar_t	*broadsword_ragtobase=0;
cvar_t	*broadsword_dircap=0;

/*
Ghoul2 Insert End
*/

// The engine owns the language cvar; the string tables are its, not ours.
extern cvar_t *se_language;

cvar_t *r_aviMotionJpegQuality;
cvar_t *r_screenshotJpegQuality;

// Vulkan
#include "vk_local.h"
Vk_Instance vk;
Vk_World	vk_world;

#if 0
#if !defined(__APPLE__)
PFNGLSTENCILOPSEPARATEPROC qglStencilOpSeparate;
#endif

PFNGLACTIVETEXTUREARBPROC qglActiveTextureARB;
PFNGLCLIENTACTIVETEXTUREARBPROC qglClientActiveTextureARB;
PFNGLMULTITEXCOORD2FARBPROC qglMultiTexCoord2fARB;
#if !defined(__APPLE__)
PFNGLTEXIMAGE3DPROC qglTexImage3D;
PFNGLTEXSUBIMAGE3DPROC qglTexSubImage3D;
#endif

PFNGLCOMBINERPARAMETERFVNVPROC qglCombinerParameterfvNV;
PFNGLCOMBINERPARAMETERIVNVPROC qglCombinerParameterivNV;
PFNGLCOMBINERPARAMETERFNVPROC qglCombinerParameterfNV;
PFNGLCOMBINERPARAMETERINVPROC qglCombinerParameteriNV;
PFNGLCOMBINERINPUTNVPROC qglCombinerInputNV;
PFNGLCOMBINEROUTPUTNVPROC qglCombinerOutputNV;

PFNGLFINALCOMBINERINPUTNVPROC qglFinalCombinerInputNV;
PFNGLGETCOMBINERINPUTPARAMETERFVNVPROC qglGetCombinerInputParameterfvNV;
PFNGLGETCOMBINERINPUTPARAMETERIVNVPROC qglGetCombinerInputParameterivNV;
PFNGLGETCOMBINEROUTPUTPARAMETERFVNVPROC qglGetCombinerOutputParameterfvNV;
PFNGLGETCOMBINEROUTPUTPARAMETERIVNVPROC qglGetCombinerOutputParameterivNV;
PFNGLGETFINALCOMBINERINPUTPARAMETERFVNVPROC qglGetFinalCombinerInputParameterfvNV;
PFNGLGETFINALCOMBINERINPUTPARAMETERIVNVPROC qglGetFinalCombinerInputParameterivNV;

PFNGLPROGRAMSTRINGARBPROC qglProgramStringARB;
PFNGLBINDPROGRAMARBPROC qglBindProgramARB;
PFNGLDELETEPROGRAMSARBPROC qglDeleteProgramsARB;
PFNGLGENPROGRAMSARBPROC qglGenProgramsARB;
PFNGLPROGRAMENVPARAMETER4DARBPROC qglProgramEnvParameter4dARB;
PFNGLPROGRAMENVPARAMETER4DVARBPROC qglProgramEnvParameter4dvARB;
PFNGLPROGRAMENVPARAMETER4FARBPROC qglProgramEnvParameter4fARB;
PFNGLPROGRAMENVPARAMETER4FVARBPROC qglProgramEnvParameter4fvARB;
PFNGLPROGRAMLOCALPARAMETER4DARBPROC qglProgramLocalParameter4dARB;
PFNGLPROGRAMLOCALPARAMETER4DVARBPROC qglProgramLocalParameter4dvARB;
PFNGLPROGRAMLOCALPARAMETER4FARBPROC qglProgramLocalParameter4fARB;
PFNGLPROGRAMLOCALPARAMETER4FVARBPROC qglProgramLocalParameter4fvARB;
PFNGLGETPROGRAMENVPARAMETERDVARBPROC qglGetProgramEnvParameterdvARB;
PFNGLGETPROGRAMENVPARAMETERFVARBPROC qglGetProgramEnvParameterfvARB;
PFNGLGETPROGRAMLOCALPARAMETERDVARBPROC qglGetProgramLocalParameterdvARB;
PFNGLGETPROGRAMLOCALPARAMETERFVARBPROC qglGetProgramLocalParameterfvARB;
PFNGLGETPROGRAMIVARBPROC qglGetProgramivARB;
PFNGLGETPROGRAMSTRINGARBPROC qglGetProgramStringARB;
PFNGLISPROGRAMARBPROC qglIsProgramARB;

PFNGLLOCKARRAYSEXTPROC qglLockArraysEXT;
PFNGLUNLOCKARRAYSEXTPROC qglUnlockArraysEXT;
#endif

bool g_bTextureRectangleHack = false;

void RE_SetLightStyle( int style, int color );
void RE_GetBModelVerts( int bmodelIndex, vec3_t *verts, vec3_t normal );

void R_Set2DRatio( void ) {
	if (cl_ratioFix->integer)
		tr.widthRatioCoef = ((float)(SCREEN_WIDTH * gls.windowHeight) / (float)(SCREEN_HEIGHT * gls.windowWidth));
	else
		tr.widthRatioCoef = 1.0f;

	if (tr.widthRatioCoef > 1)
		tr.widthRatioCoef = 1.0f;
}

/*
==============================================================================

						SCREEN SHOTS

==============================================================================
*/

/*
==================
RB_ReadPixels

Reads an image but takes care of alignment issues for reading RGB images.

Reads a minimum offset for where the RGB data starts in the image from
integer stored at pointer offset. When the function has returned the actual
offset was written back to address offset. This address will always have an
alignment of packAlign to ensure efficient copying.

Stores the length of padding after a line of pixels to address padlen

Return value must be freed with R_Hunk_FreeTempMemory()
==================
*/

byte *RB_ReadPixels( int x, int y, int width, int height, size_t *offset, int *padlen, int lineAlign )
{
	byte *buffer, *bufstart;
	int bufAlign, linelen;
	int packAlign = 1;

	linelen = width * 3;

	bufAlign = MAX(packAlign, 16); // for SIMD

	// Allocate a few more bytes so that we can choose an alignment we like
	//buffer = R_Hunk_AllocateTempMemory(padwidth * height + *offset + bufAlign - 1);
	buffer = (byte*)R_Hunk_AllocateTempMemory(width * height * 4 + *offset + bufAlign - 1);
	bufstart = (byte*)PADP((intptr_t)buffer + *offset, bufAlign);

	vk_read_pixels(bufstart, width, height);

	*offset = bufstart - buffer;
	*padlen = PAD(linelen, packAlign) - linelen;

	return buffer;
}

/*
==================
RE_GetScreenShot

A small RGBA picture of the frame on screen, for the save game to embed.

The 4x3 box filter is not a general resample: it takes exactly twelve source
pixels per destination pixel, which is what makes the fixed thumbnail size the
save game wants out of any video mode. The rows come back bottom-up, as they do
from the screenshot path, and are flipped on the way out.
==================
*/
void RE_GetScreenShot( byte *buffer, int w, int h )
{
	size_t	offset = 0, memcount;
	int		padlen;

	if ( !buffer || w <= 0 || h <= 0 ) {
		return;
	}

	byte *source = RB_ReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, &offset, &padlen, 0 );
	memcount = (size_t)( glConfig.vidWidth * 3 + padlen ) * glConfig.vidHeight;

	if ( glConfig.deviceSupportsGamma && !glConfigExt.doGammaCorrectionWithShaders ) {
		R_GammaCorrect( source + offset, (int)memcount );
	}

	const float xScale = glConfig.vidWidth / ( 4.0f * w );
	const float yScale = glConfig.vidHeight / ( 3.0f * h );

	for ( int y = 0; y < h; y++ ) {
		for ( int x = 0; x < w; x++ ) {
			int r = 0, g = 0, b = 0;

			for ( int yy = 0; yy < 3; yy++ ) {
				for ( int xx = 0; xx < 4; xx++ ) {
					const byte *src = source + offset + 3 *
						( glConfig.vidWidth * (int)( ( y * 3 + yy ) * yScale ) + (int)( ( x * 4 + xx ) * xScale ) );
					r += src[0];
					g += src[1];
					b += src[2];
				}
			}

			byte *dst = buffer + 4 * ( ( h - y - 1 ) * w + x );
			dst[0] = (byte)( r / 12 );
			dst[1] = (byte)( g / 12 );
			dst[2] = (byte)( b / 12 );
			dst[3] = 255;
		}
	}

	R_Hunk_FreeTempMemory( source );
}

/*
==================
R_TakeScreenshot
==================
*/
void R_TakeScreenshot( int x, int y, int width, int height, char *fileName ) {
	byte *allbuf, *buffer;
	byte *srcptr, *destptr;
	byte *endline, *endmem;
	byte temp;

	int linelen, padlen;
	size_t offset = 18, memcount;

	allbuf = RB_ReadPixels(x, y, width, height, &offset, &padlen, 0);
	buffer = allbuf + offset - 18;

	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width & 255;
	buffer[13] = width >> 8;
	buffer[14] = height & 255;
	buffer[15] = height >> 8;
	buffer[16] = 24;	// pixel size

	// swap rgb to bgr and remove padding from line endings
	linelen = width * 3;

	srcptr = destptr = allbuf + offset;
	endmem = srcptr + (linelen + padlen) * height;

	while(srcptr < endmem)
	{
		endline = srcptr + linelen;

		while(srcptr < endline)
		{
			temp = srcptr[0];
			*destptr++ = srcptr[2];
			*destptr++ = srcptr[1];
			*destptr++ = temp;

			srcptr += 3;
		}

		// Skip the pad
		srcptr += padlen;
	}

	memcount = linelen * height;

	// gamma correct
	if(glConfig.deviceSupportsGamma && !glConfigExt.doGammaCorrectionWithShaders)
		R_GammaCorrect(allbuf + offset, memcount);

	FS_WriteFile(fileName, buffer, memcount + 18);

	R_Hunk_FreeTempMemory(allbuf);
}

/*
==================
R_TakeScreenshotPNG
==================
*/
void R_TakeScreenshotPNG( int x, int y, int width, int height, char *fileName ) {
	byte *buffer=NULL;
	size_t offset=0;
	int padlen=0;

	buffer = RB_ReadPixels( x, y, width, height, &offset, &padlen, 0);
	RE_SavePNG( fileName, buffer, width, height, 3 );
	R_Hunk_FreeTempMemory( buffer );
}

/*
==================
R_TakeScreenshotJPEG
==================
*/
void R_TakeScreenshotJPEG( int x, int y, int width, int height, char *fileName ) {
	byte *buffer;
	size_t offset = 0, memcount;
	int padlen;

	buffer = RB_ReadPixels(x, y, width, height, &offset, &padlen, 0);
	memcount = (width * 3 + padlen) * height;

	// gamma correct
	if(glConfig.deviceSupportsGamma && !glConfigExt.doGammaCorrectionWithShaders)
		R_GammaCorrect(buffer + offset, memcount);

	RE_SaveJPG(fileName, r_screenshotJpegQuality->integer, width, height, buffer + offset, padlen);
	R_Hunk_FreeTempMemory(buffer);
}

/*
==================
R_ScreenshotFilename
==================
*/
void R_ScreenshotFilename( char *buf, int bufSize, const char *ext ) {
	time_t rawtime;
	char timeStr[32] = {0}; // should really only reach ~19 chars

	time( &rawtime );
	strftime( timeStr, sizeof( timeStr ), "%Y-%m-%d_%H-%M-%S", localtime( &rawtime ) ); // or gmtime

	Com_sprintf( buf, bufSize, "screenshots/shot%s.%s", timeStr, ext );
}

/*
====================
R_LevelShot

levelshots are specialized 256*256 thumbnails for
the menu system, sampled down from full screen distorted images
====================
*/
#define LEVELSHOTSIZE 256
static void R_LevelShot( void ) {
	char		checkname[MAX_OSPATH];
	byte		*buffer;
	byte		*source, *allsource;
	byte		*src, *dst;
	size_t		offset = 0;
	int			padlen;
	int			x, y;
	int			r, g, b;
	float		xScale, yScale;
	int			xx, yy;

	Com_sprintf( checkname, sizeof(checkname), "levelshots/%s.tga", tr.world->baseName );

	allsource = RB_ReadPixels(0, 0, gls.captureWidth, gls.captureHeight, &offset, &padlen, 0);
	source = allsource + offset;

	buffer = (byte *)R_Hunk_AllocateTempMemory(LEVELSHOTSIZE * LEVELSHOTSIZE*3 + 18);
	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = LEVELSHOTSIZE & 255;
	buffer[13] = LEVELSHOTSIZE >> 8;
	buffer[14] = LEVELSHOTSIZE & 255;
	buffer[15] = LEVELSHOTSIZE >> 8;
	buffer[16] = 24;	// pixel size

	// resample from source
	xScale = glConfig.vidWidth / (4.0*LEVELSHOTSIZE);
	yScale = glConfig.vidHeight / (3.0*LEVELSHOTSIZE);
	for ( y = 0 ; y < LEVELSHOTSIZE ; y++ ) {
		for ( x = 0 ; x < LEVELSHOTSIZE ; x++ ) {
			r = g = b = 0;
			for ( yy = 0 ; yy < 3 ; yy++ ) {
				for ( xx = 0 ; xx < 4 ; xx++ ) {
					src = source + 3 * ( glConfig.vidWidth * (int)( (y*3+yy)*yScale ) + (int)( (x*4+xx)*xScale ) );
					r += src[0];
					g += src[1];
					b += src[2];
				}
			}
			dst = buffer + 18 + 3 * ( y * LEVELSHOTSIZE + x );
			dst[0] = b / 12;
			dst[1] = g / 12;
			dst[2] = r / 12;
		}
	}

	// gamma correct
	if ( ( tr.overbrightBits > 0 ) && glConfig.deviceSupportsGamma && !glConfigExt.doGammaCorrectionWithShaders ) {
		R_GammaCorrect( buffer + 18, LEVELSHOTSIZE * LEVELSHOTSIZE * 3 );
	}

	FS_WriteFile( checkname, buffer, LEVELSHOTSIZE * LEVELSHOTSIZE*3 + 18 );

	R_Hunk_FreeTempMemory( buffer );
	R_Hunk_FreeTempMemory( allsource );

	vk_debug("[skipnotify]Wrote %s\n", checkname );
}

void R_ScreenShot_f ( void ) {
	char checkname[MAX_OSPATH] = {0};
	qboolean silent = qfalse;
	int			typeMask;
	const char *ext;

	if (WIN_VK_IsMinimized() && !R_CanMinimize()) {
		CL_RefPrintf(PRINT_WARNING, "WARNING: unable to take screenshot when minimized because FBO is not available/enabled.\n");
		return;
	}

	if ( !strcmp( Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if (Q_stricmp(Cmd_Argv(0), "screenshot_tga") == 0) {
		typeMask = SCREENSHOT_TGA;
		ext = "tga";
	}
	else if (Q_stricmp(Cmd_Argv(0), "screenshot_png") == 0) {
		typeMask = SCREENSHOT_PNG;
		ext = "png";
	}
	else {
		typeMask = SCREENSHOT_JPG;
		ext = "jpg";
	}

	// check if already scheduled
	if (backEnd.screenshotMask & typeMask)
		return;

	if ( !strcmp( Cmd_Argv(1), "silent" ) )
		silent = qtrue;

	if ( Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, sizeof( checkname ), "screenshots/%s.%s", Cmd_Argv( 1 ), ext );
	}
	else {
		// timestamp the filename
		R_ScreenshotFilename( checkname, sizeof( checkname ), ext );

		if ( S_FileExists( checkname ) ) {
			vk_debug("ScreenShot: Couldn't create a file\n" );
			return;
 		}
	}

	// we will make the screenshot right at the end of RE_EndFrame()
	backEnd.screenshotMask |= typeMask;
	if (typeMask == SCREENSHOT_JPG) {
		backEnd.screenShotJPGsilent = silent;
		Q_strncpyz(backEnd.screenshotJPG, checkname, sizeof(backEnd.screenshotJPG));
	}
	else if (typeMask == SCREENSHOT_PNG) {
		backEnd.screenShotPNGsilent = silent;
		Q_strncpyz(backEnd.screenshotPNG, checkname, sizeof(backEnd.screenshotPNG));
	}
	else {
		backEnd.screenShotTGAsilent = silent;
		Q_strncpyz(backEnd.screenshotTGA, checkname, sizeof(backEnd.screenshotTGA));
	}
}

/*
==================
RB_TakeVideoFrameCmd
==================
*/
// Video capture. Multiplayer records the framebuffer to an AVI through
// CL_WriteAVIVideoFrame; single-player has no writer to hand it to and never
// had the feature, so this is not adapted - it is left out until the engine
// side exists. See the project backlog.

void R_RemapSkyShader_f ( void ) {
	int num;

	if (Cmd_Argc() != 2 || !strlen(Cmd_Argv(1))) {
		vk_debug("Usage: /remapSky <new>\n");
		return;
	}

	for (num = 0; num < tr.numShaders; num++) {
		if (tr.shaders[num]->sky)
		{
			R_RemapShader(tr.shaders[num]->name, Cmd_Argv(1), NULL);
		}
	}
}

void R_ClearRemaps_f( void ) {
	int num;

	for (num = 0; num < tr.numShaders; num++) {
		tr.shaders[num]->remappedShader = NULL;
	}
}

// THE LIGHTING, ONE TERM AT A TIME
//
// The physically based shader computes a dozen intermediate quantities and
// shows none of them; what reaches the screen is their sum, and a sum is the
// one thing you cannot debug by looking at it. Every question of the form "is
// this too bright because the ambient is wrong, or because the specular is, or
// because the normal map is upside down" has the same answer today, which is a
// guess.
//
// It is worth a push constant on every draw because of what a wrong guess costs
// here: this project is built in one place and run in another, and a round trip
// is hours. Six screenshots from one run beat six builds.
//
// The list is upstream's, from JKSunny's inspector branch, minus the ImGui it
// was wired to. See the project's upstream survey.
static const char *debugViewNames[] = {
	"off",
	"diffuse", "specular", "roughness", "ao",
	"normals", "normalmap", "lightdir", "viewdir", "tangents",
	"lightcolor", "ambient", "reflectance", "attenuation",
	"halfangle", "fd", "fs",
	"ne", "nl", "lh", "nh", "vh",
	"ibl"
};

static void R_DebugView_f( void ) {
	size_t i;

	if ( Cmd_Argc() < 2 ) {
		CL_RefPrintf( PRINT_ALL, "usage: debugview <name|number>, currently %s\n",
			( r_debugView->integer > 0 && r_debugView->integer < (int)ARRAY_LEN( debugViewNames ) )
				? debugViewNames[ r_debugView->integer ] : "off" );

		for ( i = 0; i < ARRAY_LEN( debugViewNames ); i++ )
			CL_RefPrintf( PRINT_ALL, "  %2u  %s\n", (unsigned)i, debugViewNames[i] );

		return;
	}

	// By name as well as by number, and that is not decoration either: a number
	// has to be looked up in a table that is not in front of whoever is running
	// the build, and "debugview roughness" survives being relayed through a chat
	// message intact.
	for ( i = 0; i < ARRAY_LEN( debugViewNames ); i++ ) {
		if ( !Q_stricmp( Cmd_Argv( 1 ), debugViewNames[i] ) ) {
			Cvar_Set( "r_debugView", va( "%u", (unsigned)i ) );
			CL_RefPrintf( PRINT_ALL, "debugview: %s\n", debugViewNames[i] );
			return;
		}
	}

	i = (size_t)atoi( Cmd_Argv( 1 ) );
	if ( i >= ARRAY_LEN( debugViewNames ) ) {
		CL_RefPrintf( PRINT_ALL, "debugview: no such view \"%s\"\n", Cmd_Argv( 1 ) );
		return;
	}

	Cvar_Set( "r_debugView", va( "%u", (unsigned)i ) );
	CL_RefPrintf( PRINT_ALL, "debugview: %s\n", debugViewNames[i] );
}

/*
==================
R_Dissolve_f

Start a screen wipe now, without waiting for a level to finish loading.

The wipe normally begins at the end of a level load, so seeing one means loading
a level and being quick. With r_dissolveFreeze this holds any wipe still, in any
scene, for as long as it takes to look at it.

What it cannot do is make a wipe visible where there is nothing to wipe between.
The old screen it captures is the screen as it is now, so a wipe started from the
console with nothing else changing blends a picture against itself and shows
nothing at all - correctly. Change the view, or the level, between the capture
and the look.
==================
*/
static void R_Dissolve_f( void )
{
	if ( !RE_InitDissolve( qfalse ) ) {
		CL_RefPrintf( PRINT_ALL, "dissolve: could not start\n" );
	}
}

typedef struct consoleCommand_s {
	const char	*cmd;
	xcommand_t	func;
} consoleCommand_t;

static consoleCommand_t	commands[] = {
	{ "imagelist",			R_ImageList_f },
	{ "shaderlist",			R_ShaderList_f },
	{ "skinlist",			R_SkinList_f },
	{ "dissolve",			R_Dissolve_f },
	{ "fontlist",			R_FontList_f },
	{ "screenshot",			R_ScreenShot_f },
	{ "screenshot_png",		R_ScreenShot_f },
	{ "screenshot_tga",		R_ScreenShot_f },
	{ "gfxinfo",			GfxInfo_f },
	{ "r_we",				R_WorldEffect_f },
	//{ "imagecacheinfo",		RE_RegisterImages_Info_f },
	{ "modellist",			R_Modellist_f },
	//{ "modelcacheinfo",		RE_RegisterModels_Info_f },
	{ "r_cleardecals",		RE_ClearDecals },
	{ "remapSky",			R_RemapSkyShader_f },
	{ "clearRemaps",		R_ClearRemaps_f },
	{ "vkinfo",				vk_info_f },
	{ "debugview",			R_DebugView_f }
};

static const size_t numCommands = ARRAY_LEN( commands );

#ifdef _DEBUG
#define MIN_PRIMITIVES -1
#else
#define MIN_PRIMITIVES 0
#endif
#define MAX_PRIMITIVES 3

/*
===============
R_Register
===============
*/
void R_Register( void )
{
	//FIXME: lol badness
	se_language = Cvar_Get("se_language", "english", CVAR_ARCHIVE | CVAR_NORESTART, "");
	//
	// latched and archived variables
	//
	r_allowExtensions					= Cvar_Get( "r_allowExtensions",					"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_compressed_textures			= Cvar_Get( "r_ext_compress_textures",			"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_compressed_lightmaps			= Cvar_Get( "r_ext_compress_lightmaps",			"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_preferred_tc_method			= Cvar_Get( "r_ext_preferred_tc_method",			"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_gamma_control					= Cvar_Get( "r_ext_gamma_control",				"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_multitexture					= Cvar_Get( "r_ext_multitexture",				"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_compiled_vertex_array			= Cvar_Get( "r_ext_compiled_vertex_array",		"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_texture_env_add				= Cvar_Get( "r_ext_texture_env_add",				"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_texture_filter_anisotropic	= Cvar_Get( "r_ext_texture_filter_anisotropic",	"16",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_gammaShaders						= Cvar_Get( "r_gammaShaders",					"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "Set gamma using pixel shaders inside the game window only." );
	r_environmentMapping				= Cvar_Get( "r_environmentMapping",				"1",						CVAR_ARCHIVE_ND, "" );
	r_DynamicGlow						= Cvar_Get( "r_DynamicGlow",						"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "Enable dynamic glow effect" );
	r_DynamicGlowAllStages				= Cvar_Get( "r_DynamicGlowAllStages",			"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "In Vanilla certain glow stages are skipped, render those anyway" );
	r_DynamicGlowPasses					= Cvar_Get( "r_DynamicGlowPasses",				"5",						CVAR_ARCHIVE_ND, "" );
	r_DynamicGlowDelta					= Cvar_Get( "r_DynamicGlowDelta",				"0.8f",						CVAR_ARCHIVE_ND, "" );
	r_DynamicGlowIntensity				= Cvar_Get( "r_DynamicGlowIntensity",			"1.13f",					CVAR_ARCHIVE_ND, "" );
	r_DynamicGlowSoft					= Cvar_Get( "r_DynamicGlowSoft",					"1",						CVAR_ARCHIVE_ND, "" );
	r_DynamicGlowWidth					= Cvar_Get( "r_DynamicGlowWidth",				"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_DynamicGlowHeight					= Cvar_Get( "r_DynamicGlowHeight",				"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_DynamicGlowScale					= Cvar_Get( "r_DynamicGlowScale",				"0.25",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_picmip							= Cvar_Get( "r_picmip",							"0",						CVAR_ARCHIVE|CVAR_LATCH, "" );
	Cvar_CheckRange( r_picmip, 0, 16, qtrue );
	r_smartpicmip						= Cvar_Get( "r_smartpicmip",						"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "Applies r_picmip setting to map textures only." );
	r_colorMipLevels					= Cvar_Get( "r_colorMipLevels",					"0",						CVAR_LATCH, "" );
	r_detailTextures					= Cvar_Get( "r_detailtextures",					"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_texturebits						= Cvar_Get( "r_texturebits",						"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_texturebitslm						= Cvar_Get( "r_texturebitslm",					"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_overBrightBits					= Cvar_Get( "r_overBrightBits",					"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_mapOverBrightBits					= Cvar_Get( "r_mapOverBrightBits",				"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_simpleMipMaps						= Cvar_Get( "r_simpleMipMaps",					"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_vertexLight						= Cvar_Get( "r_vertexLight",						"0",						CVAR_ARCHIVE|CVAR_LATCH, "" );
	r_uiFullScreen						= Cvar_Get( "r_uifullscreen",					"0",						CVAR_NONE, "" );
	r_subdivisions						= Cvar_Get( "r_subdivisions",					"4",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	Cvar_CheckRange( r_subdivisions, 0, 80, qfalse );

	r_fullbright						= Cvar_Get( "r_fullbright",						"0",						CVAR_ARCHIVE_ND, "" );
	r_intensity							= Cvar_Get( "r_intensity",						"1",						CVAR_LATCH, "" );
	r_singleShader						= Cvar_Get( "r_singleShader",					"0",						CVAR_CHEAT|CVAR_LATCH, "" );
	r_lodCurveError						= Cvar_Get( "r_lodCurveError",					"250",						CVAR_ARCHIVE_ND, "" );
	r_lodbias							= Cvar_Get( "r_lodbias",							"0",						CVAR_ARCHIVE_ND, "" );
	r_autolodscalevalue					= Cvar_Get( "r_autolodscalevalue",				"0",						CVAR_ROM, "" );

	r_flares							= Cvar_Get( "r_flares",							"1",						CVAR_ARCHIVE_ND, "" );
	//r_flareSize							= Cvar_Get( "r_flareSize",						"40",						CVAR_ARCHIVE_ND, "" );
	//r_flareFade							= Cvar_Get( "r_flareFade",						"10",						CVAR_ARCHIVE_ND, "" );
	//r_flareCoeff						= Cvar_Get( "r_flareCoeff",						"150",						CVAR_ARCHIVE_ND, "" );
	//Cvar_CheckRange(r_flareCoeff, 0.1f, 250, qfalse);

	r_znear								= Cvar_Get( "r_znear",							"4",						CVAR_ARCHIVE_ND, "" );
	Cvar_CheckRange( r_znear, 0.001f, 10, qfalse );
	r_zproj								= Cvar_Get("r_zproj",							"64",						CVAR_ARCHIVE_ND, "" );
	r_ignoreGLErrors					= Cvar_Get( "r_ignoreGLErrors",					"1",						CVAR_ARCHIVE_ND, "" );
	r_fastsky							= Cvar_Get( "r_fastsky",							"0",						CVAR_ARCHIVE_ND, "" );
	r_inGameVideo						= Cvar_Get( "r_inGameVideo",						"1",						CVAR_ARCHIVE_ND, "" );
	r_drawSun							= Cvar_Get( "r_drawSun",							"0",						CVAR_ARCHIVE_ND, "" );
	r_dynamiclight						= Cvar_Get( "r_dynamiclight",					"1",						CVAR_ARCHIVE, "" );
	// rjr - removed for hacking
	r_dlightBacks						= Cvar_Get( "r_dlightBacks",						"1",						CVAR_ARCHIVE_ND, "dlight non-facing surfaces for continuity" );
	r_finish							= Cvar_Get( "r_finish",							"0",						CVAR_ARCHIVE_ND, "" );
	r_textureMode						= Cvar_Get( "r_textureMode",						"GL_LINEAR_MIPMAP_LINEAR",	CVAR_ARCHIVE, "" );
	r_markcount							= Cvar_Get( "r_markcount",						"100",						CVAR_ARCHIVE_ND, "" );
	r_gamma								= Cvar_Get( "r_gamma",							"1",						CVAR_ARCHIVE_ND, "" );
	r_facePlaneCull						= Cvar_Get( "r_facePlaneCull",					"1",						CVAR_ARCHIVE_ND, "" );
	r_cullRoofFaces						= Cvar_Get( "r_cullRoofFaces",					"0",						CVAR_CHEAT, "" ); //attempted smart method of culling out upwards facing surfaces on roofs for automap shots -rww
	r_roofCullCeilDist					= Cvar_Get( "r_roofCullCeilDist",				"256",						CVAR_CHEAT, "" ); //attempted smart method of culling out upwards facing surfaces on roofs for automap shots -rww
	r_roofCullFloorDist					= Cvar_Get( "r_roofCeilFloorDist",				"128",						CVAR_CHEAT, "" ); //attempted smart method of culling out upwards facing surfaces on roofs for automap shots -rww
	r_primitives						= Cvar_Get( "r_primitives",						"0",						CVAR_ARCHIVE_ND, "" );
	Cvar_CheckRange( r_primitives, MIN_PRIMITIVES, MAX_PRIMITIVES, qtrue );
	r_ambientScale						= Cvar_Get( "r_ambientScale",					"0.6",						CVAR_NONE, "" );
	r_directedScale						= Cvar_Get( "r_directedScale",					"1",						CVAR_NONE, "" );
	r_autoMap							= Cvar_Get( "r_autoMap",							"0",						CVAR_ARCHIVE_ND, "" ); //automap renderside toggle for debugging -rww
	r_autoMapBackAlpha					= Cvar_Get( "r_autoMapBackAlpha",				"0",						CVAR_NONE, "" ); //alpha of automap bg -rww
	r_autoMapDisable					= Cvar_Get( "r_autoMapDisable",					"1",						CVAR_NONE, "" );
	r_showImages						= Cvar_Get( "r_showImages",						"0",						CVAR_CHEAT, "" );
	r_debugLight						= Cvar_Get( "r_debuglight",						"0",						CVAR_TEMP, "" );
	r_debugView							= Cvar_Get( "r_debugView",						"0",						CVAR_TEMP, "show one term of the lighting instead of the frame; \"debugview\" lists them" );
	r_skyCubemap						= Cvar_Get( "r_skyCubemap",					"1",						CVAR_ARCHIVE_ND | CVAR_LATCH, "gather the six sky faces into one cubemap; 0 keeps the six-quad box" );
	r_debugSort							= Cvar_Get( "r_debugSort",						"0",						CVAR_CHEAT, "" );
	r_dlightStyle						= Cvar_Get( "r_dlightStyle",						"1",						CVAR_TEMP, "" );
	r_surfaceSprites					= Cvar_Get( "r_surfaceSprites",					"1",						CVAR_ARCHIVE_ND | CVAR_LATCH, "" );
	r_surfaceWeather					= Cvar_Get( "r_surfaceWeather",					"0",						CVAR_TEMP, "" );
	r_windSpeed							= Cvar_Get( "r_windSpeed",						"0",						CVAR_NONE, "" );
	r_windAngle							= Cvar_Get( "r_windAngle",						"0",						CVAR_NONE, "" );
	r_windGust							= Cvar_Get( "r_windGust",						"0",						CVAR_NONE, "" );
	r_windDampFactor					= Cvar_Get( "r_windDampFactor",					"0.1",						CVAR_NONE, "" );
	r_windPointForce					= Cvar_Get( "r_windPointForce",					"0",						CVAR_NONE, "" );
	r_windPointX						= Cvar_Get( "r_windPointX",						"0",						CVAR_NONE, "" );
	r_windPointY						= Cvar_Get( "r_windPointY",						"0",						CVAR_NONE, "" );
	r_nocurves							= Cvar_Get( "r_nocurves",						"0",						CVAR_CHEAT, "" );
	r_drawworld							= Cvar_Get( "r_drawworld",						"1",						CVAR_CHEAT, "" );
	r_drawfog							= Cvar_Get("r_drawfog",							"2",						CVAR_ARCHIVE_ND, "Fog mode\n"
		" 0 - disabled\n"
		" 1 - legacy fog\n"
		" 2 - fog collapse\n");
	r_lightmap							= Cvar_Get( "r_lightmap",						"0",						CVAR_ARCHIVE_ND, "" );
	r_distanceCull						= Cvar_Get( "r_distanceCull",					"0",						CVAR_ARCHIVE_ND, "" );
	r_portalOnly						= Cvar_Get( "r_portalOnly",						"0",						CVAR_CHEAT, "" );
	r_skipBackEnd						= Cvar_Get( "r_skipBackEnd",						"0",						CVAR_CHEAT, "" );
	r_measureOverdraw					= Cvar_Get( "r_measureOverdraw",					"0",						CVAR_NONE, "" );
	r_lodscale							= Cvar_Get( "r_lodscale",						"5",						CVAR_ARCHIVE_ND, "" );
	r_norefresh							= Cvar_Get( "r_norefresh",						"0",						CVAR_CHEAT, "" );
	r_drawentities						= Cvar_Get( "r_drawentities",					"1",						CVAR_CHEAT, "" );
	r_ignore							= Cvar_Get( "r_ignore",							"1",						CVAR_CHEAT, "" );
	r_nocull							= Cvar_Get( "r_nocull",							"0",						CVAR_CHEAT, "" );
	r_novis								= Cvar_Get( "r_novis",							"0",						CVAR_CHEAT, "" );
	r_showcluster						= Cvar_Get( "r_showcluster",						"0",						CVAR_CHEAT, "" );
	r_speeds							= Cvar_Get( "r_speeds",							"0",						CVAR_CHEAT, "" );
	r_verbose							= Cvar_Get( "r_verbose",							"0",						CVAR_CHEAT, "" );
	r_logFile							= Cvar_Get( "r_logFile",							"0",						CVAR_CHEAT, "" );
	r_debugSurface						= Cvar_Get( "r_debugSurface",					"0",						CVAR_CHEAT, "" );
	r_nobind							= Cvar_Get( "r_nobind",							"0",						CVAR_CHEAT, "" );
	r_showtris							= Cvar_Get( "r_showtris",						"0",						CVAR_NONE, "" );
	r_showsky							= Cvar_Get( "r_showsky",							"0",						CVAR_CHEAT, "" );
	// Which screen wipe to use, or -1 for the random pick the game makes. It
	// exists because a wipe chosen at random is a wipe nobody can take a picture
	// of twice, and the picture is the only way to see whether the boundary is
	// soft. See Dissolve_e in tr_dissolve.cpp for the order.
	//
	// Neither this nor the freeze below is cheat protected. They were, and the
	// bench could not set them: a cheat-protected cvar is forced back to its
	// default whenever cheats are off, silently, so the wipe ran normally and
	// the check photographed whatever it found. Neither of them can do anything
	// a player would call cheating - one picks which wipe, the other holds it
	// still.
	r_dissolveType						= Cvar_Get( "r_dissolveType",						"-1",						CVAR_ARCHIVE_ND, "" );
	// Hold the wipe at a percentage instead of running it, so that a picture of
	// it is a picture of a known thing. -1 runs it normally.
	r_dissolveFreeze					= Cvar_Get( "r_dissolveFreeze",						"-1",						0, "" );
	r_depthPrepass						= Cvar_Get( "r_depthPrepass",					"0",						CVAR_ARCHIVE_ND, "fill the depth buffer with opaque geometry before shading it" );
	r_shownormals						= Cvar_Get( "r_shownormals",						"0",						CVAR_CHEAT, "" );
	r_clear								= Cvar_Get( "r_clear",							"0",						CVAR_CHEAT, "" );
	r_offsetFactor						= Cvar_Get( "r_offsetfactor",					"-1",						CVAR_CHEAT, "" );
	r_offsetUnits						= Cvar_Get( "r_offsetunits",						"-2",						CVAR_CHEAT, "" );
	r_lockpvs							= Cvar_Get( "r_lockpvs",							"0",						CVAR_CHEAT, "" );
	r_noportals							= Cvar_Get( "r_noportals",						"0",						CVAR_NONE, "" );
	r_shadows							= Cvar_Get( "cg_shadows",						"1",						CVAR_NONE, "" );
	r_shadowRange						= Cvar_Get( "r_shadowRange",						"1000",						CVAR_NONE, "" );
	r_marksOnTriangleMeshes				= Cvar_Get( "r_marksOnTriangleMeshes",			"0",						CVAR_ARCHIVE_ND, "" );
	r_aspectCorrectFonts				= Cvar_Get( "r_aspectCorrectFonts",				"0",						CVAR_ARCHIVE, "" );
	cl_ratioFix							= Cvar_Get( "cl_ratioFix",						"1",						CVAR_ARCHIVE, "" );
	r_patchStitching					= Cvar_Get( "r_patchStitching",					"1",						CVAR_ARCHIVE, "Enable stitching of neighbouring patch surfaces" );
	r_maxpolys							= Cvar_Get( "r_maxpolys",						XSTRING( DEFAULT_MAX_POLYS ),		CVAR_NONE, "" );
	r_maxpolyverts						= Cvar_Get( "r_maxpolyverts",					XSTRING( DEFAULT_MAX_POLYVERTS ),	CVAR_NONE, "" );

	// Vulkan
	r_defaultImage						= Cvar_Get("r_defaultImage",						"",							CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	r_device							= Cvar_Get("r_device",							"-1",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Select physical device to render:\n" \
		" 0+ - use explicit device index\n" \
		" -1 - first discrete GPU\n" \
		" -2 - first integrated GPU");
	Cvar_CheckRange(r_device, -2, 8, qtrue);
	r_device->modified					= qfalse;

	//r_stencilbits						= Cvar_Get("r_stencilbits",						"8",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	r_ext_multisample					= Cvar_Get("r_ext_multisample",					"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	Cvar_CheckRange(r_ext_multisample, 0, 64, qtrue);
	r_ext_supersample					= Cvar_Get("r_ext_supersample",					"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	Cvar_CheckRange(r_ext_supersample, 0, 1, qtrue);
	r_ext_alpha_to_coverage				= Cvar_Get("r_ext_alpha_to_coverage",			"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	Cvar_CheckRange(r_ext_alpha_to_coverage, 0, 1, qtrue);
	r_hdr								= Cvar_Get("r_hdr",								"1",						CVAR_ARCHIVE | CVAR_LATCH, "");
	r_mapGreyScale						= Cvar_Get("r_mapGreyScale",						"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	Cvar_CheckRange(r_mapGreyScale, -1, 1, qfalse);
	r_ext_max_anisotropy				= Cvar_Get("r_ext_max_anisotropy",				"2",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	Cvar_CheckRange(r_ext_max_anisotropy, 1, 16, qtrue);
	r_greyscale							= Cvar_Get("r_greyscale",						"0",						CVAR_ARCHIVE_ND, "");
	Cvar_CheckRange(r_greyscale, -1, 1, qfalse);
	r_dither							= Cvar_Get("r_dither",							"0",						CVAR_ARCHIVE_ND, "Set dithering mode:\n 0 - disabled\n 1 - ordered");
	Cvar_CheckRange(r_dither, 0, 1, qtrue);
	r_presentBits						= Cvar_Get("r_presentBits",						"24",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Select color bits used for presentation surfaces");
	Cvar_CheckRange(r_presentBits, 16, 30, qtrue);
	r_bloom								= Cvar_Get("r_bloom",							"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Enable bloom effect");
	Cvar_CheckRange(r_bloom, 0, 1, qtrue);
	r_bloom_threshold					= Cvar_Get("r_bloom_threshold",					"0.05",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Color level to extract to bloom texture, default is 0.05");
	Cvar_CheckRange(r_bloom_threshold, 0.01f, 1, qfalse);
	r_bloom_threshold_mode				= Cvar_Get("r_bloom_threshold_mode",				"0",						CVAR_ARCHIVE_ND, "Color extraction mode:\n 0: (r|g|b) >= threshold\n 1: (r + g + b ) / 3 >= threshold\n 2: luma(r, g, b) >= threshold");
	r_bloom_intensity					= Cvar_Get("r_bloom_intensity",					"0.15",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Final bloom blend factor, default is 0.15");
	Cvar_CheckRange(r_bloom_intensity, 0.01f, 2, qfalse);
	r_bloom_modulate					= Cvar_Get("r_bloom_modulate",					"0",						CVAR_ARCHIVE_ND, "Modulate extracted color:\n 0: off (color = color, i.e. no changes)\n 1: by itself (color = color * color)\n 2: by intensity (color = color * luma(color))");
#ifdef HDR_DELUXE_LIGHTMAP
	r_deluxeMapping						= Cvar_Get("r_deluxeMapping",					"1",						CVAR_ARCHIVE, "Reading deluxemaps when compiled with q3map2:\n 0: off (approximated from lightgrid)\n 1: on (compiled deluxemaps)");
	r_deluxeSpecular					= Cvar_Get("r_deluxeSpecular",					"1",						CVAR_ARCHIVE, "Scale the specular response from deluxemaps");
#endif
#ifdef VK_DLIGHT_GPU
	r_dlightMethod						= Cvar_Get("r_dlightMethod",						"1",						CVAR_ARCHIVE, "Dynamic light method:\n 0: CPU-based (fallback)\n 1: GPU-based (requires r_normalMapping and r_specularMapping enabled) ");
#endif
#ifdef USE_PMLIGHT
	r_dlightMode						= Cvar_Get("r_dlightMode",						"2",						CVAR_ARCHIVE, "");
	Cvar_CheckRange(r_dlightMode, 0, 2, qtrue);
	r_dlightScale						= Cvar_Get("r_dlightScale",						"0.8",						CVAR_ARCHIVE_ND, "");
	Cvar_CheckRange(r_dlightScale, 0.1f, 1, qfalse);
	r_dlightIntensity					= Cvar_Get("r_dlightIntensity",					"1.0",						CVAR_ARCHIVE_ND, "");
	Cvar_CheckRange(r_dlightIntensity, 0.1f, 1, qfalse);
#endif

	r_dlightSaturation					= Cvar_Get("r_dlightSaturation",					"1",						CVAR_ARCHIVE_ND, "");
	Cvar_CheckRange(r_dlightSaturation, 0, 1, qfalse);

	r_roundImagesDown					= Cvar_Get("r_roundImagesDown",					"1",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	r_nomip								= Cvar_Get("r_nomip",							"0",						CVAR_ARCHIVE | CVAR_LATCH, "Apply picmip only on worldspawn textures");
	Cvar_CheckRange(r_nomip, 0, 1, qtrue);
	r_refraction						= Cvar_Get("r_refraction",						"1",						CVAR_ARCHIVE | CVAR_LATCH, "Screen-space refraction for distorting surfaces" );
	Cvar_CheckRange(r_refraction, 0, 1, qtrue);
	r_refractionScale					= Cvar_Get("r_refractionScale",					"1",						CVAR_ARCHIVE, "How far a refracting surface bends what is behind it" );
	Cvar_CheckRange(r_refractionScale, 0, 4, qfalse);
	r_refractionChromatic				= Cvar_Get("r_refractionChromatic",				"0.04",						CVAR_ARCHIVE, "How far apart the colour channels refract; 0 turns the split off" );
	Cvar_CheckRange(r_refractionChromatic, 0, 0.5, qfalse);
#ifdef USE_VK_PBR
	r_normalMapping						= Cvar_Get("r_normalMapping",					"1",						CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable normal mapping" );
	r_specularMapping					= Cvar_Get("r_specularMapping",					"1",						CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable specular mapping" );	
	r_baseNormalX						= Cvar_Get("r_baseNormalX",						"1.0",						CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_baseNormalY						= Cvar_Get("r_baseNormalY",						"1.0",						CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_baseParallax						= Cvar_Get("r_baseParallax",						"0.05",						CVAR_ARCHIVE | CVAR_LATCH, "" );	
	r_baseSpecular						= Cvar_Get("r_baseSpecular",						"0.04",						CVAR_ARCHIVE | CVAR_LATCH, "" );
#ifdef VK_CUBEMAP
	r_cubeMapping						= Cvar_Get("r_cubeMapping",						"0",						CVAR_ARCHIVE | CVAR_LATCH, "" );
#endif
#ifdef VK_COMPUTE_NORMALMAP
	r_genNormalMaps						= Cvar_Get("r_genNormalMaps",					"0",						CVAR_ARCHIVE | CVAR_LATCH, "Approximate normal maps from baked diffuse (albedo) textures" );
#endif
#endif


	r_renderWidth						= Cvar_Get("r_renderWidth",						"800",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	r_renderHeight						= Cvar_Get("r_renderHeight",						"600",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	r_renderScale						= Cvar_Get("r_renderScale",						"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Scaling mode to be used with custom render resolution:\n"
		" 0 - disabled\n"
		" 1 - nearest filtering, stretch to full size\n"
		" 2 - nearest filtering, preserve aspect ratio (black bars on sides)\n"
		" 3 - linear filtering, stretch to full size\n"
		" 4 - linear filtering, preserve aspect ratio (black bars on sides)\n");
	Cvar_CheckRange(r_renderScale, 0, 4, qtrue);
	r_ignorehwgamma						= Cvar_Get("r_ignorehwgamma",					"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Overrides hardware gamma capabilities");
	Cvar_CheckRange(r_ignorehwgamma, 0, 1, qtrue);


/*
Ghoul2 Insert Start
*/
#ifdef _DEBUG
	r_noPrecacheGLA						= Cvar_Get( "r_noPrecacheGLA",					"0",						CVAR_CHEAT, "" );
#endif
	r_noServerGhoul2					= Cvar_Get( "r_noserverghoul2",					"0",						CVAR_CHEAT, "" );
	r_Ghoul2AnimSmooth					= Cvar_Get( "r_ghoul2animsmooth",				"0.3",						CVAR_NONE, "" );
	// Ghoul2's own knobs, back from the single-player renderer this one
	// replaced. They arrived here commented out, along with the code they
	// guarded, because this renderer came by way of a multiplayer fork that had
	// dropped them - and one of them was not a debug switch at all.
	//
	// The name is spelt with the u it is missing everywhere else in this file;
	// the old one is registered too, so a config that has it keeps working.
	// How much bigger the head-up display is drawn than it was authored. Not
	// applied to menus: a menu is fitted to the window already, so there is
	// nothing for a magnifier to fix. Distance field text stays sharp at any
	// value of this, which is what makes it worth having at all - magnifying a
	// bitmap font was how it used to look.
	r_uiScale							= Cvar_Get( "r_uiScale",						"1",						CVAR_ARCHIVE,
											"Size of the head-up display, 0.5 to 2" );
	Cvar_CheckRange( r_uiScale, 0.5f, 2.0f, qfalse );

	r_Ghoul2UnSqashAfterSmooth			= Cvar_Get( "r_ghoul2unsquashaftersmooth",		"1",						CVAR_NONE,
											"Remove non-uniform scale from bones after animation smoothing" );
	Cvar_Get( "r_ghoul2unsqashaftersmooth", "1", CVAR_NONE, "Old spelling of r_ghoul2unsquashaftersmooth" );

	r_Ghoul2UnSqash						= Cvar_Get( "r_ghoul2unsquash",					"1",						CVAR_NONE,
											"Remove non-uniform scale from bone matrices. Off is what this renderer did before it was asked" );
	r_Ghoul2NoLerp						= Cvar_Get( "r_ghoul2nolerp",					"0",						CVAR_NONE,
											"Never interpolate between animation frames" );
	r_Ghoul2NoBlend						= Cvar_Get( "r_ghoul2noblend",					"0",						CVAR_NONE,
											"Never blend between animations" );
	r_Ghoul2BlendMultiplier				= Cvar_Get( "r_ghoul2blendmultiplier",			"1",						CVAR_NONE,
											"Scale every animation blend time. 0 disables blending" );
	broadsword							= Cvar_Get( "broadsword",						"0",						CVAR_ARCHIVE_ND, "" );
	broadsword_kickbones				= Cvar_Get( "broadsword_kickbones",				"1",						CVAR_NONE, "" );
	broadsword_kickorigin				= Cvar_Get( "broadsword_kickorigin",				"1",						CVAR_NONE, "" );
	broadsword_dontstopanim				= Cvar_Get( "broadsword_dontstopanim",			"0",						CVAR_NONE, "" );
	broadsword_waitforshot				= Cvar_Get( "broadsword_waitforshot",			"0",						CVAR_NONE, "" );
	broadsword_playflop					= Cvar_Get( "broadsword_playflop",				"1",						CVAR_NONE, "" );
	broadsword_smallbbox				= Cvar_Get( "broadsword_smallbbox",				"0",						CVAR_NONE, "" );
	broadsword_extra1					= Cvar_Get( "broadsword_extra1",					"0",						CVAR_NONE, "" );
	broadsword_extra2					= Cvar_Get( "broadsword_extra2",					"0",						CVAR_NONE, "" );
	broadsword_effcorr					= Cvar_Get( "broadsword_effcorr",				"1",						CVAR_NONE, "" );
	broadsword_ragtobase				= Cvar_Get( "broadsword_ragtobase",				"2",						CVAR_NONE, "" );
	broadsword_dircap					= Cvar_Get( "broadsword_dircap",					"64",						CVAR_NONE, "" );
/*
Ghoul2 Insert End
*/
	r_modelpoolmegs = Cvar_Get("r_modelpoolmegs", "20", CVAR_ARCHIVE, "" );
	if (R_LowPhysicalMemory() )
		Cvar_Set("r_modelpoolmegs", "0");

	r_aviMotionJpegQuality				= Cvar_Get( "r_aviMotionJpegQuality",			"100",						CVAR_ARCHIVE_ND, "" );
	r_screenshotJpegQuality				= Cvar_Get( "r_screenshotJpegQuality",			"100",						CVAR_ARCHIVE_ND, "" );

	Cvar_CheckRange( r_aviMotionJpegQuality, 10, 100, qtrue );
	Cvar_CheckRange( r_screenshotJpegQuality, 10, 100, qtrue );

	// Defined in tr_subs.cpp, read by rd-common's font loader. It had a
	// definition and no registration, so the first font registered dereferenced
	// a null pointer - which is what a cvar_t* that only exists to satisfy the
	// linker does.
	extern cvar_t *com_buildScript;
	com_buildScript = Cvar_Get( "com_buildScript", "0", CVAR_NONE, "1 touches every asset on load, 2 also registers foreign fonts" );

	for ( size_t i = 0; i < numCommands; i++ )
		R_AddCommand( commands[i].cmd, commands[i].func, "" );
}

/*
===============
R_Init
===============
*/
extern void R_InitWorldEffects( void ); //tr_WorldEffects.cpp
void R_Init( void ) {
	int i;
	byte *ptr;

	vk_debug("----- R_Init -----\n" );
	CL_RefPrintf(PRINT_ALL, "----- R_Init -----\n");
	// clear all our internal state
	Com_Memset( &tr, 0, sizeof( tr ) );
	Com_Memset( &backEnd, 0, sizeof( backEnd ) );
	Com_Memset( &tess, 0, sizeof( tess ) );
	//Com_Memset( &glState, 0, sizeof( glState ) );

#ifndef FINAL_BUILD
	if ( (intptr_t)tess.xyz & 15 ) {
		CL_RefPrintf(PRINT_WARNING, "tess.xyz not 16 byte aligned\n");
	}
#endif
	//
	// init function tables
	//
	for (i = 0; i < FUNCTABLE_SIZE; i++) {
#if 0
		if (i == 0) {
			tr.sinTable[i] = EPSILON;
		}
		else if (i == (FUNCTABLE_SIZE - 1)) {
			tr.sinTable[i] = -EPSILON;
		}
		else {
			tr.sinTable[i] = sin(DEG2RAD(i * 360.0f / ((float)(FUNCTABLE_SIZE - 1))));
		}
#else
		tr.sinTable[i] = sin( DEG2RAD( i * 360.0f / FUNCTABLE_SIZE ) + 0.0001f );
#endif
		tr.squareTable[i] = (i < FUNCTABLE_SIZE / 2) ? 1.0f : -1.0f;
		if (i == 0) {
			tr.sawToothTable[i] = EPSILON;
		}
		else {
			tr.sawToothTable[i] = (float)i / FUNCTABLE_SIZE;
		}
		tr.inverseSawToothTable[i] = 1.0f - tr.sawToothTable[i];
		if (i < FUNCTABLE_SIZE / 2) {
			if (i < FUNCTABLE_SIZE / 4) {
				if (i == 0) {
					tr.triangleTable[i] = EPSILON;
				}
				else {
					tr.triangleTable[i] = (float)i / (FUNCTABLE_SIZE / 4);
				}
			}
			else {
				tr.triangleTable[i] = 1.0f - tr.triangleTable[i - FUNCTABLE_SIZE / 4];
			}
		}
		else {
			tr.triangleTable[i] = -tr.triangleTable[i - FUNCTABLE_SIZE / 2];
		}
	}

	R_InitFogTable();
	R_ImageLoader_Init();
	R_NoiseInit();
	R_Register();

	max_polys = Q_min( r_maxpolys->integer, DEFAULT_MAX_POLYS );
	max_polyverts = Q_min( r_maxpolyverts->integer, DEFAULT_MAX_POLYVERTS );

	ptr = (byte *)R_Hunk_Alloc( 
		sizeof( *backEndData ) + 
		sizeof(srfPoly_t) * max_polys + 
		sizeof(polyVert_t) * max_polyverts +
		sizeof(Allocator) +
		PER_FRAME_MEMORY_BYTES,
		h_low);
	backEndData = (backEndData_t *)ptr;

	ptr = (byte *)(backEndData + 1);

	backEndData->polys = (srfPoly_t *)ptr;
	ptr += sizeof(*backEndData->polys) * max_polys;

	backEndData->polyVerts = (polyVert_t *)ptr;
	ptr += sizeof(*backEndData->polyVerts) * max_polyverts;

	backEndData->perFrameMemory = new(ptr) Allocator(ptr + sizeof(*backEndData->perFrameMemory), PER_FRAME_MEMORY_BYTES);
	
	R_InitNextFrame();

	for(i = 0; i < MAX_LIGHT_STYLES; i++)
	{
		RE_SetLightStyle(i, -1);
	}

	R_InitImagesPool();

	vk_create_window();		// Vulkan

	// The VBO release used to be here, and here is too late: RE_Shutdown runs
	// before Hunk_Clear, and Hunk_Clear takes the VBO_t structures with it -
	// they are R_Hunk_Alloc'd - along with tr.numVBOs, which is how many there
	// were. By this line the count is zero and the loop frees nothing, while
	// the VkBuffers those structures described are still on the device. Moved
	// into RE_Shutdown, which is the last moment the bookkeeping still exists.

	R_Set2DRatio();
	R_InitImages();	

#ifdef _G2_GORE
	R_CreateGoreVBO();
#endif
	vk_create_pipelines();	// Vulkan

#ifdef VK_PBR_BRDFLUT
	vk_create_brfdlut();
#endif

	vk_set_clearcolor();

	R_InitShaders(qfalse);
	R_InitSkins();
	R_InitFonts();
	R_ModelInit();
	R_InitDecals();
	R_InitWorldEffects();
	RestoreGhoul2InfoArray();

	vk_debug("----- finished R_Init -----\n" );
}


// This need some tlc
/*
===============
RE_Shutdown
===============
*/
// LOADING A MAP: WHO OWNS WHAT, AND IN WHAT ORDER
//
// Four things happen, in this order, every time a map is loaded. Getting them
// wrong is how the first four crashes on real hardware happened, so the order
// is written down here rather than left to be reconstructed from call sites.
//
//   1. RE_Shutdown( 0, 0 )    from SV_SpawnServer.
//      Releases what the GPU holds: textures, pipelines, fonts, world effects,
//      the dissolve. tr itself is left alone.
//
//   2. Hunk_Clear             frees TAG_HUNKALLOC and TAG_HUNKMISCMODELS, and
//      then calls this function. Everything R_Hunk_Alloc ever returned stops
//      existing at that line: the world, its surfaces and its lightmap list,
//      every model_t, every shader_t, every skin_t, the shader text, the font
//      glyphs. This function's only job is that nothing points at any of it
//      afterwards.
//
//   3. re.SVModelInit()       from SV_SpawnServer, immediately after.
//      Puts the model list back into a valid empty state - it has to be here
//      and not later, because between this line and step 4 the server loads the
//      game library and spawns entities, and the game registers their models.
//      The renderer is otherwise empty for the whole of that window.
//
//   4. R_Init()               from RE_BeginRegistration, when the client
//      finally starts loading. Wipes tr a second time - the memset at the top
//      of it is not decoration - and rebuilds everything: images, shaders,
//      skins, fonts, models, decals, world effects.
//
// THE RULE. Between 2 and 4 the renderer has no shaders and no images, and
// callers reached in that window have to expect a miss rather than assume a
// result - see R_LoadMDXM, which is reached exactly there.
//
// AND THE CONSEQUENCE, which looks like a bug and is not one to fix here:
// anything the game registers in that window is thrown away at step 4. Measured
// on a map with no entities in it at all, G_ParseAnimFileSet precaches two
// skeletons and keeps their handles (NPC_stats.cpp asserts on the pair being
// consecutive); step 4 empties the model list under them and the next thing
// cgame registers takes handle 1. Multiplayer solved this with a second,
// server-side model list and a hunk mark to decide which one to use -
// G2_ShouldRegisterServer, which in this tree is stubbed to qfalse because
// single-player never had that mechanism. Vanilla single-player behaves exactly
// this way too, so the handles evidently are not dereferenced; it is written
// down here so that the next person to find it does not have to prove it twice.
//
// WHAT SURVIVES THE HUNK, and therefore must not be dropped by the wipe: the
// image pool (TAG_IMAGE_T), the Vulkan device state, and the model cache
// (TAG_MODEL_*), which step 3 clears deliberately. Anything else added to tr
// that is not hunk-allocated has to be added to the carry list below, or it
// leaks once per map load.
//
// WHAT POINTS INTO THE HUNK FROM OUTSIDE tr, and therefore cannot be reached by
// wiping tr, has to be told separately. Today that is three things, all in
// tr_shader.cpp, and each of them was a separate crash: the shader hash table,
// the shader text, and the index into that text.
// The half that has to run BEFORE the hunk is freed.
//
// tr.vbos[] and tr.ibos[] are R_Hunk_Alloc'd structures, and each one holds a
// VkBuffer and its allocation plus a staging pair. Those are device objects:
// the hunk describes them, it does not own them. Free the hunk first and the
// description is gone while the buffers are still on the device, with nothing
// left that knows they exist - which is what the validation layer was counting
// at vkDestroyDevice.
//
// RE_Shutdown releases them too, and on most paths runs first; this is for the
// paths where it does not. The first map load out of the menu is one: the
// client was never in a game, so CL_FlushMemory never ran, and the model VBO
// the menu built for its character went straight into the freed hunk.
void RE_HunkClearBegin( void )
{
	if ( vk.device == VK_NULL_HANDLE )
		return;

	// The buffers may still be referenced by the frame that just finished.
	vk_wait_idle();

#ifdef USE_VBO
	vk_release_vbo();
	vk_release_model_vbo();
#endif
}

void RE_HunkClear( void )
{
	extern void KillTheShaderHashTable( void );

	// The images do not die with the hunk. They are R_Z_Malloc'd under
	// TAG_IMAGE_T, which Hunk_Clear does not touch, and the hash table that
	// finds them is a file-static in vk_image.cpp that the wipe cannot reach
	// either. So they are all still there and still findable; the only thing
	// the wipe destroyed was tr's list of them.
	//
	// They do die with RE_Shutdown, which runs first on most paths - CL_MapLoading
	// takes the client through it - and takes the pool and the hash table
	// together. This carry is for the path where it does not: the first map load
	// out of the menu, where the client was never in a game. There the pool is
	// live and losing it would be the whole texture set.
	//
	// Losing that list leaks the entire texture set once per map load, and
	// costs more than memory: image_t::index is assigned from the pool's count,
	// so a restarted pool hands out indices that images already in the hash
	// table are using. Those indices address descriptor slots.
	//
	// Carried across, therefore. Everything else here did live in the hunk and
	// has to go. Only until step 4, which restarts the pool for real after
	// RE_Shutdown destroyed the textures it described - what this carry buys is
	// the window, where the game is registering models against a renderer that
	// otherwise has no idea what it owns.
	const image_pool_t images = tr.images;

	Com_Memset( &tr, 0, sizeof( tr ) );

	tr.images = images;

	// Four more point into the hunk from outside tr, so the wipe cannot reach
	// them either and each has to be told separately: the shader hash table,
	// the shader text, the index into that text, and the back end's own data
	// block. Two of the first three were crashes on real hardware, in that
	// order - clearing one only moved the fault along to the next.
	R_InitShaders( qtrue );
	KillTheShaderHashTable();

	// backEndData is R_Hunk_Alloc'd in R_Init and is a plain global, so it has
	// just become a pointer into freed memory. It is not idle in this window
	// either: registering a shader can call FixRenderCommandList, which writes
	// a terminator into the command list through this pointer, and the window
	// is full of shader registration - the server spawns entities and their
	// models bring their materials with them. Nulled rather than left, so that
	// anything else reaching for it in here says so instead of writing into
	// whatever now owns that memory.
	backEndData = NULL;
}

void RE_Shutdown( qboolean destroyWindow, qboolean restarting ) {
	vk_debug("RE_Shutdown( %i, %i )\n", destroyWindow, restarting);

	// The engine calls this on its way out of Com_Error, and one of the errors
	// it can be carrying is "this machine has no Vulkan driver" - raised from
	// inside vk_initialize, before there is an instance. Everything below wants
	// a device; without one it walks null function pointers and turns a message
	// the user could act on into a crash they cannot.
	if ( vk.instance == VK_NULL_HANDLE ) {
		for ( size_t i = 0; i < numCommands; i++ )
			Cmd_RemoveCommand( commands[i].cmd );

		vk_remove_crash_handler();
		Com_Memset( &tr, 0, sizeof( tr ) );
		Com_Memset( &backEnd, 0, sizeof( backEnd ) );
		Com_Memset( &tess, 0, sizeof( tess ) );
		tr.registered = qfalse;
		return;
	}

	for (size_t i = 0; i < numCommands; i++)
		Cmd_RemoveCommand(commands[i].cmd);

	R_ShutdownWorldEffects();
	R_ShutdownFonts();

	// Before the hunk goes, because the hunk is where the bookkeeping lives and
	// the device is where the buffers live. tr.vbos[] and tr.ibos[] are
	// R_Hunk_Alloc'd structures holding a VkBuffer and its allocation apiece,
	// plus a staging pair; Hunk_Clear frees the structures and RE_HunkClear
	// wipes the count, and after that nothing knows those buffers exist. The
	// validation layer counted them: two VkBuffer and two VkDeviceMemory left
	// undestroyed at vkDestroyDevice, once per model VBO that outlived a level
	// change. See backlog section 21.
	//
	// After the device is idle, because these buffers were referenced by the
	// command buffer of the frame that just finished, and vkDestroyBuffer on
	// one of those is its own spec violation.
#ifdef USE_VBO
	vk_wait_idle();
	vk_release_vbo();
	vk_release_model_vbo();
#endif

	// The wipe holds pointers to images and pipelines, both of which are about
	// to stop existing.
	R_DissolveShutdown();

	// contains vulkan resources/state, reinitialized on a map change.
	//if (tr.registered) {

		if (destroyWindow){
			//vk_delete_textures();

			if (restarting)
				SaveGhoul2InfoArray();
		}

		vk_delete_textures();
		vk_release_resources();
	//}

	//vk_release_resources(); not merged yet (https://github.com/ec-/Quake3e/commit/d31b84ebf2ab702686e98dff40b7673473026b30)

	if (destroyWindow) {
		vk_shutdown();
		Com_Memset(&glState, 0, sizeof(glState));

		if (destroyWindow && !restarting) {
			WIN_VK_DestroyWindow();
			Com_Memset(&glConfig, 0, sizeof(glConfig));
		}
	}

	tr.registered = qfalse;
	tr.inited = qfalse;
}

/*
=============
RE_EndRegistration

Touch all images to make sure they are resident
=============
*/
void RE_EndRegistration( void ) {
	vk_wait_idle();

	// command buffer is not in recording state at this stage
	// so we can't issue RB_ShowImages() here.
	// moved to RB_SwapBuffers
}

void RE_GetLightStyle( int style, color4ub_t color )
{
	if (style >= MAX_LIGHT_STYLES)
	{
	    Com_Error( ERR_FATAL, "RE_GetLightStyle: %d is out of range", (int)style );
		return;
	}

	byteAlias_t *baDest = (byteAlias_t *)&color, 
				*baSource = (byteAlias_t *)&styleColors[style];
	baDest->i = baSource->i;
}

void RE_SetLightStyle( int style, int color )
{
	if (style >= MAX_LIGHT_STYLES)
	{
	    Com_Error( ERR_FATAL, "RE_SetLightStyle: %d is out of range", (int)style );
		return;
	}

	byteAlias_t *ba = (byteAlias_t *)&styleColors[style];
	if ( ba->i != color) {
		ba->i = color;
	}
}

static void SetRangedFog( float range ) { tr.rangedFog = range; }

// G2API_BoltMatrixReconstruction and G2API_BoltMatrixSPMethod were here, unused
// and unexported. Both set a global that multiplayer's G2API_GetBoltMatrix read
// once and cleared; single-player exports neither and calls neither, and the
// behaviour they selected is now simply what the function does. See the note
// there.

//extern float tr_distortionAlpha; //opaque
//extern float tr_distortionStretch; //no stretch override
//extern qboolean tr_distortionPrePost; //capture before postrender phase?
//extern qboolean tr_distortionNegate; //negative blend mode
static void SetRefractionProperties( float distortionAlpha, float distortionStretch, qboolean distortionPrePost, qboolean distortionNegate ) {
	//tr_distortionAlpha = distortionAlpha;
	//tr_distortionStretch = distortionStretch;
	//tr_distortionPrePost = distortionPrePost;
	//tr_distortionNegate = distortionNegate;
}

static float GetDistanceCull( void ) { return tr.distanceCull; }

static void GetRealRes( int *w, int *h ) {
	*w = glConfig.vidWidth;
	*h = glConfig.vidHeight;
}

extern void R_SVModelInit( void ); //tr_model.cpp
extern void R_AutomapElevationAdjustment( float newHeight ); //tr_world.cpp
extern qboolean R_InitializeWireframeAutomap( void ); //tr_world.cpp

extern qhandle_t RE_RegisterServerSkin( const char *name );

// How many distinct levels have been loaded this run.
//
// Not in tr, and that is the whole point: tr is wiped twice per map load, so a
// counter kept there reads zero every time it is asked. Both caches that age
// media by level - the model cache here and the sound cache in snd_dma.cpp,
// through re.RegisterMedia_GetLevel - compare an asset's last-used level
// against this one, and against a permanent zero nothing is ever older, so
// nothing was ever evicted.
static int	s_currentLevel = 0;

void C_LevelLoadBegin(const char *psMapName, ForceReload_e eForceReload)
{
	static char sPrevMapName[MAX_QPATH]={0};
	bool bDeleteModels = eForceReload == eForceReload_MODELS || eForceReload == eForceReload_ALL;

	if( bDeleteModels )
		CModelCache->DeleteAll();
	else if( Cvar_VariableIntegerValue( "sv_pure" ) )
		CModelCache->DumpNonPure();

	tr.numBSPModels = 0;

	/* If we're switching to the same level, don't increment current level */
	if (Q_stricmp( psMapName,sPrevMapName ))
	{
		Q_strncpyz( sPrevMapName, psMapName, sizeof(sPrevMapName) );
		s_currentLevel++;
	}
}

int C_GetLevel( void )
{
	return s_currentLevel;
}

// Whether the load that is finishing may close with a screen wipe. It arrives at
// the start of the load and is wanted at the end of it, which is the only reason
// it is a variable rather than an argument.
static qboolean	gbAllowScreenDissolve = qtrue;

void C_SetAllowScreenDissolve( qboolean allow )
{
	gbAllowScreenDissolve = allow;
}

void C_LevelLoadEnd( void )
{
	CModelCache->LevelLoadEnd( qfalse );
	SND_RegisterAudio_LevelLoadEnd( qfalse );

	// Before the music restarts, not after: starting the wipe is cheap, loading
	// the music is not, and the wipe's own clock does not start until the first
	// frame that draws it.
	if ( gbAllowScreenDissolve ) {
		RE_InitDissolve( qfalse );
	}

	S_RestartMusic();
}


// Declarations for the single-player export table below. These are not in any
// header: single-player's Ghoul2 API header does not declare the ragdoll and IK
// entries, and rd-vanilla declares them here too, for the same reason.
//
// Every signature is transcribed from the definition in our own sources, not
// from rd-vanilla. A declaration that disagrees with its definition compiles
// and then misbehaves, which is the most expensive way to be wrong in this
// port - see the project's phase 2 Ghoul2 notes.
extern qboolean	G2API_GetRagBonePos( CGhoul2Info_v &ghoul2, const char *boneName, vec3_t pos, vec3_t entAngles, vec3_t entPos, vec3_t entScale );
extern qboolean	G2API_RagEffectorGoal( CGhoul2Info_v &ghoul2, const char *boneName, vec3_t pos );
extern qboolean	G2API_RagEffectorKick( CGhoul2Info_v &ghoul2, const char *boneName, vec3_t velocity );
extern qboolean	G2API_RagForceSolve( CGhoul2Info_v &ghoul2, qboolean force );
extern qboolean	G2API_RagPCJConstraint( CGhoul2Info_v &ghoul2, const char *boneName, vec3_t min, vec3_t max );
extern qboolean	G2API_RagPCJGradientSpeed( CGhoul2Info_v &ghoul2, const char *boneName, const float speed );
extern qboolean	G2API_SetBoneIKState( CGhoul2Info_v &ghoul2, int time, const char *boneName, int ikState, sharedSetBoneIKStateParams_t *params );
extern qboolean	G2API_IKMove( CGhoul2Info_v &ghoul2, int time, sharedIKMoveParams_t *params );
extern void		G2API_SetRagDoll( CGhoul2Info_v &ghoul2, CRagDollParams *parms );
extern IGhoul2InfoArray &TheGhoul2InfoArray( void );

#ifdef G2_PERFORMANCE_ANALYSIS
extern void		G2Time_ResetTimers( void );
extern void		G2Time_ReportTimers( void );
#endif

// The weather system. Single-player reaches it through the export table; the
// renderer's own header declares only part of it.
extern bool		R_GetWindGusting( vec3_t atPoint );
extern bool		R_IsOutside( vec3_t pos );
extern float	R_IsOutsideCausingPain( vec3_t pos );
extern float	R_GetChanceOfSaberFizz( void );
extern bool		R_IsShaking( vec3_t pos );

#include "tr_sp_exports.h"

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" {
Q_EXPORT refexport_t* QDECL GetRefAPI( int apiVersion ) {
	static refexport_t re;

	memset( &re, 0, sizeof( re ) );

	if ( apiVersion != REF_API_VERSION ) {
		vk_debug("Mismatched REF_API_VERSION: expected %i, got %i\n", REF_API_VERSION, apiVersion );
		return NULL;
	}

	// the RE_ functions are Renderer Entry points

	// Single-player's export table, in its own declaration order. It is not a
	// subset of multiplayer's - the two diverged in both directions - so this
	// is a second table rather than the first one with holes in it. It goes
	// away with the module boundary in 2.2, when the engine calls these
	// directly and there is no table to fill.
	//
	// The comments are single-player's own, carried over from the declaration.

	// called before the library is unloaded
	// if the system is just reconfiguring, pass destroyWindow = qfalse,
	// which will keep the screen from flashing to the desktop.
	re.Shutdown                               = RE_Shutdown;

	// All data that will be used in a level should be
	// registered before rendering any frames to prevent disk hits,
	// but they can still be registered at a later time
	// if necessary.
	//
	// BeginRegistration makes any existing media pointers invalid
	// and returns the current gl configuration, including screen width
	// and height, which can be used by the client to intelligently
	// size display elements
	re.BeginRegistration                      = RE_BeginRegistration;
	re.RegisterModel                          = RE_RegisterModel;
	re.RegisterSkin                           = RE_RegisterSkin;
	re.GetAnimationCFG                        = RE_GetAnimationCFG;
	re.RegisterShader                         = RE_RegisterShader;
	re.RegisterShaderNoMip                    = RE_RegisterShaderNoMip;
	re.LoadWorld                              = RE_LoadWorldMap;
	re.R_LoadImage                            = R_LoadImage;

	// these two functions added to help with the new model alloc scheme...
	//
	re.RegisterMedia_LevelLoadBegin           = RE_SP_RegisterMedia_LevelLoadBegin;
	re.RegisterMedia_LevelLoadEnd             = C_LevelLoadEnd;
	re.RegisterMedia_GetLevel                 = C_GetLevel;
	re.RegisterModels_LevelLoadEnd            = C_Models_LevelLoadEnd;
	re.RegisterImages_LevelLoadEnd            = C_Images_LevelLoadEnd;

	// the vis data is a large enough block of data that we go to the trouble
	// of sharing it with the clipmodel subsystem
	re.SetWorldVisData                        = RE_SetWorldVisData;

	// EndRegistration will draw a tiny polygon with each texture, forcing
	// them to be loaded into card memory
	re.EndRegistration                        = RE_EndRegistration;

	// a scene is built up by calls to R_ClearScene and the various R_Add functions.
	// Nothing is drawn until R_RenderScene is called.
	re.ClearScene                             = RE_ClearScene;
	re.AddRefEntityToScene                    = RE_AddRefEntityToScene;
	re.AddPolyToScene                         = RE_SP_AddPolyToScene;
	re.AddLightToScene                        = RE_AddLightToScene;
	re.RenderScene                            = RE_RenderScene;
	re.GetLighting                            = RE_GetLighting;

	re.SetColor                               = RE_SetColor;
	re.DrawStretchPic                         = RE_StretchPic;
	re.DrawRotatePic                          = RE_RotatePic;
	re.DrawRotatePic2                         = RE_RotatePic2;
	re.LAGoggles                              = RE_LAGoggles;
	re.Scissor                                = RE_Scissor;
	re.Set2DSpace                             = RE_Set2DSpace;

	// Draw images for cinematic rendering, pass as 32 bit rgba
	re.DrawStretchRaw                         = RE_StretchRaw;
	re.UploadCinematic                        = RE_UploadCinematic;

	re.BeginFrame                             = RE_BeginFrame;

	// if the pointers are not NULL, timing info will be returned
	re.EndFrame                               = RE_EndFrame;

	re.ProcessDissolve                        = RE_ProcessDissolve;
	re.InitDissolve                           = RE_InitDissolve;


	// for use with save-games mainly...
	re.GetScreenShot                          = RE_GetScreenShot;

	// Filled for both games. The entries are declared unconditionally, the
	// functions are compiled unconditionally, and only Jedi Outcast's save code
	// calls them - so the #ifdef that used to be here bought a pair of null
	// pointers in one build and nothing else. It was the only place in this
	// renderer that knew which game it was drawing.
	re.SaveJPGToBuffer                        = RE_SaveJPGToBuffer;
	re.LoadJPGFromBuffer                      = LoadJPGFromBuffer;

	// this is so you can get access to raw pixels from a graphics format (TGA/JPG/BMP etc),
	//	currently only the save game uses it (to make raw shots for the autosaves)
	//
	re.TempRawImage_ReadFromFile              = RE_TempRawImage_ReadFromFile;
	re.TempRawImage_CleanUp                   = RE_TempRawImage_CleanUp;

	//misc stuff
	re.MarkFragments                          = R_MarkFragments;

	//model stuff
	re.LerpTag                                = RE_SP_LerpTag;
	re.ModelBounds                            = R_ModelBounds;

	re.GetLightStyle                          = RE_GetLightStyle;
	re.SetLightStyle                          = RE_SetLightStyle;

	re.GetBModelVerts                         = RE_GetBModelVerts;
	re.WorldEffectCommand                     = RE_WorldEffectCommand;
	re.GetModelBounds                         = RE_GetModelBounds;

	re.RegisterFont                           = RE_RegisterFont;

	re.Font_HeightPixels                      = RE_Font_HeightPixels;
	re.Font_StrLenPixels                      = RE_Font_StrLenPixels;
	re.Font_DrawString                        = RE_Font_DrawString;
	re.Font_StrLenChars                       = RE_Font_StrLenChars;
	re.Language_IsAsian                       = Language_IsAsian;
	re.Language_UsesSpaces                    = Language_UsesSpaces;
	re.AnyLanguage_ReadCharFromString         = AnyLanguage_ReadCharFromString;
	re.AnyLanguage_ReadCharFromString2        = AnyLanguage_ReadCharFromString2;

	// Misc
	re.R_InitWorldEffects                     = R_InitWorldEffects;
	re.RE_HunkClearBegin = RE_HunkClearBegin;
	re.RE_HunkClear = RE_HunkClear;
	re.R_inPVS                                = RE_SP_inPVS;

	re.SVModelInit                            = R_SVModelInit;

	// Distortion effects
	re.tr_distortionAlpha                     = get_tr_distortionAlpha;
	re.tr_distortionStretch                   = get_tr_distortionStretch;
	re.tr_distortionPrePost                   = get_tr_distortionPrePost;
	re.tr_distortionNegate                    = get_tr_distortionNegate;

	// Weather effects
	re.GetWindVector                          = RE_SP_GetWindVector;
	re.GetWindGusting                         = RE_SP_GetWindGusting;
	re.IsOutside                              = R_IsOutside;
	re.IsOutsideCausingPain                   = R_IsOutsideCausingPain;
	re.GetChanceOfSaberFizz                   = R_GetChanceOfSaberFizz;
	re.IsShaking                              = RE_SP_IsShaking;
	re.AddWeatherZone                         = RE_AddWeatherZone;
	re.SetTempGlobalFogColor                  = R_SetTempGlobalFogColor;

	re.SetRangedFog                           = SetRangedFog;

	// GHOUL 2
	re.TheGhoul2InfoArray                     = TheGhoul2InfoArray;

	// GHOUL 2 API
	re.G2API_AddBolt                          = G2API_AddBolt;
	re.G2API_AddBoltSurfNum                   = G2API_AddBoltSurfNum;
	re.G2API_AddSurface                       = G2API_AddSurface;
	re.G2API_AnimateG2Models                  = G2API_AnimateG2Models;
	re.G2API_AttachEnt                        = G2API_AttachEnt;
	re.G2API_AttachG2Model                    = G2API_AttachG2Model;
	re.G2API_CollisionDetect                  = G2API_CollisionDetect;
	re.G2API_CleanGhoul2Models                = G2API_CleanGhoul2Models;
	re.G2API_CopyGhoul2Instance               = G2API_CopyGhoul2Instance;
	re.G2API_DetachEnt                        = G2API_DetachEnt;
	re.G2API_DetachG2Model                    = G2API_DetachG2Model;
	re.G2API_GetAnimFileName                  = G2API_GetAnimFileName;
	re.G2API_GetAnimFileNameIndex             = G2API_GetAnimFileNameIndex;
	re.G2API_GetAnimFileInternalNameIndex     = G2API_GetAnimFileInternalNameIndex;
	re.G2API_GetAnimIndex                     = G2API_GetAnimIndex;
	re.G2API_GetAnimRange                     = G2API_GetAnimRange;
	re.G2API_GetAnimRangeIndex                = G2API_GetAnimRangeIndex;
	re.G2API_GetBoneAnim                      = G2API_GetBoneAnim;
	re.G2API_GetBoneAnimIndex                 = G2API_GetBoneAnimIndex;
	re.G2API_GetBoneIndex                     = G2API_GetBoneIndex;
	re.G2API_GetBoltMatrix                    = G2API_GetBoltMatrix;
	re.G2API_GetGhoul2ModelFlags              = G2API_GetGhoul2ModelFlags;
	re.G2API_GetGLAName                       = G2API_GetGLAName;
	re.G2API_GetParentSurface                 = G2API_GetParentSurface;
	re.G2API_GetRagBonePos                    = G2API_GetRagBonePos;
	re.G2API_GetSurfaceIndex                  = G2API_GetSurfaceIndex;
	re.G2API_GetSurfaceName                   = G2API_GetSurfaceName;
	re.G2API_GetSurfaceRenderStatus           = G2API_GetSurfaceRenderStatus;
	re.G2API_GetTime                          = G2API_GetTime;
	re.G2API_GiveMeVectorFromMatrix           = G2API_GiveMeVectorFromMatrix;
	re.G2API_HaveWeGhoul2Models               = G2API_HaveWeGhoul2Models;
	re.G2API_IKMove                           = G2API_IKMove;
	re.G2API_InitGhoul2Model                  = G2API_InitGhoul2Model;
	re.G2API_IsPaused                         = G2API_IsPaused;
	re.G2API_ListBones                        = G2API_ListBones;
	re.G2API_ListSurfaces                     = G2API_ListSurfaces;
	re.G2API_LoadGhoul2Models                 = G2API_LoadGhoul2Models;
	re.G2API_LoadSaveCodeDestructGhoul2Info   = G2API_LoadSaveCodeDestructGhoul2Info;
	re.G2API_PauseBoneAnim                    = G2API_PauseBoneAnim;
	re.G2API_PauseBoneAnimIndex               = G2API_PauseBoneAnimIndex;
	re.G2API_PrecacheGhoul2Model              = G2API_PrecacheGhoul2Model;
	re.G2API_RagEffectorGoal                  = G2API_RagEffectorGoal;
	re.G2API_RagEffectorKick                  = G2API_RagEffectorKick;
	re.G2API_RagForceSolve                    = G2API_RagForceSolve;
	re.G2API_RagPCJConstraint                 = G2API_RagPCJConstraint;
	re.G2API_RagPCJGradientSpeed              = G2API_RagPCJGradientSpeed;
	re.G2API_RemoveBolt                       = G2API_RemoveBolt;
	re.G2API_RemoveBone                       = G2API_RemoveBone;
	re.G2API_RemoveGhoul2Model                = G2API_RemoveGhoul2Model;
	re.G2API_RemoveSurface                    = G2API_RemoveSurface;
	re.G2API_SaveGhoul2Models                 = G2API_SaveGhoul2Models;
	re.G2API_SetAnimIndex                     = G2API_SetAnimIndex;
	re.G2API_SetBoneAnim                      = G2API_SetBoneAnim;
	re.G2API_SetBoneAnimIndex                 = G2API_SetBoneAnimIndex;
	re.G2API_SetBoneAngles                    = G2API_SetBoneAngles;
	re.G2API_SetBoneAnglesIndex               = G2API_SetBoneAnglesIndex;
	re.G2API_SetBoneAnglesMatrix              = G2API_SetBoneAnglesMatrix;
	re.G2API_SetBoneAnglesMatrixIndex         = G2API_SetBoneAnglesMatrixIndex;
	re.G2API_SetBoneIKState                   = G2API_SetBoneIKState;
	re.G2API_SetGhoul2ModelFlags              = G2API_SetGhoul2ModelFlags;
	re.G2API_SetGhoul2ModelIndexes            = G2API_SetGhoul2ModelIndexes;
	re.G2API_SetLodBias                       = G2API_SetLodBias;
	re.G2API_SetNewOrigin                     = G2API_SetNewOrigin;
	re.G2API_SetRagDoll                       = G2API_SetRagDoll;
	re.G2API_SetRootSurface                   = G2API_SetRootSurface;
	re.G2API_SetShader                        = G2API_SetShader;
	re.G2API_SetSkin                          = G2API_SetSkin;
	re.G2API_SetSurfaceOnOff                  = G2API_SetSurfaceOnOff;
	re.G2API_SetTime                          = G2API_SetTime;
	re.G2API_StopBoneAnim                     = G2API_StopBoneAnim;
	re.G2API_StopBoneAnimIndex                = G2API_StopBoneAnimIndex;
	re.G2API_StopBoneAngles                   = G2API_StopBoneAngles;
	re.G2API_StopBoneAnglesIndex              = G2API_StopBoneAnglesIndex;

#ifdef _G2_GORE
	re.G2API_AddSkinGore                      = G2API_AddSkinGore;
	re.G2API_ClearSkinGore                    = G2API_ClearSkinGore;
#endif

	// Performance analysis (perform anal)
#ifdef G2_PERFORMANCE_ANALYSIS
	re.G2Time_ResetTimers                     = G2Time_ResetTimers;
	re.G2Time_ReportTimers                    = G2Time_ReportTimers;
#endif

	#ifdef _G2_GORE
	#endif // _SOF2

	return &re;
}

} //extern "C"
