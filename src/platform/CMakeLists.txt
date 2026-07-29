# game_platform — the only place allowed to know about the operating system:
# window handles, asset locations, writable directories, mobile lifecycle.
# Client-side only; the server links none of this.
add_library(game_platform STATIC
    src/paths.cpp
    src/vfs.cpp)

target_include_directories(game_platform PUBLIC include)

target_link_libraries(game_platform
    PUBLIC  game_core
            SDL3::SDL3
    PRIVATE game_warnings)
