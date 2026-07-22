#!/usr/bin/env sh
set -eu

repo_root=$(git rev-parse --show-toplevel 2>/dev/null || pwd)
cd "$repo_root"

have() {
  command -v "$1" >/dev/null 2>&1
}

install_apt_packages() {
  if ! have apt-get; then
    echo "setup-dev-env: apt-get not found; install manually: $*" >&2
    return 1
  fi

  if [ "$(id -u)" -eq 0 ]; then
    apt-get update
    apt-get install -y "$@"
  elif have sudo; then
    sudo apt-get update
    sudo apt-get install -y "$@"
  else
    echo "setup-dev-env: sudo not found; install manually: $*" >&2
    return 1
  fi
}

pip_cmd=""
if have pip3; then
  pip_cmd="pip3"
elif have pip; then
  pip_cmd="pip"
fi

missing_apt=""
if ! have clang-format-18; then
  missing_apt="$missing_apt clang-format-18"
fi
if ! have python3; then
  missing_apt="$missing_apt python3"
fi
if [ -z "$pip_cmd" ]; then
  missing_apt="$missing_apt python3-pip"
fi
if ! have node; then
  missing_apt="$missing_apt nodejs"
fi
if ! have npm; then
  missing_apt="$missing_apt npm"
fi

if [ -n "$missing_apt" ]; then
  # shellcheck disable=SC2086
  install_apt_packages $missing_apt
fi

if [ -z "$pip_cmd" ]; then
  if have pip3; then
    pip_cmd="pip3"
  elif have pip; then
    pip_cmd="pip"
  fi
fi

if [ -n "$pip_cmd" ]; then
  "$pip_cmd" install --user cpplint
else
  echo "setup-dev-env: pip is unavailable; install cpplint manually." >&2
fi

if have npm; then
  if ! have markdownlint; then
    npm install -g markdownlint-cli
  fi
  if [ -f source/frontend/package-lock.json ]; then
    (cd source/frontend && npm ci)
  fi
else
  echo "setup-dev-env: npm is unavailable; frontend dependencies and markdownlint were not installed." >&2
fi

scripts/install-git-hooks.sh

echo "Developer environment setup complete."
