# game_server — headless authoritative simulation.
#
# Links game_sim, game_net and game_store. If this ever needs SDL3, a layer
# boundary has been broken — but SQLite is fine: the server-only preset's invariant
# is "no graphics library", and game_store is how content and player state are
# read and written (docs/content.md).
add_executable(game_server main.cpp)

target_link_libraries(game_server PRIVATE
    game_core
    game_sim
    game_net
    game_store
    game_warnings)
