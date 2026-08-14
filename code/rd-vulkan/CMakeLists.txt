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
# BuildRdVulkan is OFF by default for exactly that reason: turning it on makes
# the build fail, on purpose. CI stays green; anyone working on the port runs
#
#     cmake -B build -DBuildRdVulkan=ON && cmake --build build --target rdsp-vulkan_x86_64 2>&1 | tail
#
# and reads what is left. Delete this comment when the target builds.

if(NOT InOpenJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InOpenJK)

if(NOT BuildRdVulkan)
	return()
endif()

set(RDVulkanIncludeDirectories
	${CodeDir}
	${SharedDir}
	"${CodeDir}/rd-vulkan"
	"${GSLIncludeDirectory}"
	"${CMAKE_SOURCE_DIR}/third_party/volk"
	"${CMAKE_SOURCE_DIR}/third_party/vma"
	${ThirdPartyDir}
	)

set(RDVulkanDefines ${SharedDefines} "RENDERER" "JKX_ENGINE" "VK_NO_PROTOTYPES")

set(RDVulkanFiles
	"${CodeDir}/rd-vulkan/G2_gore_r2.cpp"
	"${CodeDir}/rd-vulkan/G2_gore_r2.h"
	"${CodeDir}/rd-vulkan/G2_misc.cpp"
	"${CodeDir}/rd-vulkan/rg_graph.cpp"
	"${CodeDir}/rd-vulkan/rg_graph.h"
	"${CodeDir}/rd-vulkan/tr_allocator.h"
	"${CodeDir}/rd-vulkan/tr_allocator.cpp"
	"${CodeDir}/rd-vulkan/tr_backend.cpp"
	"${CodeDir}/rd-vulkan/tr_bsp.cpp"
	"${CodeDir}/rd-vulkan/tr_cache.h"
	"${CodeDir}/rd-vulkan/tr_cache.cpp"
	"${CodeDir}/rd-vulkan/tr_cmds.cpp"
	"${CodeDir}/rd-vulkan/tr_curve.cpp"
	"${CodeDir}/rd-vulkan/tr_decals.cpp"
	"${CodeDir}/rd-vulkan/tr_dissolve.cpp"
	"${CodeDir}/rd-vulkan/tr_ghoul2.cpp"
	"${CodeDir}/rd-vulkan/tr_image.cpp"
	"${CodeDir}/rd-vulkan/tr_init.cpp"
	"${CodeDir}/rd-vulkan/tr_light.cpp"
	"${CodeDir}/rd-vulkan/tr_local.h"
	"${CodeDir}/rd-vulkan/tr_main.cpp"
	"${CodeDir}/rd-vulkan/tr_marks.cpp"
	"${CodeDir}/rd-vulkan/tr_mesh.cpp"
	"${CodeDir}/rd-vulkan/tr_model.cpp"
	"${CodeDir}/rd-vulkan/tr_quicksprite.cpp"
	"${CodeDir}/rd-vulkan/tr_quicksprite.h"
	"${CodeDir}/rd-vulkan/tr_scene.cpp"
	"${CodeDir}/rd-vulkan/tr_shade.cpp"
	"${CodeDir}/rd-vulkan/tr_shader.cpp"
	"${CodeDir}/rd-vulkan/tr_shade_calc.cpp"
	"${CodeDir}/rd-vulkan/tr_shadows.cpp"
	"${CodeDir}/rd-vulkan/tr_skin.cpp"
	"${CodeDir}/rd-vulkan/tr_sky.cpp"
	"${CodeDir}/rd-vulkan/tr_sp_exports.cpp"
	"${CodeDir}/rd-vulkan/tr_sp_exports.h"
	"${CodeDir}/rd-vulkan/tr_subs.cpp"
	"${CodeDir}/rd-vulkan/tr_surface.cpp"
	"${CodeDir}/rd-vulkan/tr_surfacesprites.cpp"
	"${CodeDir}/rd-vulkan/tr_terrain.cpp"
	"${CodeDir}/rd-vulkan/tr_world.cpp"
	"${CodeDir}/rd-vulkan/tr_WorldEffects.cpp"
	"${CodeDir}/rd-vulkan/tr_WorldEffects.h"
	"${CodeDir}/rd-vulkan/vk_allocator.cpp"
	"${CodeDir}/rd-vulkan/vk_attachments.cpp"
	"${CodeDir}/rd-vulkan/vk_bloom.cpp"
	"${CodeDir}/rd-vulkan/vk_cmd.cpp"
	"${CodeDir}/rd-vulkan/vk_crash.cpp"
	"${CodeDir}/rd-vulkan/vk_cubemap.cpp"
	"${CodeDir}/rd-vulkan/vk_debug.cpp"
	"${CodeDir}/rd-vulkan/vk_dynamic_glow.cpp"
	"${CodeDir}/rd-vulkan/vk_flares.cpp"
	"${CodeDir}/rd-vulkan/vk_frame.cpp"
	"${CodeDir}/rd-vulkan/vk_image.cpp"
	"${CodeDir}/rd-vulkan/vk_image_process.cpp"
	"${CodeDir}/rd-vulkan/vk_info.cpp"
	"${CodeDir}/rd-vulkan/vk_init.cpp"
	"${CodeDir}/rd-vulkan/vk_instance.cpp"
	"${CodeDir}/rd-vulkan/vk_local.h"
	"${CodeDir}/rd-vulkan/vk_mikktspace.cpp"
	"${CodeDir}/rd-vulkan/vk_normalmap.cpp"
	"${CodeDir}/rd-vulkan/vk_pipeline_cache.cpp"
	"${CodeDir}/rd-vulkan/vk_pipelines.cpp"
	"${CodeDir}/rd-vulkan/vk_shaders.cpp"
	"${CodeDir}/rd-vulkan/vk_shade_geometry.cpp"
	"${CodeDir}/rd-vulkan/vk_shader_pak.cpp"
	"${CodeDir}/rd-vulkan/vk_shader_pak.h"
	"${CodeDir}/rd-vulkan/vk_swapchain.cpp"
	"${CodeDir}/rd-vulkan/vk_vbo.cpp"
	"${CodeDir}/rd-vulkan/vk_vbo_surfacesprites.cpp"
	)
source_group("renderer" FILES ${RDVulkanFiles})

set(RDVulkanUtils
	"${CodeDir}/rd-vulkan/utils/stb_dxt.h"
	"${CodeDir}/rd-vulkan/utils/stb_image.h"
	"${CodeDir}/rd-vulkan/utils/mikktspace/mikktspace.h"
	"${CodeDir}/rd-vulkan/utils/mikktspace/mikktspace.c")
source_group("renderer\\utils" FILES ${RDVulkanUtils})
list(APPEND RDVulkanFiles ${RDVulkanUtils})

# volk owns every entry point, so it is compiled in rather than linked against.
list(APPEND RDVulkanFiles "${CMAKE_SOURCE_DIR}/third_party/volk/volk.c")

set(RDVulkanGhoul2Files
	"${CodeDir}/ghoul2/G2.h"
	"${CodeDir}/ghoul2/ghoul2_gore.h")
source_group("ghoul2" FILES ${RDVulkanGhoul2Files})
list(APPEND RDVulkanFiles ${RDVulkanGhoul2Files})

# rd-common was the code two renderers shared. There is one renderer, so the
# image loaders, the noise table and the font declarations have moved in here
# where they are read. What is left in rd-common is the pair of headers the
# engine itself includes - the interface between the two - plus the model
# format, which the gamecode also reads.
set(RDVulkanRdCommonFiles
	"${CodeDir}/rd-common/mdx_format.h"
	"${CodeDir}/rd-common/tr_public.h"
	"${CodeDir}/rd-common/tr_types.h"
	"${CodeDir}/rd-vulkan/tr_common.h"
	"${CodeDir}/rd-vulkan/tr_font.h"
	"${CodeDir}/rd-vulkan/tr_font_sdf.cpp"
	"${CodeDir}/rd-vulkan/tr_image_load.cpp"
	"${CodeDir}/rd-vulkan/tr_image_jpg.cpp"
	"${CodeDir}/rd-vulkan/tr_image_tga.cpp"
	"${CodeDir}/rd-vulkan/tr_image_tga_decode.cpp"
	"${CodeDir}/rd-vulkan/tr_image_tga_decode.h"
	"${CodeDir}/rd-vulkan/tr_image_png.cpp"
	"${CodeDir}/rd-vulkan/tr_noise.cpp")
source_group("rd-common" FILES ${RDVulkanRdCommonFiles})
list(APPEND RDVulkanFiles ${RDVulkanRdCommonFiles})

set(RDVulkanCommonFiles
	"${CodeDir}/qcommon/matcomp.cpp"
	"${CodeDir}/qcommon/q_shared.cpp"
	${SharedCommonFiles})
source_group("common" FILES ${RDVulkanCommonFiles})
list(APPEND RDVulkanFiles ${RDVulkanCommonFiles})

# Image libraries, same transparent bundled-or-system treatment the other
# renderer gets. png before zlib: it uses it.
list(APPEND RDVulkanIncludeDirectories ${JPEG_INCLUDE_DIR} ${PNG_INCLUDE_DIRS}
	${ZLIB_INCLUDE_DIR} ${MINIZIP_INCLUDE_DIRS})
list(APPEND RDVulkanLibraries ${JPEG_LIBRARIES} ${PNG_LIBRARIES}
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
			--manifest "${CodeDir}/rd-vulkan/shaders/shaders.json"
			bind --out "${JKX_SHADER_GENERATED_DIR}/shader_slots.inl"
		RESULT_VARIABLE bind_result
		OUTPUT_VARIABLE bind_output
		ERROR_VARIABLE  bind_output)
	if(NOT bind_result EQUAL 0)
		message(FATAL_ERROR "shadergen bind failed:\n${bind_output}")
	endif()
endif()
list(APPEND RDVulkanIncludeDirectories "${JKX_SHADER_GENERATED_DIR}")

# One binary. The renderer is a static library linked into the engine rather
# than a module loaded at runtime, which removes the whole class of defect that
# lives on a DLL boundary - two heaps, two copies of a struct layout, and an
# unload path that has to put everything back exactly as it found it.
#
# This used to be an option, because rd-vanilla was still around to compare
# against and comparing two renderers at runtime needs a loadable module.
# rd-vanilla is gone.

add_library(${RDVulkanRenderer} STATIC ${RDVulkanFiles})

set_target_properties(${RDVulkanRenderer} PROPERTIES
	COMPILE_DEFINITIONS "${RDVulkanDefines}"
	INCLUDE_DIRECTORIES "${RDVulkanIncludeDirectories}"
	PROJECT_LABEL "Vulkan Renderer")
set_property(TARGET ${RDVulkanRenderer} APPEND PROPERTY
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
	target_compile_definitions(${RDVulkanRenderer} PRIVATE JKX_VK_TRACE=1)
endif()

target_compile_features(${RDVulkanRenderer} PRIVATE cxx_std_20)
target_link_libraries(${RDVulkanRenderer} ${RDVulkanLibraries} Vulkan::Headers)

# --no-undefined would mean nothing here: a static library is an archive and has
# nothing to resolve against. The same check happens when the engine links.

if(WIN32)
	# vk_crash.cpp walks the stack and resolves the frames through dbghelp. It
	# is the difference between a crash report and a column of addresses, and
	# the import library has to be named or the link fails on SymFromAddr and
	# friends. This lived in the build harness until the harness went; nothing
	# on Linux needs it, which is why nothing here noticed.
	target_link_libraries(${RDVulkanRenderer} dbghelp)
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
	target_compile_options(${RDVulkanRenderer} PRIVATE /Z7)
	# An archive is not linked, so link options on it would do nothing. The
	# engine's own apply, and /Z7 above means the symbols are in the objects and
	# travel into it.
endif()

# Nothing to install: it is inside the engine.
