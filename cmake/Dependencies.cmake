# Third-party dependencies. Everything is fetched at configure time so a fresh
# clone builds with nothing but CMake, Ninja and a compiler (plus emsdk for the
# web).

include(FetchContent)

set(GRIDFIN_SDL_VERSION "release-3.4.8" CACHE STRING "SDL3 git tag")
set(GRIDFIN_GLM_VERSION "1.0.1" CACHE STRING "glm git tag")
set(GRIDFIN_DOCTEST_VERSION "v2.4.12" CACHE STRING "doctest git tag")

# --- SDL3: window, input, gamepads, main loop (native and Emscripten) --------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# Subsystems the game doesn't use; trimming them speeds up the build.
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${GRIDFIN_SDL_VERSION}
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    SYSTEM)

# --- glm: math ----------------------------------------------------------------
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG ${GRIDFIN_GLM_VERSION}
    GIT_SHALLOW TRUE
    SYSTEM)

FetchContent_MakeAvailable(SDL3 glm)

if(GRIDFIN_BUILD_TESTS AND NOT EMSCRIPTEN)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG ${GRIDFIN_DOCTEST_VERSION}
        GIT_SHALLOW TRUE
        SYSTEM)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()

include(WebGPU)
