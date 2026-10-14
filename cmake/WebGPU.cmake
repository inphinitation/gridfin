# Provides the `gridfin::webgpu` target implementing the standard webgpu.h API.
#
#  - Web:    Dawn's emdawnwebgpu port that ships with Emscripten.
#  - Native: prebuilt wgpu-native (Vulkan / Metal / D3D12), downloaded once.
#
# Both implement the same webgpu-native/webgpu-headers API, so game code is
# written once against <webgpu/webgpu.h>.

add_library(gridfin_webgpu INTERFACE)
add_library(gridfin::webgpu ALIAS gridfin_webgpu)

# Copies the wgpu-native runtime next to an executable and sets its rpath.
function(gridfin_deploy_webgpu target)
    if(EMSCRIPTEN)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${GRIDFIN_WGPU_RUNTIME}" "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
    if(APPLE)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "@executable_path" INSTALL_RPATH "@executable_path")
    elseif(UNIX)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
    endif()
endfunction()


if(EMSCRIPTEN)
    target_compile_options(gridfin_webgpu INTERFACE --use-port=emdawnwebgpu)
    target_link_options(gridfin_webgpu INTERFACE --use-port=emdawnwebgpu)
    return()
endif()

set(GRIDFIN_WGPU_VERSION "v29.0.1.1" CACHE STRING "wgpu-native release tag")

# Pinned archive hashes for the default version.
set(_wgpu_sha256_linux-x86_64        95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a)
set(_wgpu_sha256_linux-aarch64       015fcdf1dbae82e614a783cc38017e5399ae0927a889fe9b69c9b664bc61b47a)
set(_wgpu_sha256_windows-x86_64-msvc  7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132)
set(_wgpu_sha256_windows-aarch64-msvc 4a876421a8c1e5fe72f849b3722214280fe485cb1c56f77f8b0c82414be5b29f)
set(_wgpu_sha256_macos-x86_64        8e2f7378548ddd0e2cf21e7d864dda46e953f0af724855a33778b85ead206d41)
set(_wgpu_sha256_macos-aarch64       a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e)

# Work out which archive matches the target platform.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _wgpu_cpu)
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 _wgpu_cpu)
endif()
if(_wgpu_cpu MATCHES "^(arm64|aarch64)$")
    set(_wgpu_arch aarch64)
elseif(_wgpu_cpu MATCHES "^(x86_64|amd64|x64)$")
    set(_wgpu_arch x86_64)
else()
    message(FATAL_ERROR "No prebuilt wgpu-native for CPU '${CMAKE_SYSTEM_PROCESSOR}'")
endif()

if(WIN32)
    set(_wgpu_platform "windows-${_wgpu_arch}-msvc")
    set(_wgpu_shared "wgpu_native.dll")
    set(_wgpu_implib "wgpu_native.dll.lib")
elseif(APPLE)
    set(_wgpu_platform "macos-${_wgpu_arch}")
    set(_wgpu_shared "libwgpu_native.dylib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_wgpu_platform "linux-${_wgpu_arch}")
    set(_wgpu_shared "libwgpu_native.so")
else()
    message(FATAL_ERROR "Unsupported platform '${CMAKE_SYSTEM_NAME}' for wgpu-native")
endif()

set(_wgpu_url "https://github.com/gfx-rs/wgpu-native/releases/download/${GRIDFIN_WGPU_VERSION}/wgpu-${_wgpu_platform}-release.zip")
set(_wgpu_hash_args "")
if(GRIDFIN_WGPU_VERSION STREQUAL "v29.0.1.1")
    set(_wgpu_hash_args URL_HASH "SHA256=${_wgpu_sha256_${_wgpu_platform}}")
endif()

FetchContent_Declare(wgpu_native
    URL "${_wgpu_url}"
    ${_wgpu_hash_args}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(wgpu_native)

set(GRIDFIN_WGPU_RUNTIME "${wgpu_native_SOURCE_DIR}/lib/${_wgpu_shared}" CACHE INTERNAL "")

add_library(gridfin_wgpu_native SHARED IMPORTED GLOBAL)
set_target_properties(gridfin_wgpu_native PROPERTIES
    IMPORTED_LOCATION "${GRIDFIN_WGPU_RUNTIME}"
    INTERFACE_INCLUDE_DIRECTORIES "${wgpu_native_SOURCE_DIR}/include")
if(WIN32)
    set_target_properties(gridfin_wgpu_native PROPERTIES
        IMPORTED_IMPLIB "${wgpu_native_SOURCE_DIR}/lib/${_wgpu_implib}")
elseif(NOT APPLE)
    # The Linux build has no SONAME; link it by name so the runtime lookup uses $ORIGIN.
    set_target_properties(gridfin_wgpu_native PROPERTIES IMPORTED_NO_SONAME TRUE)
endif()

target_link_libraries(gridfin_webgpu INTERFACE gridfin_wgpu_native)
target_compile_definitions(gridfin_webgpu INTERFACE GRIDFIN_WGPU_NATIVE=1)
