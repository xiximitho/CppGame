# game_net — wire format and transport. No SDL: the headless server links this.
add_library(game_net STATIC
    src/protocol.cpp
    src/enet_transport.cpp)

target_include_directories(game_net PUBLIC include)

target_link_libraries(game_net
    PUBLIC  game_core
            game_sim
    PRIVATE enet
            game_warnings)
