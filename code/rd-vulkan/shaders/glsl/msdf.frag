#version 450

// Text, from a signed distance field.
//
// The atlas does not store how much ink covers a texel; it stores how far that
// texel is from the glyph's outline, negative inside and positive out, scaled
// so that the range the field was generated over maps onto nought to one. The
// shape is therefore wherever the field crosses one half, and it crosses it
// just as cleanly when the texture is magnified eight times as when it is
// drawn at its own size. That is the whole point: one atlas, every size.
//
// Three channels rather than one, because a single field rounds corners. A
// corner is exactly the place two edges are equidistant, and one number cannot
// say "this edge" and "that edge" at once. Multi-channel gives each edge a
// pair of channels, so at a corner two channels agree and one disagrees, and
// the median follows the corner. A field built from a bitmap has the one
// channel copied into three; the median of three equal numbers is that number,
// so this shader reads both without knowing which it has.

#include "global.h"

// Set 1, like every other diffuse texture in this renderer. Declaring it at set
// zero - which is where a standalone shader's first set naturally lands, and
// where the uniform block actually lives - compiles, links, creates a pipeline,
// and then samples a descriptor that was never written. On lavapipe that is a
// null dereference inside the JIT-compiled fragment shader, several frames
// after anything to do with fonts.
layout(set = VK_DESC_TEXTURE0, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) centroid in vec4 frag_color;
layout(location = 1) centroid in vec2 frag_tex_coord0;

layout(location = 0) out vec4 out_color;

// Texels of field either side of the outline, as the generator was asked for.
// It has to match what the atlas was built with or the antialiasing comes out
// the wrong width - too small and the edges go hard, too large and the text
// goes soft.
//
// This is a constant here rather than a specialization constant because the
// fragment specialization arrays in vk_pipelines.cpp are sized exactly and
// their tail is chopped off by count when surface sprites are off; appending
// to them means touching that accounting for a number that is the same for
// every font we ship. The font loader reads the range out of the .jkxfont
// header and refuses an atlas that disagrees with this, so the two cannot
// drift apart quietly - see MSDF_PIXEL_RANGE in tr_font_sdf.cpp.
const float msdf_range = 4.0;

float median( float r, float g, float b )
{
	return max( min( r, g ), min( max( r, g ), b ) );
}

void main()
{
	vec3 field = texture( u_atlas, frag_tex_coord0 ).rgb;
	float signedDistance = median( field.r, field.g, field.b ) - 0.5;

	// How many screen pixels one texel of the atlas covers here. fwidth is the
	// derivative of the texture coordinate across the quad, so its reciprocal
	// is the size of the quad in texels per pixel, and that is the only thing
	// that has to be known to antialias at any size, any rotation and any
	// perspective without being told what size the text was drawn at.
	vec2 unitRange = vec2( msdf_range ) / vec2( textureSize( u_atlas, 0 ) );
	vec2 screenTexSize = vec2( 1.0 ) / fwidth( frag_tex_coord0 );
	float screenPxRange = max( 0.5 * dot( unitRange, screenTexSize ), 1.0 );

	// Below one pixel of range there is nothing left to blend across and the
	// clamp above turns this into a hard threshold, which is the honest
	// outcome: text too small for its atlas aliases rather than dissolving.
	float alpha = clamp( signedDistance * screenPxRange + 0.5, 0.0, 1.0 );

	out_color = vec4( frag_color.rgb, frag_color.a * alpha );
}
