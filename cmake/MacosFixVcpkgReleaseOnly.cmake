# vcpkg exports Debug and Release CMake import configs, but macOS dev builds often
# install Release libraries only. Imported-target validation then fails because
# debug/lib artifacts are missing.
#
# Pass 1: symlink same-named release files into debug/lib.
# Pass 2: parse *debug*.cmake import paths and alias release names (e.g. libcurl-d
# -> libcurl) when debug-specific names are used.

if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT VCPKG_TARGET_TRIPLET)
    return()
endif()

set(_vcpkg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
set(_release_lib_dir "${_vcpkg_root}/lib")
set(_debug_lib_dir "${_vcpkg_root}/debug/lib")

if(NOT IS_DIRECTORY "${_release_lib_dir}")
    return()
endif()

file(MAKE_DIRECTORY "${_debug_lib_dir}")

function(_macos_vcpkg_alias_debug_lib release_path debug_path alias_count_var)
    if(EXISTS "${release_path}" AND NOT EXISTS "${debug_path}")
        file(CREATE_LINK "${release_path}" "${debug_path}" SYMBOLIC)
        math(EXPR _count "${${alias_count_var}} + 1")
        set(${alias_count_var} "${_count}" PARENT_SCOPE)
    endif()
endfunction()

set(_macos_vcpkg_alias_count 0)

file(GLOB _release_artifacts
    RELATIVE "${_release_lib_dir}"
    "${_release_lib_dir}/*"
)
foreach(_artifact IN LISTS _release_artifacts)
    if(_artifact STREQUAL "")
        continue()
    endif()
    _macos_vcpkg_alias_debug_lib(
        "${_release_lib_dir}/${_artifact}"
        "${_debug_lib_dir}/${_artifact}"
        _macos_vcpkg_alias_count
    )
endforeach()

file(GLOB_RECURSE _debug_cmake_files
    "${_vcpkg_root}/share/*debug*.cmake"
)
foreach(_cmake_file IN LISTS _debug_cmake_files)
    file(READ "${_cmake_file}" _cmake_content)
    string(REGEX MATCHALL "[^ \t\n\r\"']*debug/lib/[^ \t\n\r\"']+" _debug_import_paths "${_cmake_content}")
    foreach(_import_path IN LISTS _debug_import_paths)
        if(NOT _import_path MATCHES "/debug/lib/(.+)$")
            continue()
        endif()
        set(_debug_name "${CMAKE_MATCH_1}")
        set(_debug_path "${_debug_lib_dir}/${_debug_name}")
        if(EXISTS "${_debug_path}")
            continue()
        endif()

        set(_release_candidates "${_debug_name}")
        string(REPLACE "-d." "." _release_name "${_debug_name}")
        list(APPEND _release_candidates "${_release_name}")
        string(REPLACE "d.dylib" ".dylib" _release_name2 "${_debug_name}")
        list(APPEND _release_candidates "${_release_name2}")
        if(_debug_name MATCHES "^lib(.+)-d(\\..+)$")
            list(APPEND _release_candidates "lib${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
        endif()
        if(_debug_name MATCHES "^lib([a-zA-Z0-9_]+)d(\\..+)$")
            list(APPEND _release_candidates "lib${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
        endif()

        foreach(_candidate IN LISTS _release_candidates)
            if(_candidate STREQUAL "")
                continue()
            endif()
            set(_release_path "${_release_lib_dir}/${_candidate}")
            if(EXISTS "${_release_path}")
                _macos_vcpkg_alias_debug_lib("${_release_path}" "${_debug_path}" _macos_vcpkg_alias_count)
                break()
            endif()
        endforeach()
    endforeach()
endforeach()

if(_macos_vcpkg_alias_count GREATER 0)
    message(STATUS "macOS: created ${_macos_vcpkg_alias_count} vcpkg debug/lib aliases from release")
endif()
