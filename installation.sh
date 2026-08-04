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

# Decisions collected from the user before any step runs; they determine
# whether sudo is needed at all.
DO_INSTALL_DEPS=0
DO_JOIN_ADM=0
SUDO_PRIMED=0
# Why Export DMESG may not work, when that is already known up front. Printed
# in the closing summary so the reason is visible without re-running.
KERNEL_LOG_NOTE=""

APT_PACKAGES=(
  build-essential
  cmake
  pkg-config
  libgpiod-dev
  libmicrohttpd-dev
  libjsoncpp-dev
  xdg-utils
)

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

ask_yes_no() {
  local prompt="$1"
  local default="$2"
  local answer

  # Non-interactive (piped, CI): take the default rather than blocking on a
  # read that will never be answered.
  if [[ ! -t 0 ]]; then
    [[ "${default}" == "yes" ]]
    return
  fi

  while true; do
    if [[ "${default}" == "yes" ]]; then
      read -r -p "${prompt} [Y/n] " answer
      answer="${answer:-y}"
    else
      read -r -p "${prompt} [y/N] " answer
      answer="${answer:-n}"
    fi

    case "${answer}" in
      y | Y | yes | YES) return 0 ;;
      n | N | no | NO) return 1 ;;
      *) echo "Please answer yes or no." ;;
    esac
  done
}

# True when this user can already read the kernel log through journalctl.
# Group membership is what grants that access — the journal directories carry
# an ACL for "adm" on Debian/Ubuntu — so no privilege is needed to check it.
has_kernel_log_access() {
  id -nG 2>/dev/null | tr ' ' '\n' | grep -qx -e adm -e systemd-journal
}

# Asks every question that needs an answer before any work starts, so the run
# is "answer, then walk away" rather than a prompt appearing between steps.
# Only after the answers are known can we tell whether sudo is needed at all.
collect_choices() {
  if deps_missing; then
    if ! need_command apt-get; then
      echo "Some dependencies are missing but apt-get was not found."
      echo "Install them manually: ${APT_PACKAGES[*]}"
      echo
    elif ask_yes_no "System dependencies are missing. Install them with apt-get?" "yes"; then
      DO_INSTALL_DEPS=1
    fi
  fi

  if has_kernel_log_access; then
    KERNEL_LOG_NOTE="already in a group that can read the kernel log"
  elif ! need_command usermod; then
    KERNEL_LOG_NOTE="usermod not found; add $(id -un) to the 'adm' group manually"
  else
    echo
    echo "Export DMESG reads the kernel log via 'journalctl -k -b', which needs"
    echo "membership in the 'adm' group. You are not in it."
    if ask_yes_no "Add $(id -un) to the 'adm' group?" "yes"; then
      DO_JOIN_ADM=1
    else
      KERNEL_LOG_NOTE="declined; Export DMESG will not work until $(id -un) joins 'adm'"
    fi
  fi
}

# Requests the sudo password once, up front, and only when an answer above
# actually calls for it. Every privileged step then reuses that credential
# instead of prompting again mid-run. drop_sudo() invalidates it at the end.
prime_sudo() {
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return
  fi
  if [[ "${DO_INSTALL_DEPS}" -eq 0 && "${DO_JOIN_ADM}" -eq 0 ]]; then
    return
  fi
  echo
  echo "Administrator access is required for the steps you selected."
  sudo -v
  SUDO_PRIMED=1
  echo
}

# Drops the cached sudo credential so it does not outlive this script. Without
# this the timestamp stays valid for the terminal's grace period, leaving a
# later command able to sudo without asking.
drop_sudo() {
  if [[ "${SUDO_PRIMED}" -eq 1 ]]; then
    sudo -k 2>/dev/null || true
    SUDO_PRIMED=0
  fi
}

ensure_dependencies() {
  if [[ "${DO_INSTALL_DEPS}" -eq 0 ]]; then
    return 2
  fi
  run sudo apt-get update
  run sudo apt-get install -y "${APT_PACKAGES[@]}"
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

  # The web UI toolchain (Vitest + @testing-library/jest-dom 7) requires
  # Node 22; CI pins the same major.
  if [[ "${node_major}" -lt 22 ]]; then
    nvm install 22 >/dev/null
    nvm use 22 >/dev/null
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

# Adds the user to "adm", the group whose ACL on the journal directories lets
# 'journalctl -k -b' read the kernel log. This is what Export DMESG needs; the
# binary itself stays unprivileged. Returns 2 (skipped) when the user already
# has access or declined.
join_adm_group() {
  if [[ "${DO_JOIN_ADM}" -eq 0 ]]; then
    return 2
  fi
  run sudo usermod -aG adm "$(id -un)"
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

# The sudo credential must not outlive the script, on any exit path — a failed
# build would otherwise leave the terminal able to sudo without a password.
trap 'stop_spinner; drop_sudo' EXIT

collect_choices
prime_sudo

run_step "Installing system dependencies" ensure_dependencies
run_step "Building web UI" build_frontend
run_step "Building C++ project" build_cpp
run_step "Installing files" install_files
run_step "Enabling kernel log access (adm group)" join_adm_group

drop_sudo
path_notice

echo
if [[ "${DRY_RUN}" -eq 1 ]]; then
  echo "Dry run complete. No files were changed."
  exit 0
fi

echo "Installation complete."
echo "Launch with: ${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic-web"

if [[ "${DO_JOIN_ADM}" -eq 1 ]]; then
  echo
  echo "Added $(id -un) to the 'adm' group. Log out and back in for this to"
  echo "take effect — Export DMESG will not work in this session until you do."
elif [[ -n "${KERNEL_LOG_NOTE}" && "$(has_kernel_log_access && echo yes || echo no)" == "no" ]]; then
  echo
  echo "Note: Export DMESG — ${KERNEL_LOG_NOTE}."
fi
