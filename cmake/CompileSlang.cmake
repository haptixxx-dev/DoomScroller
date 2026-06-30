# ds_compile_slang(TARGET output_target
#     SOURCES   foo.slang bar.slang ...
#     STAGES    vertex fragment          # one per source, or "vertex;fragment" to compile both from each
#     TARGETS   spirv msl dxil          # output formats
#     OUT_DIR   ${CMAKE_BINARY_DIR}/shaders
# )
#
# Produces custom build rules that run slangc at build time.
# Output files are added as sources to output_target (an INTERFACE library).
# Caller links or copies from there.

function(ds_compile_slang)
    cmake_parse_arguments(ARG "" "OUT_DIR" "SOURCES;STAGES;TARGETS" ${ARGN})

    if(NOT SLANG_COMPILER)
        message(FATAL_ERROR "ds_compile_slang: SLANG_COMPILER not set. Include cmake/Slang.cmake first.")
    endif()

    set(_all_outputs "")

    foreach(_src ${ARG_SOURCES})
        get_filename_component(_base "${_src}" NAME_WE)
        get_filename_component(_abs  "${_src}" ABSOLUTE)

        foreach(_stage ${ARG_STAGES})
            foreach(_fmt ${ARG_TARGETS})
                # Extension per format
                if(_fmt STREQUAL "spirv")
                    set(_ext "spv")
                elseif(_fmt STREQUAL "metal")
                    set(_ext "msl")
                elseif(_fmt STREQUAL "dxil")
                    set(_ext "dxil")
                else()
                    set(_ext "${_fmt}")
                endif()

                set(_out "${ARG_OUT_DIR}/${_base}.${_stage}.${_ext}")
                list(APPEND _all_outputs "${_out}")

                # Entry point convention: vertMain / fragMain / compMain
                if(_stage STREQUAL "vertex")
                    set(_entry "vertMain")
                elseif(_stage STREQUAL "fragment")
                    set(_entry "fragMain")
                elseif(_stage STREQUAL "compute")
                    set(_entry "compMain")
                else()
                    set(_entry "${_stage}Main")
                endif()

                add_custom_command(
                    OUTPUT  "${_out}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_OUT_DIR}"
                    COMMAND "${SLANG_COMPILER}"
                            "${_abs}"
                            -entry "${_entry}"
                            -stage "${_stage}"
                            -target "${_fmt}"
                            -o "${_out}"
                    DEPENDS "${_abs}"
                    COMMENT "slangc [${_fmt}] ${_base}.${_stage}"
                    VERBATIM
                )
            endforeach()
        endforeach()
    endforeach()

    # Custom target that builds all shader outputs.
    # Sanitize path into valid CMake target name (colons/slashes illegal on Windows).
    # Include the stage list so multiple invocations sharing one OUT_DIR (e.g. a
    # vertex/fragment batch + a separate compute batch) produce distinct target
    # names instead of colliding.
    string(REPLACE ";" "_" _stage_tag "${ARG_STAGES}")
    string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" _tgt_name "shaders_${_stage_tag}_${ARG_OUT_DIR}")
    add_custom_target("${_tgt_name}" ALL DEPENDS ${_all_outputs})

    # Export output list and directory for callers
    set(DS_COMPILED_SHADERS "${_all_outputs}" PARENT_SCOPE)
    set(DS_SHADER_OUT_DIR   "${ARG_OUT_DIR}"  PARENT_SCOPE)
endfunction()
