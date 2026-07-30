# game_editor — a small isometric map editor.
#
# A client-side tool: it links SDL and reuses the client's renderer, tileset and
# world drawing. Those three sources are compiled into this target as well as the
# client rather than split into a shared library — a pragmatic reuse that keeps
# the client's build untouched. If a third consumer appears, promote them to a
# game_render library instead.
add_executable(game_editor
    main.cpp
    item_mode.cpp
    ${CMAKE_SOURCE_DIR}/src/client/src/sdl_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/client/src/tileset.cpp
    ${CMAKE_SOURCE_DIR}/src/client/src/ui.cpp
    ${CMAKE_SOURCE_DIR}/src/client/src/world_render.cpp)

target_include_directories(game_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/src/client/include)

target_link_libraries(game_editor PRIVATE
    game_core
    game_sim
    game_platform
    game_store
    SDL3::SDL3
    SDL3_image::SDL3_image
    game_warnings)
