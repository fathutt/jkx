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
// The trick is the same one the OpenGL renderer used, because it is a good one.
// Nothing here masks in the fragment shader. Instead the depth buffer is cleared
// and used as a stencil: everything that should still show the OLD screen writes
// depth, and the old screen is then drawn once with the depth test set to EQUAL.
// The wipe boundary is a picture with an alpha ramp, drawn with an alpha test, so
// the ragged edge of the wipe comes out of the texture rather than out of code.
//
// Colour writes during the masking passes are a deliberate no-op - source times
// zero plus destination times one - so those passes leave the new screen exactly
// as it was drawn and touch only depth.
//
// What changed in the move to Vulkan:
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

// The masking quads overlap the boundary by a couple of pixels. Two edges that
// meet exactly can leave a seam of unwritten depth between them, and a seam
// shows up as a one-pixel line of the wrong screen.
#define iSAFETY_SPRITE_OVERLAP	2

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
	image_t		*mask;			// the wipe boundary, an alpha ramp
	image_t		*black;			// 8x8 of nothing, for the flat masking quads
	int			startTime;		// 0 means no dissolve is running
	Dissolve_e	type;
	qboolean	touchNeeded;
} dissolve_t;

static dissolve_t	dissolve;

// Three pipelines, one per job. Built on first use and then kept: the set is
// fixed, and a dissolve starts at a moment when nobody wants to wait for a
// pipeline to compile.
static uint32_t		dissolve_pipeline_mask;		// the alpha-tested boundary
static uint32_t		dissolve_pipeline_solid;	// the flat masking quads
static uint32_t		dissolve_pipeline_screen;	// the old screen itself
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

	// Writes depth where the alpha test passes and leaves colour alone.
	def.state_bits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE | GLS_ATEST_LT_80;
	dissolve_pipeline_mask = vk_find_pipeline_ext( 0, &def, qtrue );

	// The same, without the alpha test: writes depth over its whole quad.
	def.state_bits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE;
	dissolve_pipeline_solid = vk_find_pipeline_ext( 0, &def, qtrue );

	// Opaque, and drawn only where the two above wrote depth.
	def.state_bits = GLS_DEPTHFUNC_EQUAL;
	dissolve_pipeline_screen = vk_find_pipeline_ext( 0, &def, qtrue );

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
RB_DissolveBlit

One textured quad in the 2D virtual screen, with an explicit pipeline. The four
corners are given separately rather than as a rectangle because two of the wipes
draw the boundary right to left, which is a quad with its corners swapped.
=================
*/
static void RB_DissolveBlit( float x0, float y0, float x1, float y1,
	float x2, float y2, float x3, float y3, image_t *image, uint32_t pipeline )
{
	int i;

	if ( !image ) {
		return;
	}

	tess.numVertexes = 0;
	tess.numIndexes = 0;

	tess.xyz[0][0] = x0; tess.xyz[0][1] = y0;
	tess.xyz[1][0] = x1; tess.xyz[1][1] = y1;
	tess.xyz[2][0] = x2; tess.xyz[2][1] = y2;
	tess.xyz[3][0] = x3; tess.xyz[3][1] = y3;

	tess.texCoords[0][0][0] = 0.0f; tess.texCoords[0][0][1] = 0.0f;
	tess.texCoords[0][1][0] = 1.0f; tess.texCoords[0][1][1] = 0.0f;
	tess.texCoords[0][2][0] = 1.0f; tess.texCoords[0][2][1] = 1.0f;
	tess.texCoords[0][3][0] = 0.0f; tess.texCoords[0][3][1] = 1.0f;

	for ( i = 0; i < 4; i++ ) {
		tess.xyz[i][2] = 0.0f;
		tess.xyz[i][3] = 1.0f;
		tess.svars.colors[0][i][0] = 255;
		tess.svars.colors[0][i][1] = 255;
		tess.svars.colors[0][i][2] = 255;
		tess.svars.colors[0][i][3] = 255;
	}

	tess.indexes[0] = 0; tess.indexes[1] = 1; tess.indexes[2] = 2;
	tess.indexes[3] = 0; tess.indexes[4] = 2; tess.indexes[5] = 3;

	tess.numVertexes = 4;
	tess.numIndexes = 6;

	vk_select_texture( 0 );
	vk_bind( image );

	tess.svars.texcoordPtr[0] = tess.texCoords[0];

	vk_bind_pipeline( pipeline );
	vk_bind_index_ext( tess.numIndexes, tess.indexes );
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 | TESS_ST0 );
	vk_bind_geometry_buffer();
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	tess.numVertexes = 0;
	tess.numIndexes = 0;
}

/*
=================
RB_Dissolve

Draw one frame of the wipe over what has already been drawn.
=================
*/
const void *RB_Dissolve( const void *data )
{
	const dissolveCommand_t	*cmd;
	float	x0, y0, x1, y1, x2, y2, x3, y3;
	float	xScale, yScale;
	int		percentage;

	cmd = (const dissolveCommand_t *)data;

	if ( !dissolve.screen || !dissolve.mask || !dissolve.black ) {
		return (const void *)( cmd + 1 );
	}

	percentage = cmd->percentage;

	RB_EndSurface();
	vk_set_2d();

	// The depth buffer is the mask, so whatever the 3D view left in it has to go.
	vk_clear_depthstencil_attachments( qfalse );

	xScale = (float)SCREEN_WIDTH / (float)dissolve.width;
	yScale = (float)SCREEN_HEIGHT / (float)dissolve.height;

	// The boundary picture is square and its width sets how wide the fuzzy edge
	// is, in captured-screen pixels.
	const float maskWidth = (float)dissolve.mask->width;

	switch ( dissolve.type )
	{
		case eDISSOLVE_RT_TO_LT:
		{
			const float boundary = (float)dissolve.width -
				( ( (float)dissolve.width + maskWidth ) * (float)percentage ) / 100.0f;

			x0 = xScale * boundary;
			y0 = 0.0f;
			x1 = xScale * ( boundary + maskWidth );
			y1 = 0.0f;
			x2 = x1;
			y2 = yScale * dissolve.height;
			x3 = x0;
			y3 = y2;
			RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.mask, dissolve_pipeline_mask );

			// Everything left of the boundary is still the old screen.
			x0 = 0.0f;
			y0 = 0.0f;
			x1 = xScale * ( boundary + iSAFETY_SPRITE_OVERLAP );
			y1 = 0.0f;
			x2 = x1;
			y2 = yScale * dissolve.height;
			x3 = x0;
			y3 = y2;
			RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.black, dissolve_pipeline_solid );
		}
		break;

		case eDISSOLVE_LT_TO_RT:
		{
			const float boundary =
				( ( (float)dissolve.width + 2.0f * maskWidth ) * (float)percentage ) / 100.0f - maskWidth;

			x0 = xScale * ( boundary + maskWidth );
			y0 = 0.0f;
			x1 = xScale * boundary;
			y1 = 0.0f;
			x2 = x1;
			y2 = yScale * dissolve.height;
			x3 = x0;
			y3 = y2;
			RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.mask, dissolve_pipeline_mask );

			x0 = xScale * ( ( boundary + maskWidth ) - iSAFETY_SPRITE_OVERLAP );
			y0 = 0.0f;
			x1 = xScale * dissolve.width;
			y1 = 0.0f;
			x2 = x1;
			y2 = yScale * dissolve.height;
			x3 = x0;
			y3 = y2;
			RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.black, dissolve_pipeline_solid );
		}
		break;

		case eDISSOLVE_TP_TO_BT:
		{
			const float boundary =
				( ( (float)dissolve.height + 2.0f * maskWidth ) * (float)percentage ) / 100.0f - maskWidth;

			x0 = 0.0f;
			y0 = yScale * ( boundary + maskWidth );
			x1 = x0;
			y1 = yScale * boundary;
			x2 = xScale * dissolve.width;
			y2 = y1;
			x3 = x2;
			y3 = y0;
			RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.mask, dissolve_pipeline_mask );

			x0 = 0.0f;
			y0 = yScale * ( ( boundary + maskWidth ) - iSAFETY_SPRITE_OVERLAP );
			x1 = xScale * dissolve.width;
			y1 = y0;
			x2 = x1;
			y2 = yScale * dissolve.height;
			x3 = x0;
			y3 = y2;
			RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.black, dissolve_pipeline_solid );
		}
		break;

		case eDISSOLVE_BT_TO_TP:
		{
			const float boundary = (float)dissolve.height -
				( ( (float)dissolve.height + maskWidth ) * (float)percentage ) / 100.0f;

			x0 = 0.0f;
			y0 = yScale * boundary;
			x1 = x0;
			y1 = yScale * ( boundary + maskWidth );
			x2 = xScale * dissolve.width;
			y2 = y1;
			x3 = x2;
			y3 = y0;
			RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.mask, dissolve_pipeline_mask );

			x0 = 0.0f;
			y0 = 0.0f;
			x1 = xScale * dissolve.width;
			y1 = y0;
			x2 = x1;
			y2 = yScale * ( boundary + iSAFETY_SPRITE_OVERLAP );
			x3 = x0;
			y3 = y2;
			RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.black, dissolve_pipeline_solid );
		}
		break;

		case eDISSOLVE_CIRCULAR_IN:
		case eDISSOLVE_CIRCULAR_OUT:
		{
			// The iris picture is its own mask, so the circle needs no masking
			// quad - except on the way out, where the four corners of the screen
			// are outside the picture and have to be held down by hand.
			const float progress = ( dissolve.type == eDISSOLVE_CIRCULAR_IN )
				? (float)( 100 - percentage ) : (float)percentage;
			const float zoom = ( ( (float)dissolve.width * 0.8f ) * progress ) / 100.0f;

			x0 = xScale * ( ( dissolve.width / 2 ) - zoom );
			y0 = yScale * ( ( dissolve.height / 2 ) - zoom );
			x1 = xScale * ( ( dissolve.width / 2 ) + zoom );
			y1 = y0;
			x2 = x1;
			y2 = yScale * ( ( dissolve.height / 2 ) + zoom );
			x3 = x0;
			y3 = y2;
			RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.mask, dissolve_pipeline_mask );

			if ( dissolve.type == eDISSOLVE_CIRCULAR_OUT )
			{
				const float screenW = xScale * dissolve.width;
				const float screenH = yScale * dissolve.height;

				// left
				RB_DissolveBlit( 0.0f, 0.0f,
					x0 + iSAFETY_SPRITE_OVERLAP, 0.0f,
					x0 + iSAFETY_SPRITE_OVERLAP, screenH,
					0.0f, screenH,
					dissolve.black, dissolve_pipeline_solid );

				// right
				RB_DissolveBlit( x1 - iSAFETY_SPRITE_OVERLAP, 0.0f,
					screenW, 0.0f,
					screenW, screenH,
					x1 - iSAFETY_SPRITE_OVERLAP, screenH,
					dissolve.black, dissolve_pipeline_solid );

				// top
				RB_DissolveBlit( x0 - iSAFETY_SPRITE_OVERLAP, 0.0f,
					x1 + iSAFETY_SPRITE_OVERLAP, 0.0f,
					x1 + iSAFETY_SPRITE_OVERLAP, y0 + iSAFETY_SPRITE_OVERLAP,
					x0 - iSAFETY_SPRITE_OVERLAP, y0 + iSAFETY_SPRITE_OVERLAP,
					dissolve.black, dissolve_pipeline_solid );

				// bottom
				RB_DissolveBlit( x0 - iSAFETY_SPRITE_OVERLAP, y3 - iSAFETY_SPRITE_OVERLAP,
					x1 + iSAFETY_SPRITE_OVERLAP, y2 - iSAFETY_SPRITE_OVERLAP,
					x1 + iSAFETY_SPRITE_OVERLAP, screenH,
					x0 - iSAFETY_SPRITE_OVERLAP, screenH,
					dissolve.black, dissolve_pipeline_solid );
			}
		}
		break;

		default:
			// A type that does not exist means the state is wrong, and drawing
			// the old screen over everything would freeze the picture.
			return (const void *)( cmd + 1 );
	}

	// And now the old screen, which lands only where the passes above wrote depth.
	x0 = 0.0f;
	y0 = 0.0f;
	x1 = xScale * dissolve.width;
	y1 = y0;
	x2 = x1;
	y2 = yScale * dissolve.height;
	x3 = x0;
	y3 = y2;
	RB_DissolveBlit( x0, y0, x1, y1, x2, y2, x3, y3, dissolve.screen, dissolve_pipeline_screen );

	// Hand the batch back the way it was found. The 2D drawing that follows -
	// the menu, the console - checks whether its shader is the one already in
	// tess and appends to it if so, so tess has to be a batch and not the
	// leftovers of the four quads above.
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
		dissolve.startTime = ri.Milliseconds();
	}

	percentage = (int)( ( ( ri.Milliseconds() - dissolve.startTime ) * 100 ) / ( 1000.0f * fDISSOLVE_SECONDS ) );

	if ( percentage > 100 ) {
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
	else
	{
		vk_bind( dissolve.screen );

		if ( dissolve.screen->width != width || dissolve.screen->height != height )
		{
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
	static const char	*maskName;

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

	if ( !dissolve.black )
	{
		// Eight by eight of opaque black. Its colour never reaches the screen -
		// these quads write depth and nothing else - but a texture with zero
		// alpha would be thrown away by a pipeline that ever gained an alpha
		// test, so it is opaque on purpose.
		static byte black[8 * 8 * 4];
		int i;

		for ( i = 0; i < 8 * 8; i++ ) {
			black[i * 4 + 0] = 0;
			black[i * 4 + 1] = 0;
			black[i * 4 + 2] = 0;
			black[i * 4 + 3] = 255;
		}

		dissolve.black = R_CreateImage( "*dissolveBlack", black, 8, 8,
			IMGFLAG_CLAMPTOEDGE | IMGFLAG_NOSCALE | IMGFLAG_NO_COMPRESSION, 0, 0 );
	}

	dissolve.type = bForceCircularExtroWipe
		? eDISSOLVE_CIRCULAR_IN
		: (Dissolve_e)Q_irand( 0, eDISSOLVE_RAND_LIMIT - 1 );

	switch ( dissolve.type )
	{
		case eDISSOLVE_CIRCULAR_IN:		maskName = "gfx/2d/iris_mono_rev";		break;
		case eDISSOLVE_CIRCULAR_OUT:	maskName = "gfx/2d/iris_mono";			break;
		default:						maskName = "textures/common/dissolve";	break;
	}

	dissolve.mask = R_FindImageFile( maskName,
		( dissolve.type == eDISSOLVE_CIRCULAR_IN || dissolve.type == eDISSOLVE_CIRCULAR_OUT )
			? IMGFLAG_CLAMPTOEDGE : IMGFLAG_NONE, 0 );

	if ( !dissolve.mask )
	{
		// No boundary picture, no wipe. Saying so once is better than a level
		// that appears to hang on a frozen screen.
		ri.Printf( PRINT_WARNING, "dissolve: %s not found, no screen wipe\n", maskName );
		R_KillDissolve();
		return qfalse;
	}

	R_DissolveBuildPipelines();

	dissolve.startTime = ri.Milliseconds();	// replaced on the first drawn frame, but must not be zero
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
