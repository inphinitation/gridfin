cmake_minimum_required(VERSION 3.25)

# std::format with floating point needs macOS 13.3.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")

project(gridfin
    VERSION 0.1.0
    DESCRIPTION "A Sega Saturn styled 3D action game for desktop and the web"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a preset: cmake --preset native")
endif()

option(GRIDFIN_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(GRIDFIN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(GRIDFIN_SHADER_HOT_RELOAD "Load shaders from the source tree and allow reloading them with F5 (native only)" OFF)

include(CompilerWarnings)
include(Dependencies)
include(EmbedFiles)

add_subdirectory(src)

if(GRIDFIN_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
endif()
