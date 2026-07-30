#!/usr/bin/env bash
#
# Enforces the module boundaries described in docs/architecture.md.
#
# This exists because layering rots by accident, not by decision. Someone needs a
# timestamp inside a movement rule, reaches for <chrono>, and six months later the
# simulation cannot be tested without a wall clock and the headless server pulls in
# SDL. A grep in CI is a cheap way to make that impossible.
set -uo pipefail

cd "$(dirname "$0")/.."

failures=0

# Matches only real include directives, so a mention in a comment does not trip it.
forbid() {
  local dir="$1"; shift
  local reason="$1"; shift

  for pattern in "$@"; do
    local matches
    matches="$(grep -rnE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"]${pattern}" "$dir" 2>/dev/null || true)"
    if [[ -n "$matches" ]]; then
      printf '\033[1;31mLAYERING VIOLATION\033[0m  %s\n' "$dir"
      printf '  forbidden include: %s\n' "$pattern"
      printf '  reason: %s\n' "$reason"
      printf '%s\n' "$matches" | sed 's/^/    /'
      echo
      failures=$((failures + 1))
    fi
  done
}

echo "checking module boundaries..."

# --- src/sim -------------------------------------------------------------------
# The strictest layer. It must stay drivable from a unit test and linkable into a
# headless server, with no ambient state at all.
forbid src/sim \
  "the simulation must run headless; the server links no SDL" \
  "SDL3/" "SDL2/" "SDL\.h"

forbid src/sim \
  "the simulation must not touch the network; net/ depends on sim, not the reverse" \
  "enet/" "sys/socket\.h" "winsock" "net/"

forbid src/sim \
  "the simulation must not touch the filesystem; asset and save I/O belong to platform/" \
  "fstream" "iostream" "filesystem" "cstdio"

forbid src/sim \
  "the simulation is advanced one fixed tick at a time by its caller and must never read a clock" \
  "chrono" "ctime" "core/time\.hpp"

forbid src/sim \
  "std distributions are not specified to be identical across implementations; use sim::Rng" \
  "random"

forbid src/sim \
  "the simulation must not know that rendering or platforms exist" \
  "client/" "platform/"

forbid src/sim \
  "the simulation consumes a registry it is handed; it never opens a database" \
  "sqlite3\.h" "store/"

# --- src/net -------------------------------------------------------------------
forbid src/net \
  "the wire format and transport are used by the headless server" \
  "SDL3/" "SDL2/"

forbid src/net \
  "net/ is below the client, not beside it" \
  "client/" "platform/"

forbid src/net \
  "the wire format has no business touching storage" \
  "sqlite3\.h" "store/"

# --- src/client ----------------------------------------------------------------
# The one that is easy to break and expensive to discover. On Android and iOS the
# client's data lives inside the application package and is not a file, while
# SQLite needs a real path to open and seek. A client that opens a database works
# on a desktop and fails on a device, months later. Content reaches the client as a
# baked blob through platform::vfs — see docs/content.md.
forbid src/client \
  "the client must not open a database; its content arrives baked, via platform::vfs" \
  "sqlite3\.h" "store/"

# --- src/core ------------------------------------------------------------------
forbid src/core \
  "core is the bottom of the stack and depends on nothing in this project" \
  "SDL3/" "SDL2/" "enet/" "sim/" "net/" "client/" "platform/" "store/" "sqlite3\.h"

# --- src/server ----------------------------------------------------------------
forbid src/server \
  "the server must build on a machine with no graphics libraries installed" \
  "SDL3/" "SDL2/"

forbid src/server \
  "the server must not reach into client code" \
  "client/"

# --- link-level check ----------------------------------------------------------
# Catches the case where an include is clean but a CMakeLists links SDL anyway.
for module in core sim net server; do
  if grep -nE "SDL3::" "src/${module}/CMakeLists.txt" >/dev/null 2>&1; then
    printf '\033[1;31mLAYERING VIOLATION\033[0m  src/%s/CMakeLists.txt links SDL3\n\n' "$module"
    failures=$((failures + 1))
  fi
done

# The same check for storage: an include can be clean while a CMakeLists links
# SQLite anyway, and the client linking it is the mistake that only shows up on a
# phone. platform/ is included because it is the client's asset path.
for module in core sim net client platform; do
  file="src/${module}/CMakeLists.txt"
  [[ -f "$file" ]] || continue
  if grep -nE "sqlite3|game_store" "$file" >/dev/null 2>&1; then
    printf '\033[1;31mLAYERING VIOLATION\033[0m  %s links SQLite or game_store\n' "$file"
    printf '  reason: only the server and the offline tools may; see docs/content.md\n\n'
    failures=$((failures + 1))
  fi
done

# tests/ links the simulation and the wire format; if it needs SDL, something has
# leaked out of the client layer.
if grep -nE "SDL3::" tests/CMakeLists.txt >/dev/null 2>&1; then
  printf '\033[1;31mLAYERING VIOLATION\033[0m  tests/CMakeLists.txt links SDL3\n'
  printf '  the rules and the wire format must be testable without a window\n\n'
  failures=$((failures + 1))
fi

if [[ "$failures" -gt 0 ]]; then
  printf '\033[1;31m%d layering violation(s).\033[0m See docs/architecture.md.\n' "$failures"
  exit 1
fi

printf '\033[1;32mok\033[0m  module boundaries are intact\n'
