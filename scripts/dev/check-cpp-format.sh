#!/usr/bin/env sh
set -eu

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

if ! command -v clang-format-18 >/dev/null 2>&1; then
  echo "C++ format check: clang-format-18 is required to match CI." >&2
  echo "Install it with:" >&2
  echo "  sudo apt-get install -y clang-format-18" >&2
  exit 1
fi

staged_cpp=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|hpp)$' || true)

if [ -z "$staged_cpp" ]; then
  exit 0
fi

if echo "$staged_cpp" | xargs clang-format-18 --dry-run --Werror; then
  exit 0
fi

echo "" >&2
echo "C++ format check: formatting differs from clang-format-18." >&2
echo "Fix with:" >&2
echo "  echo \"$staged_cpp\" | xargs clang-format-18 -i" >&2
exit 1
