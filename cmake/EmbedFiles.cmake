# Embeds text files (shaders) into a generated C++ header at build time.
#
#   gridfin_embed_files(<target> NAMESPACE <ns> HEADER <name.hpp> FILES <files...>)
#
# Every file becomes `inline constexpr std::string_view <ns>::<stem>` in
# `<binary dir>/generated/<name.hpp>`. Editing a file re-generates the header.

set(_GRIDFIN_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/EmbedFilesScript.cmake")

function(gridfin_embed_files target)
    cmake_parse_arguments(ARG "" "NAMESPACE;HEADER" "FILES" ${ARGN})
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(out_file "${out_dir}/${ARG_HEADER}")

    set(abs_files "")
    foreach(file IN LISTS ARG_FILES)
        cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE abs)
        list(APPEND abs_files "${abs}")
    endforeach()

    add_custom_command(
        OUTPUT "${out_file}"
        COMMAND "${CMAKE_COMMAND}"
            "-DOUTPUT=${out_file}"
            "-DNAMESPACE=${ARG_NAMESPACE}"
            "-DFILES=${abs_files}"
            -P "${_GRIDFIN_EMBED_SCRIPT}"
        DEPENDS ${abs_files} "${_GRIDFIN_EMBED_SCRIPT}"
        COMMENT "Embedding ${ARG_HEADER}"
        VERBATIM)

    target_sources(${target} PRIVATE "${out_file}" ${abs_files})
    target_include_directories(${target} PRIVATE "${out_dir}")
    source_group("Shaders" FILES ${abs_files})
endfunction()
