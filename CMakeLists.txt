#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
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

# The <min>...<policy_max> syntax is needed to remain compatible with 3.1, but also newer versions that dropped 3.1 compatibility
# More info here: https://cmake.org/cmake/help/latest/command/cmake_minimum_required.html
cmake_minimum_required(VERSION 3.1...3.31)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")

set(InOpenJK ON)

set(ProjectName "JKX" CACHE STRING "Project Name")
project(${ProjectName})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Where the generated game data - base/shaders.pak, base/fonts - has to land so
# that it is beside the executable and not merely near it.
#
# Visual Studio is a multi-config generator: it puts binaries in a per-config
# subdirectory and CMAKE_RUNTIME_OUTPUT_DIRECTORY names the parent. A custom
# command writing to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/base therefore puts the
# data one directory above the exe, where the engine does not look. On Ninja and
# Makefiles there is no subdirectory and the two are the same, which is why this
# has never been noticed: nothing has run the Windows build.
if(CMAKE_CONFIGURATION_TYPES)
    set(JKX_GAME_DATA_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/base")
else()
    set(JKX_GAME_DATA_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/base")
endif()

#=============================================================================
#
# JKX build policy
#
#   Rules live in docs/CODING-STANDARDS.md. Everything enforceable is enforced
#   here or in tools/ci/. Do not weaken these without updating that document.
#
#=============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(JKX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(JKX_ENABLE_ASAN        "Build with AddressSanitizer" OFF)
option(JKX_ENABLE_UBSAN       "Build with UndefinedBehaviorSanitizer" OFF)
option(JKX_ENABLE_TSAN        "Build with ThreadSanitizer" OFF)
# Vulkan renderer is imported but not yet buildable: the shader toolchain
# (phase 1) must land first. Generated SPIR-V is deliberately not in git.
option(JKX_BUILD_VULKAN_RENDERER "Build the Vulkan renderer (needs shader toolchain)" OFF)

if(MSVC)
    add_compile_options(/utf-8 /permissive- /W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
    if(JKX_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
    add_compile_options(-Wall -Wextra)
    # Legacy noise we accept for now; each entry is a debt item, not a policy.
    add_compile_options(
        -Wno-unused-parameter        # pervasive in Raven/id code
        -Wno-missing-field-initializers
        -Wno-invalid-offsetof
        -Wno-comment)
    # Not debt: MSVC makes this an error under /permissive- (/Zc:strictStrings),
    # so silencing it on GCC only meant Windows found it first, every time, on
    # CI. Same rule on both compilers or the two do not agree on what compiles.
    add_compile_options(-Werror=write-strings)
    if(JKX_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

# ...except in the gamecode, which holds 1243 of the 1252 occurrences: every
# ent->classname = "func_door" in twenty years of Raven logic. Making those
# const means changing gEntity_t and everything that touches it - a refactor of
# game logic this project does not otherwise intend to touch.
#
# So the rule holds where the code is ours - engine, renderers, shared - and is
# suspended per target where it is not. A ratchet, like the layering baseline:
# call sites get fixed and targets leave this list, they never join it quietly.
function(jkx_legacy_string_literals target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /Zc:strictStrings-)
    else()
        target_compile_options(${target} PRIVATE -Wno-write-strings)
    endif()
endfunction()

# Make a shared module link like a Windows DLL: refuse to produce one with
# symbols nothing defines.
#
# ELF is happy to link an .so full of undefined symbols and only fail when the
# process tries to call one. That is how a function whose body had been
# overwritten under a neighbour's name, and a definition left inside an #if 0,
# both survived every Linux build in this project and surfaced a week later as
# unresolved externals on the Windows packaging job - after the change they
# belonged to had already been merged. The link is the right place to find out.
#
# Not applied when a sanitizer is on: the runtime's symbols are resolved from
# the executable at load time, so a sanitized module is undefined by design.
function(jkx_no_undefined_symbols target)
    if(MSVC)
        return() # already the default, and there is no equivalent flag
    endif()
    if(UseAddressSanitizer OR UseUndefinedSanitizer OR UseThreadSanitizer
       OR JKX_ENABLE_ASAN OR JKX_ENABLE_UBSAN OR JKX_ENABLE_TSAN)
        return()
    endif()
    if(APPLE)
        target_link_options(${target} PRIVATE "LINKER:-undefined,error")
    else()
        target_link_options(${target} PRIVATE "LINKER:--no-undefined")
    endif()
endfunction()

if(JKX_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(JKX_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all)
    add_link_options(-fsanitize=undefined)
endif()
if(JKX_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()


#=============================================================================
#
# Customizable options
#
#=============================================================================
option(BuildPortableVersion "Build portable version (does not read or write files from your user/home directory" OFF)


option(BuildSPEngine "Whether to create projects for the SP engine (openjk_sp.exe)" ON)
option(BuildSPGame "Whether to create projects for the SP gamecode (jagamex86.dll)" ON)
# The one renderer. rd-vanilla was kept through phase 2 as something to compare
# against; it is gone, and with it the module boundary that existed only so two
# renderers could be swapped at runtime.
option(BuildSPRdVulkan "Whether to create the SP Vulkan renderer project" ON)

option(BuildJK2SPEngine "Build the JK2 SP engine" ON)
option(BuildJK2SPGame "Build the JK2 SP gamecode" ON)
option(BuildJK2SPRdVanilla "Build the JK2 SP reference renderer" ON)

option(BuildTests "Whether to build automatic unit tests (requires Boost)" OFF)
option(UseAddressSanitizer "Whether to enable runtime address sanitizer" OFF)
option(UseUndefinedSanitizer "Whether to enable runtime Undefined Behavior sanitizer" OFF)

include(CMakeDependentOption)
cmake_dependent_option(BuildSymbolServer "Build WIP Windows Symbol Server (experimental and unused)" OFF "NOT WIN32 OR NOT MSVC" OFF)

# Configure the use of bundled libraries.  By default, we assume the user is on
# a platform that does not require any bundling.
#
# Note that we always use the bundled copy of minizip, since it is modified to
# use Z_Malloc.

set(UseInternalOpenALDefault OFF)
set(UseInternalZlibDefault   OFF)
set(UseInternalPNGDefault    OFF)
set(UseInternalJPEGDefault   OFF)
set(UseInternalSDL2Default   OFF)

if(UseInternalLibs OR WIN32)
  set(UseInternalOpenALDefault ON)
  set(UseInternalZlibDefault   ON)
  set(UseInternalPNGDefault    ON)
  set(UseInternalJPEGDefault   ON)
  set(UseInternalSDL2Default   ON)
endif()

if(APPLE)
  set(UseInternalJPEGDefault ON)
  set(UseInternalPNGDefault  ON)
endif()

# lib/SDL2 was a checked-in Windows SDK - headers, import libraries and DLLs -
# and it went out with the rest of the committed binaries in phase 0. Defaulting
# to "bundled" on Windows therefore points the linker at files that are not
# there, which is how the Windows CI job failed on a missing SDL2main.lib. Track
# what is actually present instead of what the platform used to imply.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/SDL2/include/SDL.h")
  set(UseInternalSDL2Default OFF)
endif()

option(UseInternalOpenAL "If set, use bundled OpenAL."  ${UseInternalOpenALDefault})
option(UseInternalZlib   "If set, use bundled zlib."    ${UseInternalZlibDefault})
option(UseInternalPNG    "If set, use bundled libpng."  ${UseInternalPNGDefault})
option(UseInternalJPEG   "If set, use bundled libjpeg." ${UseInternalJPEGDefault})
option(UseInternalSDL2   "If set, use bundled SDL2."    ${UseInternalSDL2Default})

# This option won't appear on non-Apple platforms.
if(APPLE)
  option(MakeApplicationBundles "Whether to build .app application bundles for engines built" ON)
endif()

#=============================================================================
#
# Custom CMake Modules needed
#
#=============================================================================
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/cmake/Modules")
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/cmake")

if(CMAKE_EXPORT_COMPILE_COMMANDS)
	add_custom_target(
		copy-compile-commands ALL
		${CMAKE_COMMAND} -E copy_if_different
		${CMAKE_BINARY_DIR}/compile_commands.json
		${CMAKE_CURRENT_LIST_DIR})
endif()

#=============================================================================
#
# Architecture/OS defines
#
#=============================================================================
# ${Architecture} must match ARCH_STRING in q_platform.h,
# and is used in DLL names (jagamex86.dll, jagamex86.dylib, jagamei386.so).
if(WIN32)
	set(X86 ON)
	if(CMAKE_SIZEOF_VOID_P MATCHES "8")
		set(Architecture "x86_64")
		set(WIN64 TRUE)
	else()
		set(Architecture "x86")
		set(WIN64 FALSE)
	endif()
else()
	set(X86 OFF)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
		set(Architecture "arm64")
		add_definitions(-DPNG_ARM_NEON_OPT=0)
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
		set(Architecture "arm")
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^i.86$")
		set(X86 ON)
		if(APPLE)
			set(Architecture "x86")
		else()
			# e.g. Linux
			set(Architecture "i386")
		endif()
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^x86.64$")
		set(X86 ON)
		set(Architecture "x86_64")
	elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "powerpc")
		set(Architecture "ppc")
	elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "powerpc64")
		set(Architecture "ppc64")
	else()
		set(Architecture "${CMAKE_SYSTEM_PROCESSOR}")
	endif()
endif()

message(STATUS "Architecture is ${Architecture}")

if(WIN32 AND CMAKE_VERSION VERSION_LESS "3.4")
	message(WARNING "Building on Windows platform with CMake version less than 3.4 is deprecated. Manifest file will fail to be included.")
endif()

#=============================================================================
#
# Binary names
#
#=============================================================================
set(SPEngine "jkx_ja.${Architecture}")
set(SPGame "jagame${Architecture}")
set(SPRDVulkanRenderer "rdsp-vulkan_${Architecture}")
set(MPEngine "openjk.${Architecture}")
set(MPDed "openjkded.${Architecture}")
set(MPGame "jampgame${Architecture}")
set(MPCGame "cgame${Architecture}")
set(MPUI "ui${Architecture}")
set(MPRend2 "rd-rend2_${Architecture}")
set(JK2SPEngine "jkx_jo.${Architecture}")
set(JK2SPGame "jospgame${Architecture}")
set(AssetsPk3 "openjk-${Architecture}.pk3")
# Library names
set(MPBotLib "botlib")
set(SharedLib "shared")

#=============================================================================
#
# Paths
#
#=============================================================================
set(SPDir "${CMAKE_SOURCE_DIR}/code")
set(JK2SPDir "${CMAKE_SOURCE_DIR}/codeJK2")
set(SharedDir ${CMAKE_SOURCE_DIR}/shared)
set(OpenJKLibDir "${CMAKE_SOURCE_DIR}/lib")
set(GSLIncludeDirectory "${OpenJKLibDir}/gsl-lite/include")

include(InstallConfig)

#=============================================================================
#
# Compiler definitions/flags
#
#=============================================================================

# Operating system settings
if(WIN64)
	set(SharedDefines ${SharedDefines} "WIN64")
endif()

if (APPLE)
	set(SharedDefines ${SharedDefines} "MACOS_X")
endif()

if (NOT WIN32 AND NOT APPLE)
	set(SharedDefines ${SharedDefines} "ARCH_STRING=\"${Architecture}\"")
endif()

if(CMAKE_SYSTEM_NAME MATCHES "BSD")
  add_definitions(-DIOAPI_NO_64)
endif()

# Compiler settings
if(MSVC)
	set(SharedDefines ${SharedDefines} "NOMINMAX")
	set(SharedDefines ${SharedDefines} "_CRT_SECURE_NO_WARNINGS")
	set(SharedDefines ${SharedDefines} "_SCL_SECURE_NO_WARNINGS")
	set(SharedDefines ${SharedDefines} "_CRT_NONSTDC_NO_DEPRECATE")

	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /arch:SSE2")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:SSE2")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

	# Release keeps its symbols. Without this there is no .pdb, and the crash
	# handler in rd-vulkan/vk_crash.cpp - which already asks dbghelp for a
	# function and a line - can only find the nearest exported symbol. The
	# renderer is linked into the engine now and exports exactly one thing, so
	# every frame in every report came out as "GetRefAPI+0x...", which is a
	# number, not an answer:
	#
	#     0  0x00007ff6bbe0f699  jkx_ja.x86_64+0x9f699  GetRefAPI+0x4fc9
	#
	# Two halves. The compiler has to emit the information, and the linker has
	# to write the file.
	#
	# The compiler half is not a flag any more. CMP0141 - NEW here, because
	# cmake_minimum_required names a policy max above 3.25 - moves the choice
	# out of CMAKE_<LANG>_FLAGS_<CONFIG> and into this variable, and its
	# default is debug information for Debug and RelWithDebInfo only. Adding
	# /Zi by hand alongside it is how you get /Z7 and /Zi on one command line,
	# which is warning D9025 and whichever the compiler saw last. Embedded is
	# /Z7: the information goes into the object files, which is also what lets
	# sccache cache them at all - a .pdb is written by the compiler out of band
	# and a cache has nothing to replay.
	#
	# /DEBUG then makes the linker collect that into one .pdb per binary. It
	# also flips the /OPT defaults to NOREF and NOICF, which would change the
	# code, so both are asked for back explicitly: same binary, plus a file
	# beside it that turns an address into a line.
	if(POLICY CMP0141)
		cmake_policy(SET CMP0141 NEW)
	endif()
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
	foreach(kind EXE SHARED MODULE)
		set(CMAKE_${kind}_LINKER_FLAGS_RELEASE
			"${CMAKE_${kind}_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
	endforeach()

	# We don't try to control symbol visibility under MSVC.
	set(OPENJK_VISIBILITY_FLAGS "")
elseif (("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU") OR ("${CMAKE_C_COMPILER_ID}" MATCHES "Clang"))
	# I hope this doesn't come back to bite me in the butt later on.
	# Realistically though, can the C and CXX compilers be different?

	# Visibility can't be set project-wide -- it needs to be specified on a
	# per-target basis.  This is primarily due to the bundled copy of ZLib.
	# ZLib explicitly declares symbols hidden, rather than defaulting to hidden.
	#
	# Note that -fvisibility=hidden is stronger than -fvisibility-inlines-hidden.
	set(OPENJK_VISIBILITY_FLAGS "-fvisibility=hidden")

	# removes the -rdynamic flag at linking (which causes crashes for some reason)
	set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
	set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

	if(UseAddressSanitizer)
		# also raise stack size drastically (to 64MiB), since the sanitizer adds overhead to stack frames
		add_compile_options(-fsanitize=address)
		add_link_options(-fsanitize=address -z stack-size=4000000)
	endif()
	if(UseUndefinedSanitizer)
		add_compile_options(-fsanitize=undefined)
		add_link_options(-fsanitize=undefined)
	endif()

	# additional flags for debug configuration
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb")

	if (X86)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
	endif()

	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")


	if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-comment")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsigned-char")
		if (X86)
			# "x86 vm will crash without -mstackrealign since MMX
			# instructions will be used no matter what and they
			# corrupt the frame pointer in VM calls"
			# -ioquake3 Makefile
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mstackrealign")
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpmath=sse")
		endif()

		if(WIN32)
			# Link libgcc and libstdc++ statically
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libgcc")
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc")
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++")
		endif()
	elseif("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-comment")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsigned-char")
	endif()

	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-offsetof")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-write-strings")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsigned-char")
		if (X86)
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mstackrealign")
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpmath=sse")
		endif()
	elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-write-strings")
		#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-writable-strings")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-comment")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-offsetof")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsigned-char")
	endif()
else()
	message(ERROR "Unsupported compiler")
endif()

set(SharedDefines ${SharedDefines} "$<$<CONFIG:Debug>:_DEBUG>")
set(SharedDefines ${SharedDefines} "$<$<NOT:$<CONFIG:Debug>>:FINAL_BUILD>")

if(BuildPortableVersion)
	set(BUILD_PORTABLE ON)
endif()

#=============================================================================
#
# Generate version file
#
#=============================================================================

# Reproducible builds
# https://reproducible-builds.org/specs/source-date-epoch/
if ("$ENV{SOURCE_DATE_EPOCH}" STREQUAL "")
	message(STATUS "SOURCE_DATE_EPOCH is not set: SOURCE_DATE will be set to the compile-time date")
else()
	execute_process(
		COMMAND "date" "--date=@$ENV{SOURCE_DATE_EPOCH}" "+%b %_d %Y"
		OUTPUT_VARIABLE SOURCE_DATE
		ERROR_QUIET
		OUTPUT_STRIP_TRAILING_WHITESPACE)
	message(STATUS "SOURCE_DATE_EPOCH is set ($ENV{SOURCE_DATE_EPOCH}): SOURCE_DATE set to \"${SOURCE_DATE}\"")
endif()

# Current Git tag/version
# ideally we would separate the hash suffix into its own variable, but sed is not available everywhere
execute_process(
	COMMAND "git" "describe" "--tag"
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	OUTPUT_VARIABLE GIT_TAG
	RESULT_VARIABLE GIT_TAG_ERROR
	OUTPUT_STRIP_TRAILING_WHITESPACE)
if(GIT_TAG_ERROR AND NOT GIT_TAG_ERROR EQUAL 0)
	message(WARNING "Unable to determine Git tag")
	set(GIT_TAG vUNKNOWN)
endif()
message(STATUS "Git tag is ${GIT_TAG}")

configure_file(shared/qcommon/q_version.h.in shared/qcommon/q_version.h @ONLY)

#=============================================================================

# Files shared across all projects
#
#=============================================================================
set(SharedCommonFiles
	"${SharedDir}/qcommon/q_color.h"
	"${SharedDir}/qcommon/q_color.c"
	"${SharedDir}/qcommon/q_math.h"
	"${SharedDir}/qcommon/q_math.c"
	"${SharedDir}/qcommon/q_string.h"
	"${SharedDir}/qcommon/q_string.c"
	"${SharedDir}/qcommon/q_platform.h"
	"${SharedDir}/qcommon/channels.h"
	"${SharedDir}/qcommon/statindex.h"
	"${SharedDir}/qcommon/surfaceflags.h"
	"${SharedDir}/qcommon/persindex.h"
	"${SharedDir}/qcommon/invindex.h"
	"${CMAKE_BINARY_DIR}/shared/qcommon/q_version.h"
	)
set(SharedCommonSafeFiles
	"${SharedDir}/qcommon/safe/gsl.h"
	"${SharedDir}/qcommon/safe/string.cpp"
	"${SharedDir}/qcommon/safe/string.h"
	"${SharedDir}/qcommon/safe/sscanf.h"
	"${SharedDir}/qcommon/safe/limited_vector.h"
	)

#=============================================================================
#
# Adding all of the projects
#
#=============================================================================
if(UseInternalJPEG)
  add_subdirectory(lib/jpeg-9a)
else()
  find_package(JPEG REQUIRED)
endif()

if(UseInternalZlib)
  add_subdirectory(lib/zlib)
else()
  find_package(ZLIB REQUIRED)
endif()

if(UseInternalPNG)
  add_subdirectory(lib/libpng)
else()
  find_package(PNG REQUIRED)
endif()

# Shader toolchain: GLSL -> SPIR-V -> shaders.pak. Runs regardless of whether
# the Vulkan renderer target itself is enabled, so shader edits are validated
# even while the renderer is being ported (phase 2).
#
# Defaults to whether glslc is actually installed. It is not a build dependency
# of the engine - the renderer that consumes the pak is not built here yet - so
# a missing shader compiler must not stop someone building the game, and did:
# every Linux CI job failed at configure time on a machine without it. Asking
# for shaders explicitly on a machine without glslc is still an error.
find_program(GLSLC_EXECUTABLE
    NAMES glslc
    HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin"
    DOC "glslc from shaderc (ships with the Vulkan SDK)")

if(GLSLC_EXECUTABLE OR JKX_BUILD_VULKAN_RENDERER)
    set(JKX_SHADERS_DEFAULT ON)
else()
    set(JKX_SHADERS_DEFAULT OFF)
endif()

option(JKX_BUILD_SHADERS "Compile GLSL to shaders.pak" ${JKX_SHADERS_DEFAULT})
if(JKX_BUILD_SHADERS)
    include(Shaders)
    jkx_add_shaders(shaders)
else()
    message(STATUS
        "Shaders: skipped, glslc not found. The engine builds without it; install "
        "the shaderc package (or the Vulkan SDK) to build base/shaders.pak.")
endif()

# The generated font atlases, copied into base/fonts. See cmake/Fonts.cmake for
# why they are checked in rather than built here.
include(Fonts)
jkx_add_fonts(fonts)

# Always use bundled minizip (sets MINIZIP_{LIBRARIES,INCLUDE_DIR})
add_subdirectory(lib/minizip)

# Add projects
add_subdirectory(${SPDir})
if(BuildJK2SPGame)
	add_subdirectory("${JK2SPDir}/game")
endif()


if(BuildSymbolServer)
	add_subdirectory("tools/WinSymbol")
endif()

if(BuildTests)
	enable_testing()
	add_subdirectory("tests")
endif()
