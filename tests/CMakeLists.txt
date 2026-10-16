add_executable(gridfin_tests
    main.cpp
    test_color.cpp
    test_vdp1.cpp
    test_geometry.cpp
    test_game.cpp
    test_streaming.cpp
    test_water.cpp
    test_sound.cpp
    test_data.cpp
    test_script.cpp)
target_link_libraries(gridfin_tests PRIVATE gridfin::engine doctest::doctest)
gridfin_set_warnings(gridfin_tests)
target_compile_definitions(gridfin_tests PRIVATE GRIDFIN_DATA_DIR="${PROJECT_SOURCE_DIR}/data/base")

include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
doctest_discover_tests(gridfin_tests)

if(NOT MSVC)
    # doctest::Approx works in double precision.
    target_compile_options(gridfin_tests PRIVATE -Wno-double-promotion)
endif()
