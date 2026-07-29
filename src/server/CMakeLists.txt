# game_server — headless authoritative simulation.
#
# Links game_sim and game_net and nothing else. If this ever needs SDL3, a layer
# boundary has been broken.
add_executable(game_server main.cpp)

target_link_libraries(game_server PRIVATE
    game_core
    game_sim
    game_net
    game_warnings)
