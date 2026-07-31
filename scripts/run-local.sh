#!/usr/bin/env bash
#
# Starts a local server and connects one client to it.
#
# The server is killed when the client exits, including on Ctrl-C, so this never
# leaves an orphan process holding UDP 7777 — which otherwise makes the next run
# fail with "could not bind UDP port".
#
# usage: ./scripts/run-local.sh [preset] [-- extra client args]
#   ./scripts/run-local.sh
#   ./scripts/run-local.sh release
#   ./scripts/run-local.sh debug -- --name felipe --zoom 3
#   GAME_MAP=torre ./scripts/run-local.sh          # a map with stairs
#
# GAME_MAP picks the SERVER's map, because in network play the client's is
# irrelevant — it receives the server's in chunks. Without it the server loads
# dungeon.txt, which has one floor, so there is no stair anywhere in the world.
set -euo pipefail

cd "$(dirname "$0")/.."

PRESET="debug"
if [[ $# -gt 0 && "$1" != "--" ]]; then
  PRESET="$1"
  shift
fi
[[ "${1:-}" == "--" ]] && shift

BIN="build/${PRESET}/bin"
PORT="${GAME_PORT:-7777}"
MAP="${GAME_MAP:-}"

if [[ ! -x "${BIN}/game_server" || ! -x "${BIN}/game_client" ]]; then
  echo "binaries not found in ${BIN}" >&2
  echo "build first:  cmake --preset ${PRESET} && cmake --build --preset ${PRESET}" >&2
  exit 1
fi

SERVER_PID=""
cleanup() {
  if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    echo
    echo "stopping server (pid $SERVER_PID)"
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

SERVER_ARGS=(--port "${PORT}")
if [[ -n "${MAP}" ]]; then
  # A bare name is resolved here rather than by the server, which takes a path
  # relative to the working directory and nothing else.
  [[ -f "${MAP}" ]] || MAP="assets/maps/${MAP%.txt}.txt"
  SERVER_ARGS+=(--map "${MAP}")
  echo "server map: ${MAP}"
fi

echo "starting server on port ${PORT}"
"${BIN}/game_server" "${SERVER_ARGS[@]}" &
SERVER_PID=$!

# Give the socket a moment to bind, then confirm the server is actually alive
# rather than letting the client fail with a confusing connection error.
sleep 0.5
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
  echo "server exited immediately; is port ${PORT} already in use?" >&2
  exit 1
fi

echo "starting client"
"${BIN}/game_client" --connect "127.0.0.1:${PORT}" "$@"
