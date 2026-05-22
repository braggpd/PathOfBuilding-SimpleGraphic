set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET "13.0")

# Apple Clang does not search libc++ under -isysroot; vcpkg/ANGLE need this explicitly.
execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE VCPKG_OSX_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
set(VCPKG_CXX_FLAGS "-isystem ${VCPKG_OSX_SDK_PATH}/usr/include/c++/v1")
set(VCPKG_C_FLAGS "${VCPKG_CXX_FLAGS}")
