# Installed by CMake install(SCRIPT) on APPLE — copies vcpkg runtime dylibs next to PoB binaries.
# CMAKE_INSTALL_PREFIX and paths below are substituted at configure time.

set(_sg_lib_dir "@MACOS_BUNDLE_LIB_DIR@")
set(_install_prefix "${CMAKE_INSTALL_PREFIX}")
set(_built_libs
    "@MACOS_BUNDLE_SG_DYLIB@"
    "@MACOS_BUNDLE_LCURL@"
    "@MACOS_BUNDLE_SOCKET@"
    "@MACOS_BUNDLE_LZIP@"
    "@MACOS_BUNDLE_UTF8@"
)

file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES ${_built_libs}
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    DIRECTORIES "${_sg_lib_dir}"
    PRE_INCLUDE_REGEXES "^${_sg_lib_dir}"
    POST_EXCLUDE_REGEXES
        "^/System/"
        "^/usr/lib/"
        "^/usr/libexec/"
        "^@MACOS_BUNDLE_LIB_DIR@/libSystem"
)

if (_unresolved)
    message(WARNING "Unresolved macOS runtime dependencies (may be OK if system libs): ${_unresolved}")
endif ()

foreach (_dep IN LISTS _resolved)
    get_filename_component(_dep_name "${_dep}" NAME)
    set(_dest "${_install_prefix}/${_dep_name}")
    if (NOT EXISTS "${_dest}")
        file(INSTALL "${_dep}" DESTINATION "${_install_prefix}" FOLLOW_SYMLINK_CHAIN)
        message(STATUS "Bundled runtime: ${_dep_name}")
    endif ()
endforeach ()

# GLFW's EGL loader looks for libEGL.dylib / libGLESv2.dylib; vcpkg ANGLE uses liblib*_angle.dylib.
foreach (_pair IN ITEMS
    "libEGL.dylib;liblibEGL_angle.dylib"
    "libGLESv2.dylib;liblibGLESv2_angle.dylib"
)
    list(GET _pair 0 _egl_link)
    list(GET _pair 1 _egl_target)
    set(_egl_target_path "${_install_prefix}/${_egl_target}")
    set(_egl_link_path "${_install_prefix}/${_egl_link}")
    if (EXISTS "${_egl_target_path}" AND NOT EXISTS "${_egl_link_path}")
        file(CREATE_LINK "${_egl_target}" "${_egl_link_path}" SYMBOLIC)
        message(STATUS "EGL alias: ${_egl_link} -> ${_egl_target}")
    endif ()
endforeach ()
