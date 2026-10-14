add_executable(gridfin_tests
    main.cpp
    test_color.cpp
    test_vdp1.cpp
    test_geometry.cpp
    test_game.cpp
    test_streaming.cpp)
target_link_libraries(gridfin_tests PRIVATE gridfin::engine doctest::doctest)
gridfin_set_warnings(gridfin_tests)

include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
doctest_discover_tests(gridfin_tests)

if(NOT MSVC)
    # doctest::Approx works in double precision.
    target_compile_options(gridfin_tests PRIVATE -Wno-double-promotion)
endif()
