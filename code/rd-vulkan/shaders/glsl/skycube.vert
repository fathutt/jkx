#version 450
#extension GL_GOOGLE_include_directive : enable

#include "global.h"

// 68 bytes. Keep in sync with pushConst in vk_local.h.
layout(push_constant) uniform Transform {
	mat4 mvp;
	float renderMode;
};

layout(location = 0) in vec4 in_position;
layout(location = 5) in vec4 in_normal;

// Not a normal. The sky box has none worth having; the attribute carries the
// direction from the camera to the vertex, which is what the cube is sampled
// by, and it rides in the normal slot because that slot is already plumbed
// through the vertex binding machinery and a vec2 texture coordinate cannot
// hold three numbers.
layout(location = 0) out vec3 frag_dir;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	gl_Position = mvp * vec4(in_position.xyz, 1.0);
	frag_dir = in_normal.xyz;
}
