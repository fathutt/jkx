/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Vulkan handle types, spelled the way vulkan_core.h spells them.
//
// The window layer creates the surface and hands it to the renderer, so both
// the platform header and the renderer interface have to name a VkInstance and
// a VkSurfaceKHR. Neither should drag the Vulkan headers into the engine to do
// it: the multiplayer fork includes rd-vulkan/vulkan/vulkan.h from its
// sys_public.h, and after that every translation unit that wanted
// Sys_Milliseconds has the whole API in it.
//
// A translation unit that does include the real headers wins - VULKAN_H_ is
// defined by then and nothing below is repeated. If it includes them after us,
// the typedefs are identical and repeating a typedef is legal. Same trick
// SDL_vulkan.h uses, and for the same reason; the macros are spelled out here
// rather than reused so that nothing can collide with the macro definitions in
// vulkan_core.h.

#pragma once

#include <stdint.h>

#if !defined( VULKAN_H_ )

typedef struct VkInstance_T *VkInstance;

// Non-dispatchable handles are pointers on 64-bit targets and 64-bit integers
// everywhere else. This is the same set of predefined macros vulkan_core.h and
// SDL_vulkan.h test, and it has to stay in step with them: getting it wrong
// gives two different VkSurfaceKHR in one program.
#if defined( __LP64__ ) || defined( _WIN64 ) || defined( __x86_64__ ) || defined( _M_X64 ) || \
	defined( __ia64 ) || defined( _M_IA64 ) || defined( __aarch64__ ) || defined( __powerpc64__ )
typedef struct VkSurfaceKHR_T *VkSurfaceKHR;
#else
typedef uint64_t VkSurfaceKHR;
#endif

#endif // !VULKAN_H_
