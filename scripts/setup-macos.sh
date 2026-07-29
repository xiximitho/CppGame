#!/usr/bin/env bash
#
# Installs the system prerequisites for building this project on macOS.
#
# SDL3 uses the system frameworks (Cocoa, Metal, CoreAudio) that ship with Xcode,
# so there are no -dev packages to install like on Linux.
#
# Safe to re-run.
set -euo pipefail

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mxx\033[0m %s\n' "$*" >&2; exit 1; }

if ! xcode-select -p >/dev/null 2>&1; then
  info "Installing the Xcode command line tools"
  warn "A GUI dialog will open. Finish it, then run this script again."
  xcode-select --install
  exit 0
fi
info "Xcode command line tools: $(xcode-select -p)"

if ! command -v brew >/dev/null 2>&1; then
  warn "Homebrew is not installed."
  warn "Install it from https://brew.sh and run this script again, or install"
  warn "cmake (>= 3.25) and ninja by hand."
  exit 1
fi

info "Installing build tools via Homebrew"
# `brew install` on an already-installed formula is a no-op warning, not an error.
brew install cmake ninja || true

echo
info "Versions installed:"
cmake --version | head -1
ninja --version | sed 's/^/ninja /'
clang++ --version | head -1

cmake_version="$(cmake --version | head -1 | awk '{print $3}')"
required="3.25.0"
if [[ "$(printf '%s\n%s\n' "$required" "$cmake_version" | sort -V | head -1)" != "$required" ]]; then
  die "cmake $cmake_version is too old; 3.25 or newer is required"
fi

# Both architectures build; CMAKE_OSX_DEPLOYMENT_TARGET is pinned to 12.0 in the
# top-level CMakeLists so binaries run on older machines than the build host.
info "Host architecture: $(uname -m)"

echo
info "Done. Next:"
echo "    cmake --preset debug"
echo "    cmake --build --preset debug"
echo "    ctest --preset debug"
echo "    ./build/debug/bin/game_client"
