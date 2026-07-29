# ---------------------------------------------------------------------------
# Third-party dependencies.
#
# Every dependency is pinned to an exact commit SHA, never to a tag or branch.
# Tags can be moved by upstream; a SHA cannot. This is what makes a build on
# your machine and a build on a teammate's machine byte-for-byte comparable.
#
# To bump a dependency: change the SHA and the comment next to it together,
# then run scripts/verify-deps.sh.
# ---------------------------------------------------------------------------
include(FetchContent)

set(FETCHCONTENT_QUIET FALSE)

# Every dependency below is declared SYSTEM (CMake 3.25+). Without it, their
# headers are compiled under our own -Wconversion/-Wsign-conversion/-Wpedantic
# set and third-party code we do not control fails our build. Warnings must stay
# strict for src/ and silent for .deps/.
set(GAME_DEP_SYSTEM SYSTEM)

# CMake 4 dropped compatibility with cmake_minimum_required(VERSION < 3.5).
# ENet 1.3.18 still declares 2.8.12, which is a hard configure error otherwise.
# This is the officially supported escape hatch and only affects dependencies.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# --- pinned revisions ------------------------------------------------------
set(GAME_DEP_SDL_REF     "f87239e71e42da91ca317a12eefb82cfbf3393eb") # release-3.4.12
set(GAME_DEP_SDLIMG_REF  "bec9134a26c7d0f31b36d6083c25296e04cabff5") # release-3.4.4
set(GAME_DEP_ENTT_REF    "b4e58bdd364ad72246c123a0c28538eab3252672") # v3.16.0
set(GAME_DEP_GLM_REF     "8d1fd52e5ab5590e2c81768ace50c72bae28f2ed") # 1.0.3
set(GAME_DEP_ENET_REF    "2662c0de09e36f2a2030ccc2c528a3e4c9e8138a") # v1.3.18
set(GAME_DEP_DOCTEST_REF "2d0a9359a60c51affe2a9bebb1be1dca47868151") # v2.5.3

# ---------------------------------------------------------------------------
# EnTT — entity component system. Header only.
# ---------------------------------------------------------------------------
set(ENTT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        ${GAME_DEP_ENTT_REF}
    ${GAME_DEP_SYSTEM})

# ---------------------------------------------------------------------------
# glm — vector/matrix math. Header only.
# ---------------------------------------------------------------------------
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        ${GAME_DEP_GLM_REF}
    ${GAME_DEP_SYSTEM})

FetchContent_MakeAvailable(EnTT glm)

# glm defaults to a lot of implicit conversions and operator overloads we do not
# want silently changing precision.
target_compile_definitions(glm INTERFACE
    GLM_FORCE_EXPLICIT_CTOR
    GLM_ENABLE_EXPERIMENTAL)

# ---------------------------------------------------------------------------
# ENet — reliable UDP. This is the transport behind net::ITransport; swapping it
# for GameNetworkingSockets later means adding one more implementation of that
# interface, not touching game code.
# ---------------------------------------------------------------------------
FetchContent_Declare(enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG        ${GAME_DEP_ENET_REF}
    ${GAME_DEP_SYSTEM})
FetchContent_MakeAvailable(enet)

# ENet 1.3.x publishes its headers with directory-scoped include_directories(),
# which does not propagate to consumers. Attach them to the target properly.
if(TARGET enet)
  target_include_directories(enet PUBLIC "${enet_SOURCE_DIR}/include")
  if(WIN32)
    target_link_libraries(enet PUBLIC ws2_32 winmm)
  endif()
endif()

# ---------------------------------------------------------------------------
# SDL3 — window, input, audio, mobile lifecycle. Client and tools only; the
# server links none of it.
#
# Built statically on purpose: shipping the client is then a single executable
# with no "missing SDL3.dll / .so" support tickets. On Linux, SDL still dlopen()s
# Wayland/X11/ALSA at runtime, so a static SDL does not pin the display server.
# ---------------------------------------------------------------------------
if(GAME_BUILD_CLIENT)
  if(GAME_USE_SYSTEM_SDL)
    find_package(SDL3 3.2 REQUIRED CONFIG)
    set(GAME_SDL_ORIGIN "system (${SDL3_VERSION})" CACHE INTERNAL "")
  else()
    set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        ${GAME_DEP_SDL_REF}
        ${GAME_DEP_SYSTEM})
    FetchContent_MakeAvailable(SDL3)
    set(GAME_SDL_ORIGIN "pinned ${GAME_DEP_SDL_REF}" CACHE INTERNAL "")
  endif()
else()
  set(GAME_SDL_ORIGIN "not needed (client disabled)" CACHE INTERNAL "")
endif()

# ---------------------------------------------------------------------------
# SDL_image — decodes the sprite atlas PNG. Client (and later the atlas tools)
# only; the server links none of it, same as SDL.
#
# Built statically with the stb_image backend and PNG-via-stb, so it pulls no
# external libpng/libjpeg and the client stays one self-contained executable.
# AVIF/JPG/TIFF/WEBP/JXL are off on purpose: AVIF in particular would vendor
# dav1d and aom, a huge build we do not want for a placeholder atlas.
# ---------------------------------------------------------------------------
if(GAME_BUILD_CLIENT)
  if(GAME_USE_SYSTEM_SDL)
    find_package(SDL3_image 3.2 REQUIRED CONFIG)
  else()
    set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES     OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TESTS       OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_INSTALL     OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_VENDORED    OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_BACKEND_STB ON  CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG         ON  CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG_LIBPNG  OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_JPG         OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_AVIF        OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TIF         OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_WEBP        OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_JXL         OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG        ${GAME_DEP_SDLIMG_REF}
        ${GAME_DEP_SYSTEM})
    FetchContent_MakeAvailable(SDL3_image)
  endif()
endif()

# ---------------------------------------------------------------------------
# doctest — unit tests.
# ---------------------------------------------------------------------------
if(GAME_BUILD_TESTS)
  set(DOCTEST_WITH_TESTS      OFF CACHE BOOL "" FORCE)
  set(DOCTEST_NO_INSTALL      ON  CACHE BOOL "" FORCE)
  FetchContent_Declare(doctest
      GIT_REPOSITORY https://github.com/doctest/doctest.git
      GIT_TAG        ${GAME_DEP_DOCTEST_REF}
      ${GAME_DEP_SYSTEM})
  FetchContent_MakeAvailable(doctest)
endif()
