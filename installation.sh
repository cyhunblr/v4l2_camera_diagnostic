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
DEBUG=0
STEP_TOTAL=5
STEP_CURRENT=0
USE_COLOR=0
SPINNER_PID=""
STEP_LABEL=""

if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
  USE_COLOR=1
fi

for arg in "$@"; do
  case "${arg}" in
    --dry-run) DRY_RUN=1 ;;
    --debug) DEBUG=1 ;;
    --help|-h)
      cat <<USAGE
Usage: ./installation.sh [--dry-run] [--debug]

Builds and installs V4L2 Camera Diagnostic for the current user.

Options:
  --dry-run Preview the install without executing file-changing commands.
  --debug   Show every command and full command output.
USAGE
      exit 0
      ;;
    *)
      echo "Unknown option: ${arg}" >&2
      exit 2
      ;;
  esac
done

color() {
  local code="$1"
  shift
  if [[ "${USE_COLOR}" -eq 1 ]]; then
    printf '\033[%sm%s\033[0m' "${code}" "$*"
  else
    printf '%s' "$*"
  fi
}

step_begin() {
  STEP_CURRENT=$((STEP_CURRENT + 1))
  STEP_LABEL="$1"
  if can_spinner; then
    start_spinner
  else
    printf '[%d/%d] %s ... ' "${STEP_CURRENT}" "${STEP_TOTAL}" "${STEP_LABEL}"
  fi
}

step_ok() {
  step_done "$(color "32" "ok")"
}

step_skip() {
  step_done "$(color "33" "skipped")"
}

step_fail() {
  step_done "$(color "31" "failed")"
}

can_spinner() {
  [[ "${DEBUG}" -eq 0 && "${DRY_RUN}" -eq 0 && -t 1 ]]
}

start_spinner() {
  local frames=('-' '\' '|' '/')
  local frame=0
  while true; do
    printf '\r\033[K[%d/%d] %s %s' "${STEP_CURRENT}" "${STEP_TOTAL}" "${STEP_LABEL}" "${frames[frame]}"
    frame=$(((frame + 1) % ${#frames[@]}))
    sleep 0.12
  done &
  SPINNER_PID="$!"
}

stop_spinner() {
  if [[ -n "${SPINNER_PID}" ]]; then
    kill "${SPINNER_PID}" 2>/dev/null || true
    wait "${SPINNER_PID}" 2>/dev/null || true
    SPINNER_PID=""
  fi
}

step_done() {
  local status_text="$1"
  stop_spinner
  if can_spinner; then
    printf '\r\033[K[%d/%d] %s %s\n' "${STEP_CURRENT}" "${STEP_TOTAL}" "${STEP_LABEL}" "${status_text}"
  else
    printf '%s\n' "${status_text}"
  fi
}

# Runs a command. Under --debug, prints "+ command" and lets its real output
# flow through live. Otherwise captures stdout+stderr to a temp file and only
# dumps it (indented) if the command fails, so the happy path stays quiet.
run() {
  if [[ "${DEBUG}" -eq 1 ]]; then
    echo "+ $*"
    if [[ "${DRY_RUN}" -eq 0 ]]; then
      "$@"
    fi
    return
  fi

  if [[ "${DRY_RUN}" -eq 1 ]]; then
    echo "+ $*"
    return
  fi

  local output
  output="$(mktemp)"
  if "$@" >"${output}" 2>&1; then
    rm -f "${output}"
    return 0
  fi

  local status=$?
  echo
  echo "Command failed (${status}): $*" >&2
  sed 's/^/  /' "${output}" >&2
  rm -f "${output}"
  return "${status}"
}

need_command() {
  command -v "$1" >/dev/null 2>&1
}

deps_missing() {
  for cmd in cmake pkg-config xdg-open; do
    if ! need_command "${cmd}"; then
      return 0
    fi
  done
  if ! pkg-config --exists libmicrohttpd jsoncpp libgpiod 2>/dev/null; then
    return 0
  fi
  return 1
}

# Requests the sudo password up front, once, before any step runs — rather
# than letting it appear unpredictably wherever the first sudo-requiring
# command happens to live (apt-get for missing deps, or setcap at the end).
prime_sudo() {
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return
  fi
  local need_sudo=0
  if deps_missing && need_command apt-get; then
    need_sudo=1
  fi
  if need_command setcap; then
    need_sudo=1
  fi
  if [[ "${need_sudo}" -eq 1 ]]; then
    echo "This installer needs sudo to install missing system packages and/or"
    echo "grant the web app permission to read the kernel log (for Export DMESG)."
    sudo -v
    if [[ -t 1 ]]; then
      # Remove the previous terminal line (sudo password prompt row).
      # Works on common ANSI terminals.
      printf '\033[1A\033[2K\r'
    fi
    echo
  fi
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
  if ! need_command apt-get; then
    echo "apt-get was not found. Install dependencies manually: ${packages[*]}" >&2
    return
  fi
  run sudo apt-get update
  run sudo apt-get install -y "${packages[@]}"
}

ensure_dependencies() {
  if deps_missing; then
    install_deps_apt
  fi
}

ensure_nvm() {
  local nvm_dir="${NVM_DIR:-$HOME/.nvm}"

  if command -v nvm &>/dev/null; then
    return 0
  fi

  if [[ -s "${nvm_dir}/nvm.sh" ]]; then
    # shellcheck source=/dev/null
    \. "${nvm_dir}/nvm.sh"
    return 0
  fi

  local nvm_install_url="https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh"
  if [[ "${DRY_RUN}" -eq 0 ]]; then
    if need_command curl; then
      run bash -c "curl -fsSL '${nvm_install_url}' | bash"
    elif need_command wget; then
      run bash -c "wget -qO- '${nvm_install_url}' | bash"
    else
      echo "ERROR: Neither curl nor wget found. Cannot install nvm." >&2
      exit 1
    fi
  fi

  if [[ -s "${nvm_dir}/nvm.sh" ]]; then
    # shellcheck source=/dev/null
    \. "${nvm_dir}/nvm.sh"
    return 0
  fi

  echo "ERROR: nvm installation failed." >&2
  exit 1
}

build_frontend() {
  if [[ ! -f "${ROOT_DIR}/source/frontend/package.json" ]]; then
    return 2
  fi

  ensure_nvm

  local node_major
  node_major=$(node -e "process.stdout.write(String(process.versions.node.split('.')[0]))" 2>/dev/null || echo "0")

  if [[ "${node_major}" -lt 12 ]]; then
    nvm install 18 >/dev/null
    nvm use 18 >/dev/null
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
  if [[ "${DEBUG}" -eq 1 ]]; then
    echo "+ write desktop entry: ${desktop_file}"
  fi
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

# Grants CAP_SYSLOG so the web binary can read dmesg even when
# kernel.dmesg_restrict=1 (common on hardened systems). Returns 2 (skipped)
# if setcap isn't available, 1 (failed, non-fatal) if granting it fails.
grant_dmesg_capability() {
  if ! need_command setcap; then
    return 2
  fi
  if [[ "${DEBUG}" -eq 1 ]]; then
    echo "+ sudo setcap cap_syslog+ep ${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic-web"
  fi
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return
  fi
  if ! sudo setcap cap_syslog+ep "${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic-web" 2>/dev/null; then
    echo "Warning: could not set CAP_SYSLOG. Export DMESG may fail if kernel.dmesg_restrict=1." >&2
    return 1
  fi
}

path_notice() {
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

run_step() {
  local label="$1"
  shift
  step_begin "${label}"
  local status=0
  if "$@"; then
    status=0
  else
    status=$?
  fi
  if [[ "${status}" -eq 0 ]]; then
    step_ok
  elif [[ "${status}" -eq 2 ]]; then
    step_skip
  else
    step_fail
    return "${status}"
  fi
}

if [[ "${DEBUG}" -eq 1 ]]; then
  echo "V4L2 Camera Diagnostic installer [debug]"
else
  echo "V4L2 Camera Diagnostic installer"
fi
echo

prime_sudo

run_step "Checking/installing system dependencies" ensure_dependencies
run_step "Building web UI" build_frontend
run_step "Building C++ project" build_cpp
run_step "Installing files" install_files

if ! run_step "Granting CAP_SYSLOG for Export DMESG" grant_dmesg_capability; then
  echo
  echo "CAP_SYSLOG could not be granted, so Export DMESG may not work." >&2
  echo "Rolling back this install with ./uninstallation.sh --yes ..." >&2
  "${ROOT_DIR}/uninstallation.sh" --yes
  exit 1
fi

path_notice

echo
if [[ "${DRY_RUN}" -eq 1 ]]; then
  echo "Dry run complete. No files were changed."
else
  echo "Installation complete."
  echo "Launch with: ${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic-web"
fi
