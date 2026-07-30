# Offline content tools. Built only when GAME_BUILD_TOOLS is ON, and never shipped
# to a player.
#
# Note what is absent: SDL. Baking is a database read and a byte write, so this
# target builds on a machine with no graphics libraries — the same property the
# server-only preset has. The map/item editor is the tool that does need SDL, and it
# lives in src/editor/ for that reason.
add_executable(game_bake bake/main.cpp)

target_link_libraries(game_bake PRIVATE
    game_core
    game_sim
    game_store
    game_warnings)
