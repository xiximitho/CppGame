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

echo
if [[ "$failures" -gt 0 ]]; then
  printf '\033[1;31m%d problem(s).\033[0m Fix the SHA or the version comment so they agree.\n' "$failures"
  exit 1
fi
printf '\033[1;32mok\033[0m  every pin resolves and matches its version comment\n'
