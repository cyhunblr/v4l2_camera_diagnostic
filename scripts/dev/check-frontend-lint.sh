#!/usr/bin/env sh
set -eu

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root/source/frontend"

if ! command -v npm >/dev/null 2>&1; then
  echo "Frontend lint check: Node.js (npm) is required to match CI." >&2
  echo "Install it with:" >&2
  echo "  sudo apt-get install -y nodejs npm" >&2
  exit 1
fi

if [ ! -d node_modules ]; then
  echo "Frontend lint check: dependencies not installed." >&2
  echo "Install them with:" >&2
  echo "  (cd source/frontend && npm ci)" >&2
  exit 1
fi

cd "$repo_root"
staged_fe=$(git diff --cached --name-only --diff-filter=ACM | grep -E '^source/frontend/src/.*\.(ts|tsx|js|jsx)$' || true)

if [ -z "$staged_fe" ]; then
  exit 0
fi

cd "$repo_root/source/frontend"
fe_files=$(echo "$staged_fe" | sed 's|^source/frontend/||')

if echo "$fe_files" | xargs npx eslint --max-warnings=0; then
  exit 0
fi

echo "" >&2
echo "Frontend lint check: ESLint violations found (see above)." >&2
exit 1
