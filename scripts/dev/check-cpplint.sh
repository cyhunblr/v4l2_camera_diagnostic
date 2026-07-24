#!/usr/bin/env sh
set -eu

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

cpplint_bin=""
if command -v cpplint >/dev/null 2>&1; then
  cpplint_bin="cpplint"
elif [ -x "$HOME/.local/bin/cpplint" ]; then
  cpplint_bin="$HOME/.local/bin/cpplint"
fi

if [ -z "$cpplint_bin" ]; then
  echo "cpplint check: cpplint is required to match CI." >&2
  echo "Install it with:" >&2
  echo "  pip install --user cpplint" >&2
  exit 1
fi

if "$cpplint_bin" --recursive source/backend/; then
  exit 0
fi

echo "" >&2
echo "cpplint check: violations found (see above)." >&2
exit 1
