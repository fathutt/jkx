/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The screen dissolve: single-player wipes from the old screen to the new one
// when a cinematic ends or a level starts. Multiplayer has no such thing, which
// is why this had to be written rather than found.
//
// The screen from before the change is captured into an image, and then drawn
// back over the new one as a handful of triangles whose vertex alpha is the
// wipe: one where the old screen is untouched, zero where the new one has taken
// over, and a ramp across the boundary. That is the whole of it. There is no
// mask texture, no fragment shader work and no second pass.
//
// The retail construction was different and is worth knowing, because it is why
// the wipe used to look the way it did. The depth buffer was cleared and used as
// a stencil: a boundary picture was drawn with an alpha TEST to write depth, and
// the old screen was then drawn once with the depth test set to EQUAL. An alpha
// test has two answers, so every pixel came out as either the old screen or the
// new one; the only softness available was the dither pattern baked into that
// picture, magnified from 64 texels to the width of the screen.
//
// What else changed in the move to Vulkan:
//
//   - No power-of-two dance. The OpenGL version expanded the captured screen
//     into a power-of-two texture, cleared the margins, and resampled if the
//     result was over the driver's limit. Vulkan takes the screen size as it is.
//   - The capture image is created once and re-uploaded, because images here do
//     not have a delete: creating one per dissolve would leak one per level.
//   - Drawing is a render command rather than immediate calls, so it lands in
//     frame order with the rest of the 2D drawing instead of ahead of it.
//

#include "tr_local.h"


// How long the whole wipe takes.
#define fDISSOLVE_SECONDS	0.75f

// How wide the soft edge is, as a fraction of the screen. The original was a
// 64-texel picture stretched across a 640-wide screen, so a tenth is what the
// wipe has always looked like - the difference is that this one is a fraction
// rather than a number of texels, and therefore the same shape at any size.
#define fDISSOLVE_EDGE	0.10f

// Segments around the circular wipes. Sixty makes a six-degree step, which is
// under a pixel of chord at any resolution anyone runs.
#define iDISSOLVE_SEGMENTS	60

typedef enum
{
	eDISSOLVE_RT_TO_LT = 0,
	eDISSOLVE_LT_TO_RT,
	eDISSOLVE_TP_TO_BT,
	eDISSOLVE_BT_TO_TP,
	eDISSOLVE_CIRCULAR_OUT,		// the new screen comes out of the centre
	//
	eDISSOLVE_RAND_LIMIT,		// a label, not a type: the random pick stops here
	//
	eDISSOLVE_CIRCULAR_IN,		// the new screen comes in from the edges
	//
	eDISSOLVE_NUMBEROF

} Dissolve_e;

typedef struct {
	int			width;			// the captured screen, in pixels
	int			height;
	image_t		*screen;		// the old screen
	int			startTime;		// 0 means no dissolve is running
	Dissolve_e	type;
	qboolean	touchNeeded;
} dissolve_t;

static dissolve_t	dissolve;

// One pipeline, built on first use and then kept: a dissolve starts at a moment
// when nobody wants to wait for a pipeline to compile.
//
// It used to be three, because the mask was made by writing the boundary into
// the depth buffer and then drawing the old screen with a depth-equal test - a
// way of getting a mask out of hardware that could not blend the way this does.
// Blending the old screen over the new one with an alpha ramp in the vertex
// colours costs one pipeline, no depth buffer, and no texture, and the ramp is
// arithmetic - so it is smooth at any size and needs nothing from the retail
// data. gfx/2d/iris_mono, gfx/2d/iris_mono_rev and textures/common/dissolve are
// no longer looked for; without them there used to be no wipe at all.
static uint32_t		dissolve_pipeline;
static qboolean		dissolve_pipelines_built;

static void R_DissolveBuildPipelines( void )
{
	Vk_Pipeline_Def def;

	if ( dissolve_pipelines_built ) {
		return;
	}

	Com_Memset( &def, 0, sizeof( def ) );
	def.shader_type = TYPE_SINGLE_TEXTURE;
	def.face_culling = CT_TWO_SIDED;
	def.state_bits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;

	dissolve_pipeline = vk_find_pipeline_ext( 0, &def, qtrue );

	dissolve_pipelines_built = qtrue;
}

/*
=================
R_KillDissolve

The capture image is kept, not freed - images in this renderer live until the
video mode changes, so the next dissolve uploads over this one.
=================
*/
static void R_KillDissolve( void )
{
	dissolve.startTime = 0;
}

/*
=================
RB_DissolveEmit

The old screen, as geometry whose vertex alpha is the wipe.

Texture coordinates come from position rather than from the caller: the captured
screen covers the whole of this space by definition, so a vertex at x is looking
at x/width of the picture. That removes the possibility of a quad whose corners
and whose texture disagree, which is what the four hand-written corner pairs per
wipe direction were.
=================
*/
typedef struct {
	float	x, y;
	float	alpha;		// 1 is still the old screen, 0 is the new one
} dissolveVert_t;

static void RB_DissolveEmit( const dissolveVert_t *verts, int numVerts,
	const int *indexes, int numIndexes, float spaceW, float spaceH )
{
	int i;

	if ( numVerts < 3 || numIndexes < 3 ) {
		return;
	}

	tess.numVertexes = 0;
	tess.numIndexes = 0;

	for ( i = 0; i < numVerts; i++ ) {
		tess.xyz[i][0] = verts[i].x;
		tess.xyz[i][1] = verts[i].y;
		tess.xyz[i][2] = 0.0f;
		tess.xyz[i][3] = 1.0f;

		tess.texCoords[0][i][0] = verts[i].x / spaceW;
		tess.texCoords[0][i][1] = verts[i].y / spaceH;

		tess.svars.colors[0][i][0] = 255;
		tess.svars.colors[0][i][1] = 255;
		tess.svars.colors[0][i][2] = 255;
		tess.svars.colors[0][i][3] = (byte)( verts[i].alpha * 255.0f );
	}

	for ( i = 0; i < numIndexes; i++ ) {
		tess.indexes[i] = indexes[i];
	}

	tess.numVertexes = numVerts;
	tess.numIndexes = numIndexes;

	vk_select_texture( 0 );
	vk_bind( dissolve.screen );

	tess.svars.texcoordPtr[0] = tess.texCoords[0];

	vk_bind_pipeline( dissolve_pipeline );
	vk_bind_index_ext( tess.numIndexes, tess.indexes );
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 | TESS_ST0 );
	vk_bind_geometry_buffer();
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	tess.numVertexes = 0;
	tess.numIndexes = 0;
}

/*
=================
RB_DissolveLinear

A wipe that runs along one axis.

The old screen is drawn over the new one, so the alpha is how much of the old
screen is left: one where it is untouched, zero where the new screen has taken
over, and a ramp between the two. The ramp is a band fDISSOLVE_EDGE wide that
travels from just off one edge to just off the other - which is what makes the
wipe begin on a whole old picture and end on a whole new one, rather than
starting with the boundary already part way across.

  alpha = 0            before the trailing edge   (the new screen)
        = 0 .. 1       across the band
        = 1            after the leading edge     (the old screen)
=================
*/
static void RB_DissolveLinear( float progress, float spaceW, float spaceH,
	qboolean bVertical, qboolean bReverse )
{
	const float	extent = bVertical ? spaceH : spaceW;
	const float	across = bVertical ? spaceW : spaceH;
	const float	band = extent * fDISSOLVE_EDGE;

	// The band travels the whole extent plus its own width at each end.
	const float	lead = -band + ( extent + band * 2.0f ) * progress;
	const float	trail = lead - band;

	// Three positions along the axis and the alpha at each: the band's two edges
	// and the far end. Everything before the trailing edge is the new screen and
	// is simply not drawn.
	const float	stops[3] = { trail, lead, extent };
	const float	alphas[3] = { 0.0f, 1.0f, 1.0f };

	dissolveVert_t	verts[6];
	int				indexes[12];
	int				i;

	for ( i = 0; i < 3; i++ ) {
		// A reversed wipe is the same construction measured from the other end.
		const float along = bReverse ? ( extent - stops[i] ) : stops[i];

		if ( bVertical ) {
			verts[i * 2 + 0].x = 0.0f;		verts[i * 2 + 0].y = along;
			verts[i * 2 + 1].x = across;	verts[i * 2 + 1].y = along;
		} else {
			verts[i * 2 + 0].x = along;		verts[i * 2 + 0].y = 0.0f;
			verts[i * 2 + 1].x = along;		verts[i * 2 + 1].y = across;
		}

		verts[i * 2 + 0].alpha = alphas[i];
		verts[i * 2 + 1].alpha = alphas[i];
	}

	// Two strips between the three lines. Winding does not matter: the pipeline
	// is two-sided, which it has to be because a reversed wipe mirrors these.
	for ( i = 0; i < 2; i++ ) {
		const int a = i * 2;
		indexes[i * 6 + 0] = a;		indexes[i * 6 + 1] = a + 1;	indexes[i * 6 + 2] = a + 3;
		indexes[i * 6 + 3] = a;		indexes[i * 6 + 4] = a + 3;	indexes[i * 6 + 5] = a + 2;
	}

	RB_DissolveEmit( verts, 6, indexes, 12, spaceW, spaceH );
}

/*
=================
RB_DissolveCircular

An iris, built from rings rather than from a picture of a circle - so it is round
at any resolution and there is no texture to find.

Same rule as the linear wipe: the alpha is how much of the old screen is left.
Outward, the new screen grows from the centre and the old one survives outside;
inward, the old screen is the disc and it shrinks. Both are the same three rings
with the alphas the other way round.
=================
*/
static void RB_DissolveCircular( float progress, float spaceW, float spaceH, qboolean bOutward )
{
	static dissolveVert_t	verts[( iDISSOLVE_SEGMENTS + 1 ) * 3];
	static int				indexes[iDISSOLVE_SEGMENTS * 12];

	const float	cx = spaceW * 0.5f;
	const float	cy = spaceH * 0.5f;

	// Far enough to reach the corners, so that a fully open iris really has
	// nothing of the old screen left anywhere - including in them.
	const float	reach = sqrtf( cx * cx + cy * cy );
	const float	band = reach * fDISSOLVE_EDGE * 2.0f;

	// The boundary sweeps from just inside nothing to past the corners.
	const float	p = bOutward ? progress : ( 1.0f - progress );
	const float	inner = -band + ( reach + band ) * p;
	const float	outer = inner + band;

	// Three radii, and the alpha at each. Outward: nothing inside the boundary,
	// old screen outside it. Inward: the reverse.
	const float	radii[3] = {
		( inner > 0.0f ) ? inner : 0.0f,
		( outer > 0.0f ) ? outer : 0.0f,
		reach + band
	};
	const float	alphas[3] = {
		bOutward ? 0.0f : 1.0f,
		bOutward ? 1.0f : 0.0f,
		bOutward ? 1.0f : 0.0f
	};

	int	numVerts = 0, numIndexes = 0;
	int	i, r;

	for ( r = 0; r < 3; r++ ) {
		for ( i = 0; i <= iDISSOLVE_SEGMENTS; i++ ) {
			const float ang = ( (float)i / (float)iDISSOLVE_SEGMENTS ) * 2.0f * (float)M_PI;

			verts[numVerts].x = cx + cosf( ang ) * radii[r];
			verts[numVerts].y = cy + sinf( ang ) * radii[r];
			verts[numVerts].alpha = alphas[r];
			numVerts++;
		}
	}

	// Two ring strips. Inward, the middle of the disc is covered because the
	// innermost ring collapses to the centre when the boundary passes it; the
	// solid part of an inward iris is the first strip, whose inner edge is at
	// radius zero once inner has gone negative.
	for ( r = 0; r < 2; r++ ) {
		const int base = r * ( iDISSOLVE_SEGMENTS + 1 );

		for ( i = 0; i < iDISSOLVE_SEGMENTS; i++ ) {
			const int a = base + i;
			const int b = a + iDISSOLVE_SEGMENTS + 1;

			indexes[numIndexes++] = a;		indexes[numIndexes++] = b;		indexes[numIndexes++] = b + 1;
			indexes[numIndexes++] = a;		indexes[numIndexes++] = b + 1;	indexes[numIndexes++] = a + 1;
		}
	}

	RB_DissolveEmit( verts, numVerts, indexes, numIndexes, spaceW, spaceH );
}

/*
=================
RB_Dissolve

Draw one frame of the wipe over what has already been drawn.
=================
*/
const void *RB_Dissolve( const void *data )
{
	const dissolveCommand_t	*cmd = (const dissolveCommand_t *)data;

	if ( !dissolve.screen ) {
		return (const void *)( cmd + 1 );
	}

	RB_EndSurface();

	// The head-up display's space, not the fitted frame. A wipe covers the
	// picture, and the picture is the window: drawn in the frame it stopped at
	// the edges of a 4:3 box in the middle of a wide screen, and the scene
	// changed underneath the black bars a moment before it changed between them.
	backEnd.space2D = SPACE2D_SCREEN;
	backEnd.projection2D = qfalse;
	vk_set_2d();

	const float spaceW = ( ( glConfig.virtualWidth > 0.0f )
		? glConfig.virtualWidth : (float)SCREEN_WIDTH ) / vk_ui_scale();
	const float spaceH = (float)SCREEN_HEIGHT / vk_ui_scale();

	const float progress = (float)cmd->percentage / 100.0f;

	switch ( dissolve.type )
	{
		case eDISSOLVE_RT_TO_LT:	RB_DissolveLinear( progress, spaceW, spaceH, qfalse, qtrue );	break;
		case eDISSOLVE_LT_TO_RT:	RB_DissolveLinear( progress, spaceW, spaceH, qfalse, qfalse );	break;
		case eDISSOLVE_TP_TO_BT:	RB_DissolveLinear( progress, spaceW, spaceH, qtrue, qfalse );	break;
		case eDISSOLVE_BT_TO_TP:	RB_DissolveLinear( progress, spaceW, spaceH, qtrue, qtrue );	break;
		case eDISSOLVE_CIRCULAR_OUT: RB_DissolveCircular( progress, spaceW, spaceH, qtrue );	break;
		case eDISSOLVE_CIRCULAR_IN:	RB_DissolveCircular( progress, spaceW, spaceH, qfalse );	break;
		default:					break;
	}

	// Hand the batch back the way it was found. The 2D drawing that follows -
	// the menu, the console - checks whether its shader is the one already in
	// tess and appends to it if so, so tess has to be a batch and not the
	// leftovers of the geometry above.
	if ( tess.shader ) {
		RB_BeginSurface( tess.shader, tess.fogNum, 0 );
	}

	return (const void *)( cmd + 1 );
}

/*
=================
RE_ProcessDissolve

Called once a frame from the client, between the game's own 2D and the menu.
The answer is always qfalse, as it was in the OpenGL renderer: no caller has
ever looked at it.
=================
*/
qboolean RE_ProcessDissolve( void )
{
	dissolveCommand_t	*cmd;
	int					percentage;

	if ( !dissolve.startTime ) {
		return qfalse;
	}

	if ( dissolve.touchNeeded )
	{
		// The clock starts on the first frame that actually draws, not on the
		// call that set the dissolve up. Between the two the game restarts the
		// music, and on a slow machine that can eat the whole wipe before it has
		// been seen once.
		dissolve.touchNeeded = qfalse;
		dissolve.startTime = Sys_Milliseconds2();
	}

	percentage = (int)( ( ( Sys_Milliseconds2() - dissolve.startTime ) * 100 ) / ( 1000.0f * fDISSOLVE_SECONDS ) );

	// r_dissolveFreeze holds the wipe at one point instead of running it.
	//
	// A wipe lasts three quarters of a second measured in milliseconds, and
	// anything that wants to look at it counts frames - so which frame has the
	// boundary in it depends on how fast the machine is drawing, and a check
	// that photographs "a frame near the start" photographs whatever happened to
	// be there. That is not hypothetical: the first attempt at a check for the
	// soft edge passed identically against a build with the edge quantised back
	// to a step, because what it had caught was the menu underneath.
	if ( r_dissolveFreeze && r_dissolveFreeze->integer >= 0 ) {
		percentage = r_dissolveFreeze->integer;
		if ( percentage > 100 ) {
			percentage = 100;
		}
	} else if ( percentage > 100 ) {
		R_KillDissolve();
		return qfalse;
	}

	cmd = (dissolveCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return qfalse;
	}

	cmd->commandId = RC_DISSOLVE;
	cmd->percentage = percentage;

	return qfalse;
}

/*
=================
R_DissolveCaptureScreen

The last frame that was presented, as an RGBA image.

vk_read_pixels answers three bytes per pixel with the bottom row first, which is
what the screenshot path wants and the opposite of what a texture wants, so the
rows are turned over on the way into the image.
=================
*/
static qboolean R_DissolveCaptureScreen( void )
{
	const int	width = gls.captureWidth;
	const int	height = gls.captureHeight;
	byte		*rgb, *rgba;
	int			y, x;

	if ( width <= 0 || height <= 0 ) {
		return qfalse;
	}

	// vk_read_pixels waits on the fence of the last submitted command buffer. If
	// nothing has been submitted yet - the first cinematic can start before the
	// menus have drawn a single frame - that fence will never be signalled, and
	// the wait is a thousand seconds long. This is the same test the screenshot
	// path makes before reading pixels, and it means the same thing: there is a
	// finished frame to read.
	if ( !vk.cmd || !vk.cmd->waitForFence ) {
		return qfalse;
	}

	rgb = (byte *)R_Malloc( width * height * 3, TAG_TEMP_WORKSPACE, qfalse );
	rgba = (byte *)R_Malloc( width * height * 4, TAG_TEMP_WORKSPACE, qfalse );

	vk_read_pixels( rgb, width, height );

	for ( y = 0; y < height; y++ )
	{
		const byte	*src = rgb + (size_t)( height - 1 - y ) * width * 3;
		byte		*dst = rgba + (size_t)y * width * 4;

		for ( x = 0; x < width; x++, src += 3, dst += 4 )
		{
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = 255;
		}
	}

	if ( !dissolve.screen )
	{
		dissolve.screen = R_CreateImage( "*dissolveScreen", rgba, width, height,
			IMGFLAG_CLAMPTOEDGE | IMGFLAG_NOSCALE | IMGFLAG_NO_COMPRESSION, 0, 0 );
	}

	if ( dissolve.screen )
	{
		vk_bind( dissolve.screen );

		// uploadWidth, not width. They are not the same thing and the
		// difference was a dead graphics card.
		//
		// image_t::width is what the caller asked for; uploadWidth is what the
		// image system actually built, after it clamps to
		// glConfig.maxTextureSize - which is 2048 here, and is a limit of the
		// resample scratch rather than of the hardware. On any window wider than
		// that, R_CreateImage above made a 1280x360 image and recorded 5120x1440
		// as its size. This test then compared 5120 against 5120, decided the
		// image was already the right shape, and uploaded 5120x1440 texels into
		// it: a copy sixteen times larger than the destination, straight past
		// the end of the allocation.
		//
		// The driver's answer to that is VK_ERROR_DEVICE_LOST, reported at the
		// next fence wait with nothing to say about where it came from. On a
		// 5120x1440 monitor it happened at the first screen wipe - which is the
		// transition out of the introduction crawl, so the campaign died on the
		// way into its first map, every time. Below 2048 wide nothing is clamped
		// and none of this happens, which is why it took a 32:9 display to find.
		if ( dissolve.screen->uploadWidth != width || dissolve.screen->uploadHeight != height )
		{
			// Finish the uploads already recorded against the old image before
			// throwing it away. vk_create_image destroys the existing VkImage,
			// and the staging command buffer can be holding a copy into it that
			// has been recorded and not yet submitted - the previous wipe's.
			//
			// The validation layer says it plainly once the fixture has a wipe
			// to run: "vkEndCommandBuffer() was called in VkCommandBuffer which
			// is invalid because bound VkImage was destroyed". A software
			// rasteriser survives that. A driver does not have to.
#ifdef USE_UPLOAD_QUEUE
			vk_flush_staging_buffer( qfalse );
#endif
			vk_wait_idle();

			dissolve.screen->width = dissolve.screen->uploadWidth = width;
			dissolve.screen->height = dissolve.screen->uploadHeight = height;
			vk_create_image( dissolve.screen, width, height, 1 );
			vk_upload_image_data( dissolve.screen, 0, 0, width, height, 1, rgba, width * height * 4, qfalse );
		}
		else
		{
			vk_upload_image_data( dissolve.screen, 0, 0, width, height, 1, rgba, width * height * 4, qtrue );
		}
	}

	R_Free( rgba );
	R_Free( rgb );

	if ( !dissolve.screen ) {
		return qfalse;
	}

	dissolve.width = width;
	dissolve.height = height;

	return qtrue;
}

/*
=================
RE_InitDissolve

Grab the screen and start a wipe. bForceCircularExtroWipe is the end of the last
level, which always closes with the iris rather than a random pick.

qtrue means a dissolve is running.
=================
*/
qboolean RE_InitDissolve( qboolean bForceCircularExtroWipe )
{
	if ( !tr.registered || !vk.active ) {
		return qfalse;
	}

	// Whatever is still queued has to reach the screen before the screen is
	// captured, or the wipe starts from a frame the player never saw.
	R_IssueRenderCommands( qfalse );
	R_KillDissolve();

	if ( !R_DissolveCaptureScreen() ) {
		return qfalse;
	}

	if ( r_dissolveType && r_dissolveType->integer >= 0
		 && r_dissolveType->integer < eDISSOLVE_NUMBEROF ) {
		dissolve.type = (Dissolve_e)r_dissolveType->integer;
	} else {
		dissolve.type = bForceCircularExtroWipe
			? eDISSOLVE_CIRCULAR_IN
			: (Dissolve_e)Q_irand( 0, eDISSOLVE_RAND_LIMIT - 1 );
	}

	// There is nothing to look for. The boundary used to be a picture from the
	// retail data - textures/common/dissolve for the linear wipes, gfx/2d/
	// iris_mono for the round ones - and if it was not there the wipe did not
	// happen at all, which is what "no screen wipe" in the log meant. The shape
	// is geometry now.

	R_DissolveBuildPipelines();

	dissolve.startTime = Sys_Milliseconds2();	// replaced on the first drawn frame, but must not be zero
	dissolve.touchNeeded = qtrue;

	return qtrue;
}

/*
=================
R_DissolveShutdown

The images belong to the image manager and go with it; what has to be forgotten
here are the pointers to them and the pipelines, both of which are rebuilt after
a video restart.
=================
*/
void R_DissolveShutdown( void )
{
	Com_Memset( &dissolve, 0, sizeof( dissolve ) );
	dissolve_pipelines_built = qfalse;
}
