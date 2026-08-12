#version 450
#extension GL_GOOGLE_include_directive : enable

#include "global.h"

layout(set = VK_DESC_TEXTURE0, binding = 0) uniform samplerCube skybox;

layout(location = 0) in vec3 frag_dir;

layout(location = 0) out vec4 out_color;

// The whole point of the cubemap: one sample, by direction, and the hardware
// picks the face and filters across the edges between them. The six-quad path
// this replaces samples six separate textures each clamped to its own edge,
// which is where the seams come from.
//
// No normalise: a cube sample only cares about which way the vector points.
void main() {
	out_color = vec4( texture( skybox, frag_dir ).rgb, 1.0 );
}
