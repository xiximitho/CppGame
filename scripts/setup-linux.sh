#!/usr/bin/env bash
#
# Installs the system prerequisites for building this project on Linux.
#
# Only build tools and the -dev headers SDL3 needs to compile against. The game's
# own dependencies (SDL3, EnTT, glm, ENet, doctest) are NOT installed here: they
# are fetched and pinned by the build itself. See docs/dependencies.md.
#
# Safe to re-run.
set -euo pipefail

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mxx\033[0m %s\n' "$*" >&2; exit 1; }

SUDO=""
if [[ "$(id -u)" -ne 0 ]]; then
  command -v sudo >/dev/null 2>&1 || die "not root and sudo is not available"
  SUDO="sudo"
fi

if command -v pacman >/dev/null 2>&1; then
  info "Arch-based system detected (pacman)"
  # --needed skips anything already installed, which is what makes this re-runnable.
  $SUDO pacman -S --needed --noconfirm \
    base-devel cmake ninja git pkgconf \
    wayland wayland-protocols libxkbcommon libdecor \
    libx11 libxext libxrandr libxcursor libxi libxfixes libxss \
    mesa libglvnd \
    alsa-lib libpulse dbus

elif command -v apt-get >/dev/null 2>&1; then
  info "Debian-based system detected (apt)"
  $SUDO apt-get update
  $SUDO apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git pkg-config \
    libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
    libxfixes-dev libxss-dev \
    libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev \
    libasound2-dev libpulse-dev libdbus-1-dev libudev-dev

elif command -v dnf >/dev/null 2>&1; then
  info "Fedora-based system detected (dnf)"
  $SUDO dnf install -y \
    gcc gcc-c++ make cmake ninja-build git pkgconf-pkg-config \
    wayland-devel wayland-protocols-devel libxkbcommon-devel libdecor-devel \
    libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel \
    libXfixes-devel libXScrnSaver-devel \
    mesa-libGL-devel mesa-libEGL-devel mesa-libGLES-devel \
    alsa-lib-devel pulseaudio-libs-devel dbus-devel systemd-devel

elif command -v zypper >/dev/null 2>&1; then
  info "openSUSE detected (zypper)"
  $SUDO zypper install -y \
    gcc gcc-c++ make cmake ninja git pkg-config \
    wayland-devel wayland-protocols-devel libxkbcommon-devel \
    libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel \
    Mesa-libGL-devel Mesa-libEGL-devel \
    alsa-devel libpulse-devel dbus-1-devel

else
  warn "Unrecognised package manager."
  warn "Install by hand: a C++20 compiler, cmake >= 3.25, ninja, git,"
  warn "plus the Wayland/X11/OpenGL/ALSA development headers."
  exit 1
fi

echo
info "Versions installed:"
cmake --version | head -1
ninja --version | sed 's/^/ninja /'
(g++ --version 2>/dev/null || clang++ --version 2>/dev/null) | head -1

# CMake 3.25 is required for the SYSTEM option on FetchContent_Declare, which is
# what keeps third-party warnings out of our -Werror build.
cmake_version="$(cmake --version | head -1 | awk '{print $3}')"
required="3.25.0"
if [[ "$(printf '%s\n%s\n' "$required" "$cmake_version" | sort -V | head -1)" != "$required" ]]; then
  die "cmake $cmake_version is too old; 3.25 or newer is required"
fi

echo
info "Done. Next:"
echo "    cmake --preset debug"
echo "    cmake --build --preset debug"
echo "    ctest --preset debug"
echo "    ./build/debug/bin/game_client"
