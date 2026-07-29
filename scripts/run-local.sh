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

echo "starting server on port ${PORT}"
"${BIN}/game_server" --port "${PORT}" &
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
