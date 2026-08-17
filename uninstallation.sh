#!/usr/bin/env bash
set -euo pipefail

INSTALL_PREFIX="${HOME}/.local"
APP_SHARE="${INSTALL_PREFIX}/share/v4l2-camera-diagnostic"
DESKTOP_FILE="${INSTALL_PREFIX}/share/applications/v4l2-camera-diagnostic.desktop"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PURGE=0
CLEAN=0
ASSUME_YES=0
DRY_RUN=0
DEBUG=0
STEP_TOTAL=4
STEP_CURRENT=0
USE_COLOR=0

if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
  USE_COLOR=1
fi

for arg in "$@"; do
  case "${arg}" in
    --purge) PURGE=1 ;;
    --clean) CLEAN=1 ;;
    --yes|-y) ASSUME_YES=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --debug) DEBUG=1 ;;
    --help|-h)
      cat <<USAGE
Usage: ./uninstallation.sh [--purge] [--clean] [--yes] [--dry-run] [--debug]

Removes the user-level V4L2 Camera Diagnostic installation.

Options:
  --purge   Remove user config, cache, and state.
  --clean   Remove local development artifacts (build, dist, node_modules,
            package-lock.json, npm cache) except reports.
  --yes     Do not ask interactive questions.
  --dry-run Print the actions without removing files.
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

title() {
  if [[ "${DEBUG}" -eq 1 ]]; then
    echo "V4L2 Camera Diagnostic uninstaller [debug]"
  else
    echo "V4L2 Camera Diagnostic uninstaller"
  fi
  echo
}

step_begin() {
  STEP_CURRENT=$((STEP_CURRENT + 1))
  if [[ "${DEBUG}" -eq 1 ]]; then
    printf '[%d/%d] %s\n' "${STEP_CURRENT}" "${STEP_TOTAL}" "$1"
  else
    printf '[%d/%d] %s ... ' "${STEP_CURRENT}" "${STEP_TOTAL}" "$1"
  fi
}

step_ok() {
  if [[ "${DEBUG}" -eq 1 ]]; then
    echo "[ok]"
  else
    color "32" "ok"
    echo
  fi
}

step_skip() {
  if [[ "${DEBUG}" -eq 1 ]]; then
    echo "[skipped]"
  else
    color "33" "skipped"
    echo
  fi
}

step_fail() {
  if [[ "${DEBUG}" -eq 1 ]]; then
    echo "[failed]"
  else
    color "31" "failed"
    echo
  fi
}

run_remove() {
  if [[ "${DEBUG}" -eq 1 ]]; then
    echo "+ $*"
    if [[ "${DRY_RUN}" -eq 0 ]]; then
      "$@"
    fi
    return
  fi

  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return
  fi

  local output
  output="$(mktemp)"
  if "$@" >"${output}" 2>&1; then
    rm -f "${output}"
    return 0
  fi

  local status=$?
  echo "Command failed (${status}): $*" >&2
  sed 's/^/  /' "${output}" >&2
  rm -f "${output}"
  return "${status}"
}

run_step() {
  local label="$1"
  shift
  step_begin "${label}"
  if "$@"; then
    step_ok
  else
    step_fail
    return 1
  fi
}

remove_installed_binaries() {
  run_remove rm -f "${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic" || return
  run_remove rm -f "${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic-web" || return
  run_remove rm -f "${DESKTOP_FILE}"
}

remove_installed_assets() {
  run_remove rm -rf "${APP_SHARE}/web" || return
  run_remove rm -rf "${APP_SHARE}/docs" || return
  run_remove rm -rf "${APP_SHARE}/configs" || return
  remove_empty_app_share
}

remove_user_state() {
  if [[ "${PURGE}" -eq 0 ]]; then
    return 2
  fi
  run_remove rm -rf "${HOME}/.config/v4l2-camera-diagnostic" || return
  run_remove rm -rf "${HOME}/.cache/v4l2-camera-diagnostic" || return
  run_remove rm -rf "${HOME}/.local/state/v4l2-camera-diagnostic"
}

remove_local_artifacts() {
  if [[ "${CLEAN}" -eq 0 ]]; then
    return 2
  fi
  run_remove rm -rf "${ROOT_DIR}/build" || return
  run_remove rm -rf "${ROOT_DIR}/source/frontend/dist" || return
  run_remove rm -rf "${ROOT_DIR}/source/frontend/node_modules" || return
  run_remove rm -f "${ROOT_DIR}/source/frontend/package-lock.json" || return
  # Load nvm so npm is reachable even when system Node is too old.
  if [[ -s "${NVM_DIR:-$HOME/.nvm}/nvm.sh" ]]; then
    # shellcheck source=/dev/null
    \. "${NVM_DIR:-$HOME/.nvm}/nvm.sh"
  fi
  if command -v npm &>/dev/null; then
    run_remove npm cache clean --force
  fi
}

run_optional_step() {
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

remove_empty_app_share() {
  if [[ "${DEBUG}" -eq 1 ]]; then
    echo "+ rmdir ${APP_SHARE}"
  fi
  if [[ "${DRY_RUN}" -eq 0 ]]; then
    rmdir "${APP_SHARE}" 2>/dev/null || true
  fi
}

ask_yes_no() {
  local prompt="$1"
  local default="$2"
  local answer

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
      y|Y|yes|YES) return 0 ;;
      n|N|no|NO) return 1 ;;
      *) echo "Please answer yes or no." ;;
    esac
  done
}

title

if [[ "${ASSUME_YES}" -eq 0 ]] && [[ "${CLEAN}" -eq 0 ]] && ask_yes_no "Remove local build artifacts while preserving reports?" "no"; then
  CLEAN=1
fi

if [[ "${ASSUME_YES}" -eq 0 ]] && [[ "${PURGE}" -eq 0 ]] && ask_yes_no "Remove user config, cache, and state?" "no"; then
  PURGE=1
fi

echo
run_step "Removing installed binaries" remove_installed_binaries
run_step "Removing installed web/docs assets" remove_installed_assets
run_optional_step "Removing user config/state" remove_user_state
run_optional_step "Removing local build artifacts" remove_local_artifacts

if [[ "${DRY_RUN}" -eq 1 ]]; then
  echo "Dry run complete. No files were removed."
else
  echo "Uninstallation complete."
fi
if [[ "${PURGE}" -eq 0 ]]; then
  echo "User profiles and state were preserved."
fi
if [[ "${CLEAN}" -eq 0 ]]; then
  echo "Local build artifacts were preserved."
elif [[ "${DRY_RUN}" -eq 1 ]]; then
  echo "Local build artifacts would be removed. Reports would be preserved."
else
  echo "Local build artifacts were removed. Reports were preserved."
fi
