# game_client — window, rendering, input, and the two session flavours.
#
# The only module that links SDL3 alongside the game logic. It depends on game_sim
# for types and rules but never advances the simulation itself, except through
# SoloSession, which drives the same World a server would.

set(GAME_CLIENT_SOURCES
    src/main.cpp
    src/sdl_backend.cpp
    src/tileset.cpp
    src/content.cpp
    src/input.cpp
    src/world_render.cpp
    src/ui.cpp
    src/damage_feed.cpp
    src/effect_feed.cpp
    src/inventory_ui.cpp
    src/solo_session.cpp
    src/remote_session.cpp)

if(ANDROID)
  # Android has no executable entry point: SDLActivity loads a shared library and
  # calls SDL_main inside it. The name must match what the Java side loads.
  # See docs/mobile.md.
  add_library(game_client SHARED ${GAME_CLIENT_SOURCES})
  set_target_properties(game_client PROPERTIES OUTPUT_NAME "main")
else()
  add_executable(game_client ${GAME_CLIENT_SOURCES})
endif()

target_include_directories(game_client PRIVATE include)

target_link_libraries(game_client PRIVATE
    game_core
    game_sim
    game_net
    game_platform
    SDL3::SDL3
    SDL3_image::SDL3_image
    game_warnings)

if(WIN32)
  # Keeps a console window from appearing behind the game in release builds, while
  # Debug keeps it so the log is visible. SDL_main handles the entry point.
  set_target_properties(game_client PROPERTIES
      WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
endif()
