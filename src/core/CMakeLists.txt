# game_core — the only module everything else may depend on. Deliberately tiny
# and free of SDL, sockets and third-party code, so it can be linked into the
# headless server and the tests without dragging anything along.
add_library(game_core STATIC
    src/bitstream.cpp
    src/log.cpp
    src/time.cpp)

target_include_directories(game_core PUBLIC include)
target_link_libraries(game_core PRIVATE game_warnings)
