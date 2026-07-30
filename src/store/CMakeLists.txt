# game_store — the SQLite wrapper.
#
# Linked by the server (player persistence) and by the offline tools (content
# authoring). NEVER by the client: on Android and iOS its data lives inside the
# application package and is not a file, while SQLite needs a real path to open
# and seek. The client reads baked content through platform::vfs instead.
# scripts/check-layering.sh enforces that, and docs/content.md explains why.
#
# sqlite3 is PRIVATE so that no consumer inherits sqlite3.h: store/db.hpp mentions
# no sqlite3 type, which is what keeps the dependency contained to this directory.
add_library(game_store STATIC
    src/db.cpp
    src/schema.cpp
    src/content.cpp)

target_include_directories(game_store PUBLIC include)

# game_sim is PUBLIC because the content API speaks in sim::ItemType: this module
# is where the game's rows become the game's types. It reads sim types and writes
# them; sim itself never learns that a database exists (check-layering.sh).
target_link_libraries(game_store
    PUBLIC  game_core
            game_sim
    PRIVATE sqlite3
            game_warnings)
