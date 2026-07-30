cmake_minimum_required(VERSION 3.25)

# Dependencies are unpacked next to the sources instead of inside a build dir so
# that (a) several build configurations share one download and (b) the whole
# cache can be tarred up and carried to a machine with no network access.
# See docs/dependencies.md.
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps"
    CACHE PATH "Where pinned third-party sources are downloaded and built")

project(game
        VERSION 0.1.0
        DESCRIPTION "Isometric tile-based multiplayer game"
        LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GAME_BUILD_CLIENT "Build the graphical client (needs SDL3)" ON)
option(GAME_BUILD_SERVER "Build the headless server" ON)
option(GAME_BUILD_TESTS  "Build unit tests" ON)
# Offline content tools: the map/item editor and the content baker. They link
# SQLite (the authoring store) and are never shipped to a player, which is why
# they are separable from the client that is.
option(GAME_BUILD_TOOLS  "Build the offline content tools (editor, bake)" ON)
option(GAME_USE_SYSTEM_SDL
       "Link the SDL3 already installed on this machine instead of the pinned one" OFF)
option(GAME_WERROR "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Base de compilação para o language server (clangd e qualquer outro que a leia).
# Sem ela o editor não conhece os -I de cada módulo: marca `sim/...` como "file
# not found" e todo tipo que vem de header some, mesmo com o build passando. O
# `.clangd` na raiz aponta para build/debug.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Everything lands in one place so shipping a build is "copy build/bin".
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

if(APPLE)
  # Oldest macOS the produced binaries will start on.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
endif()

# Shared warning flags, attached to our own targets only. Third-party code is
# consumed as-is; we never want its warnings failing our build.
add_library(game_warnings INTERFACE)
if(MSVC)
  target_compile_options(game_warnings INTERFACE /W4 /permissive- /utf-8)
  target_compile_definitions(game_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
  if(GAME_WERROR)
    target_compile_options(game_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(game_warnings INTERFACE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Wold-style-cast -Wdouble-promotion)
  if(GAME_WERROR)
    target_compile_options(game_warnings INTERFACE -Werror)
  endif()
endif()

include(Dependencies)

# ---------------------------------------------------------------------------
# Modules. The dependency direction is strictly one-way:
#
#   core   <- sim <- net <- server
#     ^        ^      ^        ^
#     |        +------+---- platform <- client
#     +---- store ------------+----------- tools (editor, bake)
#
# sim/ must never reach for SDL, sockets or the filesystem: it is the thing the
# server runs headless and the thing the tests drive. scripts/check-layering.sh
# enforces that in CI.
#
# store/ is the SQLite wrapper. It sits beside net/ rather than under it: the
# server needs both, the tools need only store, and the CLIENT MUST LINK NEITHER
# SQLite NOR store — its content comes from the baked blob, because a database
# file does not exist inside an Android APK.
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/sim)
add_subdirectory(src/net)

if(GAME_BUILD_SERVER OR GAME_BUILD_TOOLS)
  add_subdirectory(src/store)
endif()

if(GAME_BUILD_CLIENT)
  add_subdirectory(src/platform)
  add_subdirectory(src/client)
endif()

if(GAME_BUILD_TOOLS AND GAME_BUILD_CLIENT)
  # The editor draws, so it needs the client's renderer and SDL. The content baker
  # does not and will live under tools/ when it exists.
  add_subdirectory(src/editor)
endif()

if(GAME_BUILD_SERVER)
  add_subdirectory(src/server)
endif()

if(GAME_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Summary — printed on every configure so a broken machine is obvious early.
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "==== ${PROJECT_NAME} ${PROJECT_VERSION} ====")
message(STATUS "  build type ....... ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler ......... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  client ........... ${GAME_BUILD_CLIENT}")
message(STATUS "  server ........... ${GAME_BUILD_SERVER}")
message(STATUS "  tests ............ ${GAME_BUILD_TESTS}")
message(STATUS "  tools ............ ${GAME_BUILD_TOOLS}")
message(STATUS "  SDL3 source ...... ${GAME_SDL_ORIGIN}")
message(STATUS "  SQLite ........... ${GAME_SQLITE_ORIGIN}")
message(STATUS "  deps cache ....... ${FETCHCONTENT_BASE_DIR}")
message(STATUS "  binaries ......... ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
