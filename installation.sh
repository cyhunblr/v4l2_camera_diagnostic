#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
INSTALL_PREFIX="${HOME}/.local"
APP_SHARE="${INSTALL_PREFIX}/share/v4l2-camera-diagnostic"
WEB_SHARE="${APP_SHARE}/web"
DOC_SHARE="${APP_SHARE}/docs"
DESKTOP_DIR="${INSTALL_PREFIX}/share/applications"
DRY_RUN=0

for arg in "$@"; do
  case "${arg}" in
    --dry-run) DRY_RUN=1 ;;
    --help|-h)
      cat <<USAGE
Usage: ./installation.sh [--dry-run]

Builds and installs V4L2 Camera Diagnostic for the current user.
USAGE
      exit 0
      ;;
    *)
      echo "Unknown option: ${arg}" >&2
      exit 2
      ;;
  esac
done

run() {
  echo "+ $*"
  if [[ "${DRY_RUN}" -eq 0 ]]; then
    "$@"
  fi
}

need_command() {
  command -v "$1" >/dev/null 2>&1
}

install_deps_apt() {
  local packages=(
    build-essential
    cmake
    pkg-config
    libgpiod-dev
    libmicrohttpd-dev
    libjsoncpp-dev
    xdg-utils
  )
  echo "Checking Debian/Ubuntu dependencies..."
  if ! need_command apt-get; then
    echo "apt-get was not found. Install dependencies manually: ${packages[*]}" >&2
    return
  fi
  run sudo apt-get update
  run sudo apt-get install -y "${packages[@]}"
}

ensure_dependencies() {
  local missing=0
  for cmd in cmake pkg-config xdg-open; do
    if ! need_command "${cmd}"; then
      missing=1
    fi
  done

  if ! pkg-config --exists libmicrohttpd jsoncpp libgpiod 2>/dev/null; then
    missing=1
  fi

  if [[ "${missing}" -eq 1 ]]; then
    install_deps_apt
  fi
}

ensure_nvm() {
  local nvm_dir="${NVM_DIR:-$HOME/.nvm}"

  # Already loaded.
  if command -v nvm &>/dev/null; then
    return 0
  fi

  # Source nvm if it exists but hasn't been loaded yet.
  if [[ -s "${nvm_dir}/nvm.sh" ]]; then
    # shellcheck source=/dev/null
    \. "${nvm_dir}/nvm.sh"
    return 0
  fi

  # Install nvm from the official installer.
  echo "nvm not found. Installing nvm..."
  local nvm_install_url="https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh"
  echo "+ curl -fsSL ${nvm_install_url} | bash"
  if [[ "${DRY_RUN}" -eq 0 ]]; then
    if need_command curl; then
      curl -fsSL "${nvm_install_url}" | bash
    elif need_command wget; then
      wget -qO- "${nvm_install_url}" | bash
    else
      echo "ERROR: Neither curl nor wget found. Cannot install nvm." >&2
      exit 1
    fi
  fi

  # Source nvm after installation.
  if [[ -s "${nvm_dir}/nvm.sh" ]]; then
    # shellcheck source=/dev/null
    \. "${nvm_dir}/nvm.sh"
    return 0
  fi

  echo "ERROR: nvm installation failed." >&2
  exit 1
}

activate_nvm() {
  local nvm_dir="${NVM_DIR:-$HOME/.nvm}"
  if [[ -s "${nvm_dir}/nvm.sh" ]]; then
    # shellcheck source=/dev/null
    \. "${nvm_dir}/nvm.sh"
    return 0
  fi
  return 1
}

build_frontend() {
  local dist_dir="${ROOT_DIR}/source/frontend/dist"

  if [[ ! -f "${ROOT_DIR}/source/frontend/package.json" ]]; then
    return
  fi

  # Ensure nvm is installed and loaded so we can use a compatible Node version.
  ensure_nvm

  local node_major
  node_major=$(node -e "process.stdout.write(String(process.versions.node.split('.')[0]))" 2>/dev/null || echo "0")

  if [[ "${node_major}" -lt 12 ]]; then
    # System Node is too old. Use nvm to get a compatible version.
    echo "Node ${node_major} detected (< 12): activating Node 18 via nvm..."
    nvm install 18 >/dev/null
    nvm use 18
  fi

  pushd "${ROOT_DIR}/source/frontend" >/dev/null
  if [[ -f package-lock.json ]]; then
    run npm ci
  else
    run npm install
  fi
  run npm run build
  popd >/dev/null
}

build_cpp() {
  run cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
  run cmake --build "${BUILD_DIR}" --parallel
}

install_files() {
  run mkdir -p "${INSTALL_PREFIX}/bin" "${WEB_SHARE}" "${DOC_SHARE}" "${DESKTOP_DIR}"
  run install -m 0755 "${BUILD_DIR}/v4l2-camera-diagnostic" "${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic"
  run install -m 0755 "${BUILD_DIR}/v4l2-camera-diagnostic-web" "${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic-web"

  run rm -rf "${WEB_SHARE}"
  run mkdir -p "${WEB_SHARE}"
  run cp -R "${ROOT_DIR}/source/frontend/dist/." "${WEB_SHARE}/"

  run rm -rf "${DOC_SHARE}"
  run mkdir -p "${DOC_SHARE}"
  run cp -R "${ROOT_DIR}/docs/." "${DOC_SHARE}/"

  local desktop_file="${DESKTOP_DIR}/v4l2-camera-diagnostic.desktop"
  echo "Writing desktop entry: ${desktop_file}"
  if [[ "${DRY_RUN}" -eq 0 ]]; then
    cat >"${desktop_file}" <<DESKTOP
[Desktop Entry]
Type=Application
Name=V4L2 Camera Diagnostic
Comment=Local web UI for V4L2 camera diagnostics
Exec=${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic-web
Terminal=false
Categories=Development;Utility;
DESKTOP
    chmod 0644 "${desktop_file}"
  fi
}

ensure_path_notice() {
  case ":${PATH}:" in
    *":${INSTALL_PREFIX}/bin:"*) ;;
    *)
      echo
      echo "Notice: ${INSTALL_PREFIX}/bin is not in PATH."
      echo "Add this to your shell profile:"
      echo "  export PATH=\"${INSTALL_PREFIX}/bin:\$PATH\""
      ;;
  esac
}

ensure_dependencies
build_frontend
build_cpp
install_files
ensure_path_notice

echo
echo "Installation complete."
echo "Launch with: ${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic-web"
