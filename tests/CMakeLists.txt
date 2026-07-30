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
    test_pathfind.cpp
    test_item_type.cpp
    test_content_blob.cpp
    test_content_hash.cpp
    test_map_io.cpp
    test_combat.cpp
    test_atlas_meta.cpp
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

# The editor's atlas.txt writer is deliberately SDL-free string manipulation, so it
# is compiled straight into the tests. It edits a file people edit by hand, and a
# writer that mangles what it did not understand is not the sort of bug a screenshot
# finds.
target_sources(game_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/editor/atlas_meta.cpp")
target_include_directories(game_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/editor")

# game_store only exists when the server or the tools are enabled, so its test is
# conditional rather than assumed. It uses ":memory:" databases, keeping the suite
# free of any need for a writable directory.
if(TARGET game_store)
  target_sources(game_tests PRIVATE test_store.cpp test_content_store.cpp)
  target_link_libraries(game_tests PRIVATE game_store)
endif()

add_test(NAME unit COMMAND game_tests --no-intro)
