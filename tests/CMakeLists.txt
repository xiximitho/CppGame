# Unit tests.
#
# They link game_sim and game_net but NOT SDL3, which is the point: the rules, the
# projection maths and the wire format are all testable without a window. If a
# test here ever needs SDL, something has leaked out of the client layer.
add_executable(game_tests
    main.cpp
    test_iso.cpp
    test_bitstream.cpp
    test_protocol.cpp
    test_world.cpp
    test_input.cpp)

target_link_libraries(game_tests PRIVATE
    game_core
    game_sim
    game_net
    doctest::doctest
    game_warnings)

# iso.hpp and input.hpp are header-only and SDL-free, so they can be tested
# without linking the client executable.
target_include_directories(game_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/client/include")

add_test(NAME unit COMMAND game_tests --no-intro)
