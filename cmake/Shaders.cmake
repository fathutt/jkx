#============================================================================
# GLSL -> SPIR-V -> shaders.pak
#
# Replaces the Windows-only shaders/tools/compile_threaded.cpp and the .bat
# scripts that drove it. See tools/shadergen/shadergen.py and
# docs/CODING-STANDARDS.md section 9.3.
#
# The variant list is expanded at configure time from shaders/shaders.json, so
# adding a permutation is a manifest edit rather than a C++ edit. Each variant
# becomes one custom command with a DEPFILE, which is what lets a change to
# global.h or common/*.glsl rebuild exactly the shaders that include it and
# nothing else.
#============================================================================

find_package(Python3 REQUIRED COMPONENTS Interpreter)

find_program(GLSLC_EXECUTABLE
    NAMES glslc
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        "$ENV{VULKAN_SDK}/Bin"
    DOC "glslc from shaderc (ships with the Vulkan SDK)")

# Whether this configuration compiles shaders at all.
#
# On by default, and OFF is for a configuration that is COMPILED AND NEVER RUN.
# tools/ci/local.sh has one of those: the Debug stage exists to compile every
# #ifdef the Release build does not, and nothing ever launches it - the engine
# runs out of the Release tree and the sanitizer tree. Six hundred and four
# glslc invocations and a pak for a binary nobody starts.
#
# The slot table is still generated, because vk_shaders.cpp includes it and the
# C++ would not compile without it. It comes out of the manifest in a fraction
# of a second; it is the glslc pass and the packing that cost.
#
# Measured, one shader touched and three configurations rebuilt: ninety-five
# seconds with all three compiling, and four hundred and eighty-one glslc runs
# in each.
#
# WHAT WAS TRIED FIRST AND DOES NOT WORK, so that nobody spends the afternoon on
# it again: pointing several build trees at ONE shared SPIR-V directory. The
# modules really are config-independent - md5 on the same module out of three
# trees is identical - so it looks free. It is not. Each build tree has its own
# ninja graph and its own log, and ninja records the mtime of every output it
# produces; when another tree rewrites that file, ninja sees an output modified
# behind its back and rebuilds it. All three configurations compiled all four
# hundred and eighty-one modules anyway, measured, and the only thing the shared
# directory added was two builds able to race on one file.
set(JKX_BUILD_SHADERS ON CACHE BOOL
    "Compile GLSL to SPIR-V and pack it. OFF for a tree that is built and never run.")

function(jkx_add_shaders TARGET_NAME)
    set(SHADER_DIR   "${CMAKE_SOURCE_DIR}/code/rd-vulkan/shaders")
    set(MANIFEST     "${SHADER_DIR}/shaders.json")
    set(GLSL_DIR     "${SHADER_DIR}/glsl")
    set(SHADERGEN    "${CMAKE_SOURCE_DIR}/tools/shadergen/shadergen.py")
    set(SPV_DIR      "${CMAKE_CURRENT_BINARY_DIR}/spirv")
    set(PAK_FILE     "${JKX_GAME_DATA_DIR}/shaders.pak")
    set(PLAN_FILE    "${CMAKE_CURRENT_BINARY_DIR}/shader_variants.cmake")

    if(NOT GLSLC_EXECUTABLE AND JKX_BUILD_SHADERS)
        message(FATAL_ERROR
            "glslc not found. Install the Vulkan SDK, or the shaderc package on Linux, "
            "and re-run cmake. Set GLSLC_EXECUTABLE to override.")
    endif()

    # Expand the manifest now so the variant list is known to the generator.
    # Re-runs whenever the manifest changes.
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${SHADERGEN}"
                --manifest "${MANIFEST}" plan --out "${PLAN_FILE}"
        RESULT_VARIABLE plan_result
        OUTPUT_VARIABLE plan_output
        ERROR_VARIABLE  plan_output)
    if(NOT plan_result EQUAL 0)
        message(FATAL_ERROR "shadergen plan failed:\n${plan_output}")
    endif()
    message(STATUS "${plan_output}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MANIFEST}")

    # The slot table is generated too, and consumed by vk_shaders.cpp.
    set(SLOT_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/shader_slots.inl")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${SHADERGEN}"
                --manifest "${MANIFEST}" bind --out "${SLOT_FILE}"
        RESULT_VARIABLE bind_result
        OUTPUT_VARIABLE bind_output
        ERROR_VARIABLE  bind_output)
    if(NOT bind_result EQUAL 0)
        message(FATAL_ERROR "shadergen bind failed:\n${bind_output}")
    endif()
    message(STATUS "${bind_output}")
    set(JKX_SHADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated" PARENT_SCOPE)

    include("${PLAN_FILE}")

    if(NOT JKX_BUILD_SHADERS)
        add_custom_target(${TARGET_NAME})
        message(STATUS "Shaders: NOT compiled in this tree "
                       "(JKX_BUILD_SHADERS is off; the slot table is still generated)")
        return()
    endif()

    file(MAKE_DIRECTORY "${SPV_DIR}")

    set(SPV_OUTPUTS "")
    foreach(variant IN LISTS JKX_SHADER_VARIANTS)
        # name|source|stage|define;define;...
        string(REPLACE "|" ";" fields "${variant}")
        list(GET fields 0 v_name)
        list(GET fields 1 v_source)
        list(GET fields 2 v_stage)
        list(GET fields 3 v_defines)

        if(v_stage STREQUAL "vert")
            set(stage_arg "vertex")
        elseif(v_stage STREQUAL "frag")
            set(stage_arg "fragment")
        elseif(v_stage STREQUAL "geom")
            set(stage_arg "geometry")
        elseif(v_stage STREQUAL "comp")
            set(stage_arg "compute")
        else()
            message(FATAL_ERROR "unknown shader stage '${v_stage}' for ${v_name}")
        endif()

        set(define_args "")
        if(NOT v_defines STREQUAL "-")
            string(REPLACE "," ";" define_list "${v_defines}")
            foreach(d IN LISTS define_list)
                list(APPEND define_args "-D${d}")
            endforeach()
        endif()

        set(spv "${SPV_DIR}/${v_name}.spv")
        add_custom_command(
            OUTPUT  "${spv}"
            COMMAND "${GLSLC_EXECUTABLE}"
                    "--target-env=${JKX_SHADER_TARGET_ENV}"
                    "-fshader-stage=${stage_arg}"
                    -I "${GLSL_DIR}"
                    -O
                    -MD -MF "${spv}.d"
                    ${define_args}
                    -o "${spv}"
                    "${GLSL_DIR}/${v_source}"
            DEPENDS "${GLSL_DIR}/${v_source}"
            DEPFILE "${spv}.d"
            COMMENT ""
            VERBATIM)
        list(APPEND SPV_OUTPUTS "${spv}")
    endforeach()

    add_custom_command(
        OUTPUT  "${PAK_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/base"
        COMMAND "${Python3_EXECUTABLE}" "${SHADERGEN}"
                --manifest "${MANIFEST}" pack --spv-dir "${SPV_DIR}" --out "${PAK_FILE}"
        DEPENDS ${SPV_OUTPUTS} "${SHADERGEN}" "${MANIFEST}"
        COMMENT "Packing ${JKX_SHADER_COUNT} SPIR-V module(s) into shaders.pak"
        VERBATIM)

    add_custom_target(${TARGET_NAME} ALL DEPENDS "${PAK_FILE}")

    message(STATUS "Shaders: ${JKX_SHADER_COUNT} variant(s) via ${GLSLC_EXECUTABLE}")
endfunction()
