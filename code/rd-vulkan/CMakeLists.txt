#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
# Copyright (C) 2026 JKX contributors
#
# This file is part of the OpenJK source code.
#
# OpenJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================

# The Vulkan renderer, built in this tree against the single-player headers.
#
# This is phase 2's finish line and it does not compile yet - that is the point
# of it existing. The renderer arrived from a multiplayer fork and until now the
# only way to build it was inside a checkout of that fork
# (tools/devkit/renderer_build_harness.sh), which meant the remaining work was
# an estimate rather than a list. With this target the compiler produces the
# list, and it shrinks with every commit.
#
# BuildSPRdVulkan is OFF by default for exactly that reason: turning it on makes
# the build fail, on purpose. CI stays green; anyone working on the port runs
#
#     cmake -B build -DBuildSPRdVulkan=ON && cmake --build build --target rdsp-vulkan_x86_64 2>&1 | tail
#
# and reads what is left. Delete this comment when the target builds.

if(NOT InOpenJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InOpenJK)

if(NOT BuildSPRdVulkan)
	return()
endif()

set(SPRDVulkanIncludeDirectories
	${SPDir}
	${SharedDir}
	"${SPDir}/rd-vulkan"
	"${GSLIncludeDirectory}"
	"${CMAKE_SOURCE_DIR}/third_party/volk"
	"${CMAKE_SOURCE_DIR}/third_party/vma"
	${OpenJKLibDir}
	)

set(SPRDVulkanDefines ${SharedDefines} "RENDERER" "_JK2EXE" "VK_NO_PROTOTYPES")

set(SPRDVulkanFiles
	"${SPDir}/rd-vulkan/G2_gore_r2.cpp"
	"${SPDir}/rd-vulkan/G2_gore_r2.h"
	"${SPDir}/rd-vulkan/G2_misc.cpp"
	"${SPDir}/rd-vulkan/rg_graph.cpp"
	"${SPDir}/rd-vulkan/rg_graph.h"
	"${SPDir}/rd-vulkan/tr_allocator.h"
	"${SPDir}/rd-vulkan/tr_allocator.cpp"
	"${SPDir}/rd-vulkan/tr_backend.cpp"
	"${SPDir}/rd-vulkan/tr_bsp.cpp"
	"${SPDir}/rd-vulkan/tr_cache.h"
	"${SPDir}/rd-vulkan/tr_cache.cpp"
	"${SPDir}/rd-vulkan/tr_cmds.cpp"
	"${SPDir}/rd-vulkan/tr_curve.cpp"
	"${SPDir}/rd-vulkan/tr_decals.cpp"
	"${SPDir}/rd-vulkan/tr_dissolve.cpp"
	"${SPDir}/rd-vulkan/tr_ghoul2.cpp"
	"${SPDir}/rd-vulkan/tr_image.cpp"
	"${SPDir}/rd-vulkan/tr_init.cpp"
	"${SPDir}/rd-vulkan/tr_light.cpp"
	"${SPDir}/rd-vulkan/tr_local.h"
	"${SPDir}/rd-vulkan/tr_main.cpp"
	"${SPDir}/rd-vulkan/tr_marks.cpp"
	"${SPDir}/rd-vulkan/tr_mesh.cpp"
	"${SPDir}/rd-vulkan/tr_model.cpp"
	"${SPDir}/rd-vulkan/tr_quicksprite.cpp"
	"${SPDir}/rd-vulkan/tr_quicksprite.h"
	"${SPDir}/rd-vulkan/tr_scene.cpp"
	"${SPDir}/rd-vulkan/tr_shade.cpp"
	"${SPDir}/rd-vulkan/tr_shader.cpp"
	"${SPDir}/rd-vulkan/tr_shade_calc.cpp"
	"${SPDir}/rd-vulkan/tr_shadows.cpp"
	"${SPDir}/rd-vulkan/tr_skin.cpp"
	"${SPDir}/rd-vulkan/tr_sky.cpp"
	"${SPDir}/rd-vulkan/tr_sp_exports.cpp"
	"${SPDir}/rd-vulkan/tr_sp_exports.h"
	"${SPDir}/rd-vulkan/tr_subs.cpp"
	"${SPDir}/rd-vulkan/tr_surface.cpp"
	"${SPDir}/rd-vulkan/tr_surfacesprites.cpp"
	"${SPDir}/rd-vulkan/tr_terrain.cpp"
	"${SPDir}/rd-vulkan/tr_world.cpp"
	"${SPDir}/rd-vulkan/tr_WorldEffects.cpp"
	"${SPDir}/rd-vulkan/tr_WorldEffects.h"
	"${SPDir}/rd-vulkan/vk_allocator.cpp"
	"${SPDir}/rd-vulkan/vk_attachments.cpp"
	"${SPDir}/rd-vulkan/vk_bloom.cpp"
	"${SPDir}/rd-vulkan/vk_cmd.cpp"
	"${SPDir}/rd-vulkan/vk_crash.cpp"
	"${SPDir}/rd-vulkan/vk_cubemap.cpp"
	"${SPDir}/rd-vulkan/vk_debug.cpp"
	"${SPDir}/rd-vulkan/vk_dynamic_glow.cpp"
	"${SPDir}/rd-vulkan/vk_flares.cpp"
	"${SPDir}/rd-vulkan/vk_frame.cpp"
	"${SPDir}/rd-vulkan/vk_image.cpp"
	"${SPDir}/rd-vulkan/vk_image_process.cpp"
	"${SPDir}/rd-vulkan/vk_info.cpp"
	"${SPDir}/rd-vulkan/vk_init.cpp"
	"${SPDir}/rd-vulkan/vk_instance.cpp"
	"${SPDir}/rd-vulkan/vk_local.h"
	"${SPDir}/rd-vulkan/vk_mikktspace.cpp"
	"${SPDir}/rd-vulkan/vk_normalmap.cpp"
	"${SPDir}/rd-vulkan/vk_pipeline_cache.cpp"
	"${SPDir}/rd-vulkan/vk_pipelines.cpp"
	"${SPDir}/rd-vulkan/vk_shaders.cpp"
	"${SPDir}/rd-vulkan/vk_shade_geometry.cpp"
	"${SPDir}/rd-vulkan/vk_shader_pak.cpp"
	"${SPDir}/rd-vulkan/vk_shader_pak.h"
	"${SPDir}/rd-vulkan/vk_swapchain.cpp"
	"${SPDir}/rd-vulkan/vk_vbo.cpp"
	"${SPDir}/rd-vulkan/vk_vbo_surfacesprites.cpp"
	)
source_group("renderer" FILES ${SPRDVulkanFiles})

set(SPRDVulkanUtils
	"${SPDir}/rd-vulkan/utils/stb_dxt.h"
	"${SPDir}/rd-vulkan/utils/stb_image.h"
	"${SPDir}/rd-vulkan/utils/mikktspace/mikktspace.h"
	"${SPDir}/rd-vulkan/utils/mikktspace/mikktspace.c")
source_group("renderer\\utils" FILES ${SPRDVulkanUtils})
list(APPEND SPRDVulkanFiles ${SPRDVulkanUtils})

# volk owns every entry point, so it is compiled in rather than linked against.
list(APPEND SPRDVulkanFiles "${CMAKE_SOURCE_DIR}/third_party/volk/volk.c")

set(SPRDVulkanGhoul2Files
	"${SPDir}/ghoul2/G2.h"
	"${SPDir}/ghoul2/ghoul2_gore.h")
source_group("ghoul2" FILES ${SPRDVulkanGhoul2Files})
list(APPEND SPRDVulkanFiles ${SPRDVulkanGhoul2Files})

set(SPRDVulkanRdCommonFiles
	"${SPDir}/rd-common/mdx_format.h"
	"${SPDir}/rd-common/tr_common.h"
	"${SPDir}/rd-common/tr_font.cpp"
	"${SPDir}/rd-common/tr_font.h"
	"${SPDir}/rd-common/tr_image_load.cpp"
	"${SPDir}/rd-common/tr_image_jpg.cpp"
	"${SPDir}/rd-common/tr_image_tga.cpp"
	"${SPDir}/rd-common/tr_image_png.cpp"
	"${SPDir}/rd-common/tr_noise.cpp"
	"${SPDir}/rd-common/tr_public.h"
	"${SPDir}/rd-common/tr_types.h")
source_group("rd-common" FILES ${SPRDVulkanRdCommonFiles})
list(APPEND SPRDVulkanFiles ${SPRDVulkanRdCommonFiles})

set(SPRDVulkanCommonFiles
	"${SPDir}/qcommon/matcomp.cpp"
	"${SPDir}/qcommon/q_shared.cpp"
	${SharedCommonFiles})
source_group("common" FILES ${SPRDVulkanCommonFiles})
list(APPEND SPRDVulkanFiles ${SPRDVulkanCommonFiles})

# Image libraries, same transparent bundled-or-system treatment the other
# renderer gets. png before zlib: it uses it.
list(APPEND SPRDVulkanIncludeDirectories ${JPEG_INCLUDE_DIR} ${PNG_INCLUDE_DIRS}
	${ZLIB_INCLUDE_DIR} ${MINIZIP_INCLUDE_DIRS})
list(APPEND SPRDVulkanLibraries ${JPEG_LIBRARIES} ${PNG_LIBRARIES}
	${ZLIB_LIBRARIES} ${MINIZIP_LIBRARIES})

# Headers only. Every entry point is resolved through volk at runtime, so there
# is nothing to link; a machine with the SDK has them, a CI runner does not, and
# installing an SDK to obtain a directory of headers is not a trade worth making.
#
# The version is a floor, not a preference. The renderer names constants from
# VK_EXT_present_mode_fifo_latest_ready, which arrived in header 1.3.297, and a
# distribution that ships older headers - Ubuntu 24.04 ships 1.3.275 - would
# otherwise satisfy find_package and then fail to compile. Asking for the floor
# makes the fetch happen instead.
find_package(Vulkan 1.3.297 QUIET)
if(NOT TARGET Vulkan::Headers)
	include(FetchContent)
	FetchContent_Declare(VulkanHeaders
		GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
		GIT_TAG v1.4.309
		GIT_SHALLOW TRUE)
	FetchContent_MakeAvailable(VulkanHeaders)
endif()

# vk_shaders.cpp includes a generated slot table. The top-level script produces
# it as part of jkx_add_shaders, but that is gated on glslc being present and
# binding does not need glslc - only the manifest - so generate it here when it
# is missing rather than making this target depend on a shader compiler.
if(NOT JKX_SHADER_GENERATED_DIR)
	set(JKX_SHADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
	find_package(Python3 COMPONENTS Interpreter REQUIRED)
	execute_process(
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/shadergen/shadergen.py"
			--manifest "${SPDir}/rd-vulkan/shaders/shaders.json"
			bind --out "${JKX_SHADER_GENERATED_DIR}/shader_slots.inl"
		RESULT_VARIABLE bind_result
		OUTPUT_VARIABLE bind_output
		ERROR_VARIABLE  bind_output)
	if(NOT bind_result EQUAL 0)
		message(FATAL_ERROR "shadergen bind failed:\n${bind_output}")
	endif()
endif()
list(APPEND SPRDVulkanIncludeDirectories "${JKX_SHADER_GENERATED_DIR}")

# Phase 2.2: one binary. The renderer is a static library linked into the engine
# rather than a module loaded at runtime, which removes the whole class of defect
# that lives on a DLL boundary - two heaps, two copies of a struct layout, and an
# unload path that has to put everything back exactly as it found it.
#
# JKX_MONOLITH is an option and not a hard-coded choice only because rd-vanilla
# is still around as a reference this phase, and comparing the two renderers at
# runtime needs a loadable module. It goes when rd-vanilla does.
option(JKX_MONOLITH "Link the Vulkan renderer into the engine instead of loading it" ON)

if(JKX_MONOLITH)
	add_library(${SPRDVulkanRenderer} STATIC ${SPRDVulkanFiles})
	# The renderer has to know too: inside the engine it must stop defining the
	# engine's own symbols, which it could only get away with behind a module
	# boundary.
	list(APPEND SPRDVulkanDefines "JKX_MONOLITH_RENDERER=1")
else()
	add_library(${SPRDVulkanRenderer} SHARED ${SPRDVulkanFiles})
endif()

if(NOT MSVC)
	# remove "lib" prefix for .so/.dylib files
	set_target_properties(${SPRDVulkanRenderer} PROPERTIES PREFIX "")
endif()

set_target_properties(${SPRDVulkanRenderer} PROPERTIES
	COMPILE_DEFINITIONS "${SPRDVulkanDefines}"
	INCLUDE_DIRECTORIES "${SPRDVulkanIncludeDirectories}"
	PROJECT_LABEL "SP Vulkan Renderer")
set_property(TARGET ${SPRDVulkanRenderer} APPEND PROPERTY
	COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})
# The renderer's own diagnostics - the vk_log.log operation trace and, where the
# layers are installed, validation reporting - in an otherwise ordinary release
# build. A debug build is not a substitute: it changes the CRT and the struct
# layouts the gamecode interface depends on, and the result dies before it can
# write a line. This used to be baked into the source at sync time by the
# harness, which is exactly the kind of thing that silently does nothing when
# the sync step is skipped.
option(JKX_VK_TRACE "Compile the Vulkan renderer's operation trace into a release build" OFF)
if(JKX_VK_TRACE)
	target_compile_definitions(${SPRDVulkanRenderer} PRIVATE JKX_VK_TRACE=1)
endif()

target_compile_features(${SPRDVulkanRenderer} PRIVATE cxx_std_20)
target_link_libraries(${SPRDVulkanRenderer} ${SPRDVulkanLibraries} Vulkan::Headers)

# --no-undefined only means anything for something that links: a static library
# is an archive and has nothing to resolve against. Inside the engine the same
# check happens when the engine links.
if(NOT JKX_MONOLITH)
	jkx_no_undefined_symbols(${SPRDVulkanRenderer})
endif()

if(WIN32)
	# vk_crash.cpp walks the stack and resolves the frames through dbghelp. It
	# is the difference between a crash report and a column of addresses, and
	# the import library has to be named or the link fails on SymFromAddr and
	# friends. This lived in the build harness until the harness went; nothing
	# on Linux needs it, which is why nothing here noticed.
	target_link_libraries(${SPRDVulkanRenderer} dbghelp)
endif()

if(MSVC)
	# Symbols in the shipped release build. /DEBUG costs a PDB beside the DLL
	# and nothing at runtime; /OPT:REF and /OPT:ICF put back the folding that
	# /DEBUG otherwise turns off, so this is not a slower binary - it is the
	# same binary with a symbol file. The crash handler finds the PDB by looking
	# next to the module.
	#
	# /Z7 rather than /Zi: it puts the debug information in the object file
	# instead of a compiler-managed PDB, which is the same symbols to the linker
	# and the difference between a build a compiler cache can reuse and one it
	# cannot.
	target_compile_options(${SPRDVulkanRenderer} PRIVATE /Z7)
	# An archive is not linked, so link options on it do nothing. Inside the
	# engine the engine's own link options apply, and /Z7 above means the symbols
	# are in the objects and travel into it.
	if(NOT JKX_MONOLITH)
		target_link_options(${SPRDVulkanRenderer} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
	endif()
endif()

# Nothing to install when it is inside the engine.
if(NOT JKX_MONOLITH)
	if(WIN32)
		install(TARGETS ${SPRDVulkanRenderer}
			RUNTIME DESTINATION ${JKAInstallDir} COMPONENT ${JKASPClientComponent})
	else()
		install(TARGETS ${SPRDVulkanRenderer}
			LIBRARY DESTINATION ${JKAInstallDir} COMPONENT ${JKASPClientComponent})
	endif()
endif()
