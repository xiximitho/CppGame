# game_sim — authoritative game state and rules.
#
# HARD RULE: no SDL, no sockets, no file I/O, no wall-clock reads. This library
# must stay linkable into a headless server and drivable from a unit test with
# nothing else present. scripts/check-layering.sh fails the build if an include
# sneaks in.
add_library(game_sim STATIC
    src/tile_map.cpp
    src/world.cpp
    src/snapshot.cpp
    src/map_gen.cpp
    src/systems.cpp)

target_include_directories(game_sim PUBLIC include)

target_link_libraries(game_sim
    PUBLIC  game_core
            EnTT::EnTT
    PRIVATE game_warnings)
