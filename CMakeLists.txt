cmake_minimum_required(VERSION 3.15)
project(SimpleGraphic C CXX)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

if (APPLE)
    enable_language(OBJCXX)
    # Apple Clang does not search libc++ under -isysroot for project targets (see triplets/arm64-osx.cmake).
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    # CLT/Xcode updates remove old SDK folders; stale CMakeCache sysroots break imgui/engine builds.
    if(NOT CMAKE_OSX_SYSROOT OR NOT EXISTS "${CMAKE_OSX_SYSROOT}")
        set(CMAKE_OSX_SYSROOT "${MACOS_SDK_PATH}" CACHE PATH "macOS SDK sysroot" FORCE)
        message(STATUS "macOS: CMAKE_OSX_SYSROOT -> ${CMAKE_OSX_SYSROOT}")
    endif()
    add_compile_options(
        "$<$<OR:$<COMPILE_LANGUAGE:CXX>,$<COMPILE_LANGUAGE:OBJCXX>>:-isystem${MACOS_SDK_PATH}/usr/include/c++/v1>"
    )
endif ()

if (MSVC)
    add_compile_options("/Zi")
    add_link_options("/DEBUG:FULL")
endif ()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(${PROJECT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
include(InstallRequiredSystemLibraries)
add_subdirectory(dep/glm)

set(SIMPLEGRAPHIC_SOURCES
    "config.h"
    "dep/stb/stb_image.h"
    "dep/stb/stb_image_resize.h"
    "dep/stb/stb_image_write.h"
    "engine/common.h"
    "engine/common/base64.c"
    "engine/common/base64.h"
    "engine/common/common.cpp"
    "engine/common/console.cpp"
    "engine/common/console.h"
    "engine/common/keylist.h"
    "engine/common/memtrak3.cpp"
    "engine/common/memtrak3.h"
    "engine/common/streams.cpp"
    "engine/common/streams.h"
    "engine/core/core_compress.cpp"
    "engine/core/core_compress.h"
    "engine/core/core_config.cpp"
    "engine/core/core_config.h"
    "engine/core/core_image.cpp"
    "engine/core/core_image.h"
    "engine/core/core_main.cpp"
    "engine/core/core_main.h"
    "engine/core/core_tex_manipulation.cpp"
    "engine/core/core_tex_manipulation.h"
    "engine/core/core_video.cpp"
    "engine/core/core_video.h"
    "engine/render/r_font.cpp"
    "engine/render/r_font.h"
    "engine/render/r_main.cpp"
    "engine/render/r_main.h"
    "engine/render/r_texture.cpp"
    "engine/render/r_texture.h"
    "engine/system/win/sys_local.h"
    "engine/system/win/sys_main.cpp"
    "engine/system/win/sys_opengl.cpp"
    "engine/system/win/sys_video.cpp"
    "engine/system/sys_console.h"
    "engine/system/sys_main.h"
    "engine/system/sys_opengl.h"
    "engine/system/sys_video.h"
    "ui.h"
    "ui_api.cpp"
    "ui_console.cpp"
    "ui_console.h"
    "ui_debug.cpp"
    "ui_debug.h"
    "ui_local.h"
    "ui_main.cpp"
    "ui_main.h"
    "ui_subscript.cpp"
    "ui_subscript.h"
)

if (APPLE)
    list(APPEND SIMPLEGRAPHIC_SOURCES "mac/entry.cpp")
elseif (WIN32)
    list(APPEND SIMPLEGRAPHIC_SOURCES "win/entry.cpp")
else ()
    list(APPEND SIMPLEGRAPHIC_SOURCES "win/entry.cpp")
endif ()

set(SIMPLEGRAPHIC_PLATFORM_SOURCES)
if (APPLE)
    list(APPEND SIMPLEGRAPHIC_PLATFORM_SOURCES
        "engine/system/win/sys_macos.mm"
    )
endif()

if (WIN32)
    list(APPEND SIMPLEGRAPHIC_PLATFORM_SOURCES
        "engine/system/win/sys_console.cpp"
        "SimpleGraphic.rc"
    )
else()
    list(APPEND SIMPLEGRAPHIC_PLATFORM_SOURCES
        "engine/system/win/sys_console_unix.cpp"
    )
endif()

add_library(SimpleGraphic SHARED
    ${SIMPLEGRAPHIC_SOURCES}
    ${SIMPLEGRAPHIC_PLATFORM_SOURCES}
)
target_compile_definitions(SimpleGraphic
    PRIVATE
    "UNICODE"
    "_CRT_SECURE_NO_DEPRECATE"
    "_CRT_SECURE_NO_WARNINGS"
    "_SCL_SECURE_NO_DEPRECATE"
    "_SCL_SECURE_NO_WARNINGS"
    "GLFW_INCLUDE_NONE"
    "GL_SILENCE_DEPRECATION"
    "SIMPLEGRAPHIC_EXPORTS"
)

target_include_directories(SimpleGraphic
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine"
)

if (APPLE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacosFixVcpkgReleaseOnly.cmake")
endif ()
find_package(unofficial-angle CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(gli CONFIG REQUIRED)
find_package(LuaJIT REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(zstd REQUIRED)
find_package(ZLIB REQUIRED)
find_package(WebP)

add_library(cmp_core STATIC
    dep/compressonator/cmp_core/source/cmp_core.cpp
    dep/compressonator/cmp_core/source/cmp_core.h
    dep/compressonator/cmp_core/shaders/bc3_encode_kernel.cpp
    dep/compressonator/cmp_core/shaders/bc7_encode_kernel.cpp
)

target_include_directories(cmp_core PRIVATE
    dep/compressonator/applications/_libs/cmp_math
)

target_include_directories(cmp_core PUBLIC
    dep/compressonator/cmp_core/shaders
    dep/compressonator/cmp_core/source
)

add_library(imgui STATIC
    dep/imgui/imconfig.h
    dep/imgui/imgui.cpp
    dep/imgui/imgui.h
    dep/imgui/imgui_demo.cpp
    dep/imgui/imgui_draw.cpp
    dep/imgui/imgui_internal.h
    dep/imgui/imgui_tables.cpp
    dep/imgui/imgui_widgets.cpp
    dep/imgui/imstb_rectpack.h
    dep/imgui/imstb_textedit.h
    dep/imgui/imstb_truetype.h
    dep/imgui/backends/imgui_impl_glfw.cpp
    dep/imgui/backends/imgui_impl_glfw.h
    dep/imgui/backends/imgui_impl_opengl3.cpp
    dep/imgui/backends/imgui_impl_opengl3.h
    dep/imgui/backends/imgui_impl_opengl3_loader.h
    dep/imgui/misc/cpp/imgui_stdlib.cpp
    dep/imgui/misc/cpp/imgui_stdlib.h
)

target_compile_definitions(imgui PUBLIC
    "IMGUI_IMPL_OPENGL_ES2"
)

target_include_directories(imgui PUBLIC
    dep/imgui
    dep/imgui/backends
    dep/imgui/misc/cpp
)

target_link_libraries(imgui PUBLIC
    unofficial::angle::libGLESv2
    glfw
)

target_include_directories(SimpleGraphic
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dep/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dep/stb
)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_options(SimpleGraphic
        PRIVATE
        "-export-dynamic"
    )
endif ()

if (WIN32)
    target_compile_options(SimpleGraphic
        PRIVATE
        "/EHa"
    )
    target_compile_definitions(SimpleGraphic
        PRIVATE
        "_CRT_SECURE_NO_DEPRECATE=1"
        "_CRT_SECURE_NO_WARNINGS=1"
        "_SCL_SECURE_NO_DEPRECATE=1"
        "_SCL_SECURE_NO_WARNINGS=1"
    )
    target_link_libraries(SimpleGraphic
        PRIVATE
        "winmm.lib"
    )
endif ()

if (APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation)
    find_library(APPLICATION_SERVICES_LIBRARY ApplicationServices)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    target_link_libraries(SimpleGraphic
        PRIVATE
        ${CORE_FOUNDATION_LIBRARY}
        ${APPLICATION_SERVICES_LIBRARY}
        ${FOUNDATION_FRAMEWORK}
    )
endif ()

target_link_libraries(SimpleGraphic
    PRIVATE
    unofficial::angle::libEGL
    unofficial::angle::libGLESv2
    cmp_core
    fmt::fmt
    glfw
    gli
    glm::glm
    imgui
    LuaJIT::LuaJIT
    Microsoft.GSL::GSL
    re2::re2
    Threads::Threads
    WebP::webpdecoder
    ZLIB::ZLIB
    zstd::libzstd_shared
)

if (APPLE)
    set(PoB_HOST_TARGET pob-host)
    add_executable(${PoB_HOST_TARGET} mac/host.cpp)
    set_target_properties(${PoB_HOST_TARGET} PROPERTIES
        OUTPUT_NAME "Path of Building-PoE2"
    )
    target_link_libraries(${PoB_HOST_TARGET} PRIVATE SimpleGraphic)
    # Support both launch layouts:
    # 1) .app bundle:    @executable_path/../Frameworks
    # 2) flat dev binary @executable_path (runtime-macos/)
    set(_macos_host_rpath "@executable_path/../Frameworks;@executable_path;@loader_path")
    set_target_properties(SimpleGraphic ${PoB_HOST_TARGET}
        PROPERTIES
            BUILD_RPATH "${_macos_host_rpath}"
            INSTALL_RPATH "${_macos_host_rpath}"
            MACOSX_RPATH ON
    )
endif ()

if (APPLE)
    install(TARGETS SimpleGraphic LIBRARY DESTINATION ".")
    install(TARGETS ${PoB_HOST_TARGET} RUNTIME DESTINATION ".")
else ()
    install(TARGETS SimpleGraphic RUNTIME DESTINATION ".")
endif ()
install(FILES $<TARGET_RUNTIME_DLLS:SimpleGraphic> DESTINATION ".")

if (WIN32)
    set(DEPS_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

    if (NOT VCPKG_TARGET_TRIPLET MATCHES "-release")
        find_file(LuaJIT_DLL_DEBUG NAMES "lua51.dll"
            PATHS "${DEPS_DIR}" PATH_SUFFIXES "debug/bin" REQUIRED NO_DEFAULT_PATH)
        find_file(ZLIB_DLL_DEBUG NAMES "zlibd1.dll"
            PATHS "${DEPS_DIR}" PATH_SUFFIXES "debug/bin" REQUIRED NO_DEFAULT_PATH)

        install(FILES
            ${LuaJIT_DLL_DEBUG}
            ${ZLIB_DLL_DEBUG}
            DESTINATION "."
            CONFIGURATIONS Debug
        )
    endif ()

    find_file(LuaJIT_DLL_RELEASE NAMES "lua51.dll"
        PATHS "${DEPS_DIR}" PATH_SUFFIXES "bin" REQUIRED NO_DEFAULT_PATH)
    find_file(ZLIB_DLL_RELEASE NAMES "zlib1.dll"
        PATHS "${DEPS_DIR}" PATH_SUFFIXES "bin" REQUIRED NO_DEFAULT_PATH)

    install(FILES
        ${LuaJIT_DLL_RELEASE}
        ${ZLIB_DLL_RELEASE}
        DESTINATION "."
        CONFIGURATIONS Release MinSizeRel RelWithDebInfo
    )
endif ()


# lcurl module

set(LCURL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/Lua-cURLv3)
file(GLOB LCURL_SOURCES ${LCURL_SOURCE_DIR}/src/**.c)
add_library(lcurl SHARED ${LCURL_SOURCES})

target_include_directories(lcurl
    PRIVATE
    ${LCURL_SOURCE_DIR}/src
)

target_link_libraries(lcurl
    PRIVATE
    CURL::libcurl
    LuaJIT::LuaJIT
)

if (APPLE)
    set_target_properties(lcurl PROPERTIES PREFIX "" SUFFIX ".so")
endif ()
if (APPLE)
    install(TARGETS lcurl LIBRARY DESTINATION ".")
else ()
    install(TARGETS lcurl RUNTIME DESTINATION ".")
endif ()
install(FILES $<TARGET_RUNTIME_DLLS:lcurl> DESTINATION ".")

# luautf8 module

add_library(lua-utf8 SHARED libs/luautf8/lutf8lib.c)

if (WIN32)
    target_compile_definitions(lua-utf8 PRIVATE LUA_BUILD_AS_DLL)
endif ()

target_include_directories(lua-utf8
    PRIVATE
)

target_link_libraries(lua-utf8
    PRIVATE
    LuaJIT::LuaJIT
)

if (APPLE)
    set_target_properties(lua-utf8 PROPERTIES PREFIX "" SUFFIX ".so")
endif ()
if (APPLE)
    install(TARGETS lua-utf8 LIBRARY DESTINATION ".")
else ()
    install(TARGETS lua-utf8 RUNTIME DESTINATION ".")
endif ()
install(FILES $<TARGET_RUNTIME_DLLS:lua-utf8> DESTINATION ".")

# luasocket module

set(LUASOCKET_SOURCES
   "libs/luasocket/src/auxiliar.c"
   "libs/luasocket/src/buffer.c"
   "libs/luasocket/src/compat.c"
   "libs/luasocket/src/except.c"
   "libs/luasocket/src/inet.c"
   "libs/luasocket/src/io.c"
   "libs/luasocket/src/luasocket.c"
   "libs/luasocket/src/options.c"
   "libs/luasocket/src/select.c"
   "libs/luasocket/src/tcp.c"
   "libs/luasocket/src/timeout.c"
   "libs/luasocket/src/udp.c"
)
if (WIN32)
    list(APPEND LUASOCKET_SOURCES "libs/luasocket/src/wsocket.c")
else ()
    list(APPEND LUASOCKET_SOURCES "libs/luasocket/src/usocket.c")
endif ()

add_library(luasocket SHARED ${LUASOCKET_SOURCES})

target_include_directories(luasocket
    PRIVATE
    "libs/luasocket/src"
)

target_link_libraries(luasocket
    PRIVATE
    LuaJIT::LuaJIT
)
if (WIN32)
    target_link_libraries(luasocket PRIVATE wsock32 ws2_32)
endif ()

set_target_properties(luasocket PROPERTIES OUTPUT_NAME "socket")
if (APPLE)
    set_target_properties(luasocket PROPERTIES PREFIX "" SUFFIX ".so")
endif ()
if (APPLE)
    install(TARGETS luasocket LIBRARY DESTINATION ".")
else ()
    install(TARGETS luasocket RUNTIME DESTINATION ".")
endif ()
install(FILES $<TARGET_RUNTIME_DLLS:luasocket> DESTINATION ".")

# lzip module

add_library(lzip SHARED libs/LZip/lzip.cpp)

target_include_directories(lzip
    PRIVATE
)

target_link_libraries(lzip
    PRIVATE
    LuaJIT::LuaJIT
    ZLIB::ZLIB
)

if (APPLE)
    set_target_properties(lzip PROPERTIES PREFIX "" SUFFIX ".so")
endif ()
if (APPLE)
    install(TARGETS lzip LIBRARY DESTINATION ".")
else ()
    install(TARGETS lzip RUNTIME DESTINATION ".")
endif ()
install(FILES $<TARGET_RUNTIME_DLLS:lzip> DESTINATION ".")

if (APPLE)
    set_target_properties(lcurl luasocket lzip lua-utf8
        PROPERTIES
            BUILD_RPATH "@executable_path;@loader_path"
            INSTALL_RPATH "@executable_path;@loader_path"
            MACOSX_RPATH ON
    )

    set(DEPS_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    set(MACOS_BUNDLE_LIB_DIR "${DEPS_DIR}/lib")
    set(MACOS_BUNDLE_SG_DYLIB "${CMAKE_CURRENT_BINARY_DIR}/libSimpleGraphic.dylib")
    set(MACOS_BUNDLE_LCURL "${CMAKE_CURRENT_BINARY_DIR}/lcurl.so")
    set(MACOS_BUNDLE_SOCKET "${CMAKE_CURRENT_BINARY_DIR}/socket.so")
    set(MACOS_BUNDLE_LZIP "${CMAKE_CURRENT_BINARY_DIR}/lzip.so")
    set(MACOS_BUNDLE_UTF8 "${CMAKE_CURRENT_BINARY_DIR}/lua-utf8.so")
    set(_macos_bundle_script "${CMAKE_CURRENT_BINARY_DIR}/macos_bundle_runtime.cmake")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_bundle_runtime.cmake"
        "${_macos_bundle_script}"
        @ONLY
    )
    install(SCRIPT "${_macos_bundle_script}")
endif ()
