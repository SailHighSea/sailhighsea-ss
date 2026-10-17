#!/usr/bin/env bash
# Prints the release tag for the current ref. Needs full git history and tags.
#   main        -> next stable patch      v0.0.1, v0.0.2, ...
#   dev         -> pre-release of that    v0.0.1-dev-1, v0.0.1-dev-2, ... (numbering restarts after each stable release)
#   v* tag push -> the tag itself
#   anything else (PRs, manual runs) -> v0.0.0-local
set -euo pipefail

REF="${1:-${GITHUB_REF:-}}"
MAJOR_MINOR="0.0"          # bump by hand for a new minor/major line (e.g. "0.1" gives v0.1.1, v0.1.2, ...)

case "$REF" in
  refs/tags/v*) echo "${REF#refs/tags/}"; exit 0 ;;
  refs/heads/main|refs/heads/dev) ;;
  *) echo "v0.0.0-local"; exit 0 ;;
esac

# highest stable patch released so far on this line (0 when there is none), so the first release is .1
last_patch=$(git tag -l "v${MAJOR_MINOR}.*" | grep -E "^v${MAJOR_MINOR//./\\.}\.[0-9]+$" \
             | sed -E "s/^v${MAJOR_MINOR//./\\.}\.//" | sort -n | tail -1 || true)
next_patch=$(( ${last_patch:-0} + 1 ))
stable="${MAJOR_MINOR}.${next_patch}"

if [ "$REF" = "refs/heads/main" ]; then
  echo "v${stable}"
else
  n=$(git tag -l "v${stable}-dev-*" | wc -l | tr -d ' ')
  echo "v${stable}-dev-$((n + 1))"
fi
