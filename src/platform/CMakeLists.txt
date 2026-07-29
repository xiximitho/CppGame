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

if(NOT ANDROID)
  # Dev builds resolve assets straight from the source tree, so map-editor saves
  # and new sprites are picked up with no rebuild and no copy step: the client
  # and the editor read and write the very same files. A packaged build would
  # ship assets next to the executable and leave this unset (paths.cpp then falls
  # back to SDL_GetBasePath).
  target_compile_definitions(game_platform PRIVATE
      GAME_ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets/")
endif()
