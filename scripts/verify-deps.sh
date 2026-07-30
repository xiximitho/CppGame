#!/usr/bin/env bash
#
# Checks that every pinned dependency SHA still exists upstream and reports which
# tag it corresponds to.
#
# Run this after editing cmake/Dependencies.cmake. It catches the two mistakes that
# are easy to make and annoying to debug: a typo in a SHA (the build fails with an
# opaque git error at configure time) and a SHA that no longer matches the version
# written in the comment beside it.
#
# Requires network access. Does not build anything.
set -uo pipefail

cd "$(dirname "$0")/.."

DEPS_FILE="cmake/Dependencies.cmake"
[[ -f "$DEPS_FILE" ]] || { echo "cannot find $DEPS_FILE" >&2; exit 1; }

# name : cmake variable : repository
DEPS=(
  "SDL3:GAME_DEP_SDL_REF:libsdl-org/SDL"
  "SDL3_image:GAME_DEP_SDLIMG_REF:libsdl-org/SDL_image"
  "EnTT:GAME_DEP_ENTT_REF:skypjack/entt"
  "glm:GAME_DEP_GLM_REF:g-truc/glm"
  "ENet:GAME_DEP_ENET_REF:lsalzman/enet"
  "doctest:GAME_DEP_DOCTEST_REF:doctest/doctest"
)

failures=0

printf '%-10s %-42s %-18s %s\n' "DEP" "PINNED SHA" "UPSTREAM TAG" "COMMENT SAYS"
printf '%.0s-' {1..100}; echo

for entry in "${DEPS[@]}"; do
  IFS=':' read -r name var repo <<< "$entry"

  line="$(grep -E "^set\(${var} " "$DEPS_FILE" || true)"
  if [[ -z "$line" ]]; then
    printf '\033[1;31m%-10s not found in %s\033[0m\n' "$name" "$DEPS_FILE"
    failures=$((failures + 1))
    continue
  fi

  sha="$(printf '%s' "$line" | sed -nE 's/.*"([0-9a-f]{40})".*/\1/p')"
  comment="$(printf '%s' "$line" | sed -nE 's/.*#[[:space:]]*(.*)$/\1/p')"

  if [[ -z "$sha" ]]; then
    printf '\033[1;31m%-10s pin is not a 40-character SHA — tags can be moved, use a SHA\033[0m\n' "$name"
    failures=$((failures + 1))
    continue
  fi

  # Resolve the SHA back to whichever tag points at it.
  tag="$(timeout 30 git ls-remote --tags --refs "https://github.com/${repo}.git" 2>/dev/null \
        | awk -v s="$sha" '$1 == s {print $2}' | sed 's|refs/tags/||' | head -1)"

  if [[ -z "$tag" ]]; then
    # Not on a tag is legitimate (pinning a fix that has no release yet), but the
    # commit still has to exist.
    if timeout 30 git ls-remote "https://github.com/${repo}.git" 2>/dev/null | grep -q "$sha"; then
      tag="(untagged, exists)"
    else
      printf '\033[1;31m%-10s %s  SHA NOT FOUND UPSTREAM\033[0m\n' "$name" "$sha"
      failures=$((failures + 1))
      continue
    fi
  fi

  mismatch=""
  if [[ -n "$comment" && "$tag" != "(untagged, exists)" && "$tag" != "$comment" ]]; then
    mismatch=" <-- MISMATCH"
    failures=$((failures + 1))
  fi

  printf '%-10s %-42s %-18s %s%s\n' "$name" "$sha" "$tag" "${comment:-—}" "$mismatch"
done

# ---------------------------------------------------------------------------
# SQLite is pinned differently, and on purpose: it is not developed on GitHub and
# the amalgamation is generated rather than checked in, so there is no commit to
# point at. The official release zip is pinned by its SHA3-256 instead — which is
# immutable in exactly the way a tag is not. Verifying it means re-downloading and
# re-hashing, so this check does real work rather than asking an API.
# ---------------------------------------------------------------------------
echo
printf 'SQLite (pinned by artefact hash, not a commit)\n'
printf '%.0s-' {1..100}; echo

sqlite_url="$(sed -nE 's/^[[:space:]]*"(https:\/\/sqlite\.org\/[^"]+)".*/\1/p' "$DEPS_FILE" | head -1)"
sqlite_hash="$(grep -A1 -E '^set\(GAME_DEP_SQLITE_SHA3' "$DEPS_FILE" \
              | sed -nE 's/.*"([0-9a-f]{64})".*/\1/p' | head -1)"
sqlite_version="$(sed -nE 's/^set\(GAME_DEP_SQLITE_VERSION[[:space:]]+"([^"]+)".*/\1/p' "$DEPS_FILE")"

if [[ -z "$sqlite_url" || -z "$sqlite_hash" ]]; then
  printf '\033[1;31mSQLite    could not read URL or SHA3-256 from %s\033[0m\n' "$DEPS_FILE"
  failures=$((failures + 1))
elif ! command -v sha3sum >/dev/null 2>&1 && ! command -v python3 >/dev/null 2>&1; then
  printf '\033[1;33mSQLite    skipped: need sha3sum or python3 to hash SHA3-256\033[0m\n'
else
  tmp="$(mktemp)"
  trap 'rm -f "$tmp"' EXIT
  if ! timeout 120 curl -sL -o "$tmp" "$sqlite_url"; then
    printf '\033[1;31mSQLite    download failed: %s\033[0m\n' "$sqlite_url"
    failures=$((failures + 1))
  else
    if command -v sha3sum >/dev/null 2>&1; then
      got="$(sha3sum -a 256 "$tmp" | awk '{print $1}')"
    else
      got="$(python3 -c "import hashlib,sys;print(hashlib.sha3_256(open(sys.argv[1],'rb').read()).hexdigest())" "$tmp")"
    fi
    if [[ "$got" == "$sqlite_hash" ]]; then
      printf '%-10s %-42s %s\n' "SQLite" "${sqlite_hash:0:40}…" "${sqlite_version} (hash matches)"
    else
      printf '\033[1;31mSQLite    HASH MISMATCH\033[0m\n'
      printf '  pinned:     %s\n  downloaded: %s\n' "$sqlite_hash" "$got"
      printf '  Either the pin is wrong or the artefact changed. Do not "fix" this\n'
      printf '  by pasting the new hash without knowing why it moved.\n'
      failures=$((failures + 1))
    fi
  fi
fi

echo
if [[ "$failures" -gt 0 ]]; then
  printf '\033[1;31m%d problem(s).\033[0m Fix the SHA or the version comment so they agree.\n' "$failures"
  exit 1
fi
printf '\033[1;32mok\033[0m  every pin resolves and matches its version comment\n'
