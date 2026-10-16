# --- Engine: Saturn emulation model + game logic. No SDL, no GPU; unit tested. --
add_library(gridfin_engine STATIC
    core/Log.hpp
    core/Math.hpp
    data/Archive.cpp
    data/Archive.hpp
    data/Config.cpp
    data/Config.hpp
    data/Vfs.cpp
    data/Vfs.hpp
    data/Wav.cpp
    data/Wav.hpp
    saturn/Color.hpp
    saturn/Hardware.hpp
    saturn/Geometry.cpp
    saturn/Geometry.hpp
    saturn/Machine.hpp
    saturn/Scsp.cpp
    saturn/Scsp.hpp
    saturn/SoundDriver.cpp
    saturn/SoundDriver.hpp
    saturn/TextureCache.cpp
    saturn/TextureCache.hpp
    saturn/Vdp1.cpp
    saturn/Vdp1.hpp
    saturn/Vdp1Timing.cpp
    saturn/Vdp1Timing.hpp
    saturn/Vdp2.cpp
    saturn/Vdp2.hpp
    saturn/Window.cpp
    saturn/Window.hpp
    game/Assets.cpp
    game/Assets.hpp
    game/CameraRig.cpp
    game/CameraRig.hpp
    game/Game.cpp
    game/Game.hpp
    game/InputState.hpp
    game/Music.cpp
    game/Music.hpp
    game/Player.cpp
    game/Player.hpp
    game/SoundBank.cpp
    game/SoundBank.hpp
    game/World.cpp
    game/World.hpp)
add_library(gridfin::engine ALIAS gridfin_engine)
target_include_directories(gridfin_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gridfin_engine PUBLIC glm::glm)
gridfin_set_warnings(gridfin_engine)

# --- The game executable ---------------------------------------------------------
add_executable(gridfin
    main.cpp
    app/App.cpp
    app/App.hpp
    app/Screenshot.cpp
    app/Screenshot.hpp
    gfx/GpuContext.cpp
    gfx/GpuContext.hpp
    gfx/SaturnRenderer.cpp
    gfx/SaturnRenderer.hpp
    gfx/Surface.cpp
    gfx/WebGpu.hpp
    platform/Audio.cpp
    platform/Audio.hpp
    platform/Input.cpp
    platform/Input.hpp)
target_link_libraries(gridfin PRIVATE gridfin::engine gridfin::webgpu SDL3::SDL3)
gridfin_set_warnings(gridfin)

gridfin_embed_files(gridfin
    NAMESPACE gridfin::shaders
    HEADER Shaders.hpp
    FILES
        ../shaders/vdp.wgsl
        ../shaders/ntsc.wgsl
        ../shaders/present.wgsl)

if(GRIDFIN_SHADER_HOT_RELOAD AND NOT EMSCRIPTEN)
    # Dev builds read shaders and game data straight from the source tree, so edits reload live.
    target_compile_definitions(gridfin PRIVATE
        GRIDFIN_SHADER_DIR="${PROJECT_SOURCE_DIR}/shaders"
        GRIDFIN_DATA_DIR="${PROJECT_SOURCE_DIR}/data/base")
endif()

# --- Game data -------------------------------------------------------------------------------
file(GLOB_RECURSE GRIDFIN_BASE_DATA CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/data/base/*")

set_target_properties(gridfin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<CONFIG>")

if(WIN32)
    set_target_properties(gridfin PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
elseif(APPLE)
    target_link_libraries(gridfin PRIVATE "-framework Metal" "-framework QuartzCore")
endif()

gridfin_deploy_webgpu(gridfin)

if(EMSCRIPTEN)
    set_target_properties(gridfin PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
    target_link_options(gridfin PRIVATE
        "--shell-file=${PROJECT_SOURCE_DIR}/web/shell.html"
        -sALLOW_MEMORY_GROWTH=1
        -sENVIRONMENT=web
        "$<$<CONFIG:Debug>:-sASSERTIONS=2>"
        "$<$<CONFIG:Debug>:-gsource-map>"
        "$<$<CONFIG:Release>:--closure=1>"
        # The base game data, fetched with the page and mounted at /data/base.
        "--preload-file=${PROJECT_SOURCE_DIR}/data/base@/data/base")
    set_property(TARGET gridfin APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/web/shell.html" ${GRIDFIN_BASE_DATA})

    # `cmake --build --preset web-release --target serve` then open http://localhost:8080
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_custom_target(serve
            COMMAND Python3::Interpreter -m http.server 8080 --directory "$<TARGET_FILE_DIR:gridfin>"
            DEPENDS gridfin
            USES_TERMINAL
            COMMENT "Serving the web build at http://localhost:8080")
    endif()
else()
    # Pack the base game into base.gfa next to the executable, with the game's own packer.
    set(GRIDFIN_BASE_ARCHIVE "${PROJECT_BINARY_DIR}/bin/$<CONFIG>/base.gfa")
    add_custom_command(
        OUTPUT "${GRIDFIN_BASE_ARCHIVE}"
        COMMAND gridfin --pack "${PROJECT_SOURCE_DIR}/data/base" "${GRIDFIN_BASE_ARCHIVE}"
        DEPENDS gridfin ${GRIDFIN_BASE_DATA}
        COMMENT "Packing data/base into base.gfa"
        VERBATIM)
    add_custom_target(gridfin_data ALL DEPENDS "${GRIDFIN_BASE_ARCHIVE}")

    add_custom_target(run
        COMMAND gridfin
        DEPENDS gridfin
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:gridfin>"
        USES_TERMINAL)
endif()
