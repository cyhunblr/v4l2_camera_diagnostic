#!/usr/bin/env sh
set -eu

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

if ! command -v shellcheck >/dev/null 2>&1; then
  echo "shellcheck check: shellcheck is required." >&2
  echo "Install it with:" >&2
  echo "  sudo apt-get install -y shellcheck" >&2
  exit 1
fi

if find .githooks scripts \( -name "*.sh" -o -path ".githooks/*" \) -type f ! -name "*.sample" -print0 \
    | xargs -0 shellcheck --shell=sh; then
  exit 0
fi

echo "" >&2
echo "shellcheck check: violations found (see above)." >&2
exit 1
