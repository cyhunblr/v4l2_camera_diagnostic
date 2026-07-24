#!/usr/bin/env sh
set -eu

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

if ! command -v markdownlint >/dev/null 2>&1; then
  echo "Markdown lint check: markdownlint-cli is required to match CI." >&2
  echo "Install it with:" >&2
  echo "  npm install -g markdownlint-cli" >&2
  exit 1
fi

if markdownlint '**/*.md'; then
  exit 0
fi

echo "" >&2
echo "Markdown lint check: violations found (see above)." >&2
echo "Fix with:" >&2
echo "  markdownlint --fix '**/*.md'" >&2
exit 1
