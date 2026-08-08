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

function(jkx_add_shaders TARGET_NAME)
    set(SHADER_DIR   "${CMAKE_SOURCE_DIR}/code/rd-vulkan/shaders")
    set(MANIFEST     "${SHADER_DIR}/shaders.json")
    set(GLSL_DIR     "${SHADER_DIR}/glsl")
    set(SHADERGEN    "${CMAKE_SOURCE_DIR}/tools/shadergen/shadergen.py")
    set(SPV_DIR      "${CMAKE_CURRENT_BINARY_DIR}/spirv")
    set(PAK_FILE     "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/base/shaders.pak")
    set(PLAN_FILE    "${CMAKE_CURRENT_BINARY_DIR}/shader_variants.cmake")

    if(NOT GLSLC_EXECUTABLE)
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

    include("${PLAN_FILE}")

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
