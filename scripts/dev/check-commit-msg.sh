#!/usr/bin/env sh
set -eu

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

msg_file=$1

if ! command -v npx >/dev/null 2>&1; then
  echo "Commit message check: Node.js (npx) is required to match CI." >&2
  echo "Install it with:" >&2
  echo "  sudo apt-get install -y nodejs npm" >&2
  exit 1
fi

# CI's wagoid/commitlint-github-action looks for ./commitlint.config.mjs by
# default; this repo has no such file, so the action falls back to the
# standard @commitlint/config-conventional preset. Mirror that explicitly
# here so local and CI checks agree.
if npx --yes commitlint --extends @commitlint/config-conventional --edit "$msg_file"; then
  exit 0
fi

echo "" >&2
echo "Commit message check: violates @commitlint/config-conventional (see above)." >&2
echo "Use Conventional Commits format, e.g. 'fix: correct GPIO trigger fallback path'." >&2
exit 1
