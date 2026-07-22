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

for arg in "$@"; do
  case "${arg}" in
    --purge) PURGE=1 ;;
    --clean) CLEAN=1 ;;
    --yes|-y) ASSUME_YES=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --help|-h)
      cat <<USAGE
Usage: ./uninstallation.sh [--purge] [--clean] [--yes] [--dry-run]

Removes the user-level V4L2 Camera Diagnostic installation.

Options:
  --purge   Remove user config, cache, and state.
  --clean   Remove local development artifacts (build, dist, node_modules,
            package-lock.json, npm cache) except reports.
  --yes     Do not ask interactive questions.
  --dry-run Print the actions without removing files.
USAGE
      exit 0
      ;;
    *)
      echo "Unknown option: ${arg}" >&2
      exit 2
      ;;
  esac
done

run_remove() {
  echo "+ $*"
  if [[ "${DRY_RUN}" -eq 0 ]]; then
    "$@"
  fi
}

remove_empty_app_share() {
  echo "+ rmdir ${APP_SHARE}"
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

if [[ "${ASSUME_YES}" -eq 0 ]] && [[ "${PURGE}" -eq 0 ]] && ask_yes_no "Remove user config, cache, and state?" "no"; then
  PURGE=1
fi

if [[ "${ASSUME_YES}" -eq 0 ]] && [[ "${CLEAN}" -eq 0 ]] && ask_yes_no "Remove local build artifacts while preserving reports?" "no"; then
  CLEAN=1
fi

run_remove rm -f "${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic"
run_remove rm -f "${INSTALL_PREFIX}/bin/v4l2-camera-diagnostic-web"
run_remove rm -f "${DESKTOP_FILE}"
run_remove rm -rf "${APP_SHARE}/web"
run_remove rm -rf "${APP_SHARE}/docs"
run_remove rm -rf "${APP_SHARE}/configs"
remove_empty_app_share

if [[ "${PURGE}" -eq 1 ]]; then
  run_remove rm -rf "${HOME}/.config/v4l2-camera-diagnostic"
  run_remove rm -rf "${HOME}/.cache/v4l2-camera-diagnostic"
  run_remove rm -rf "${HOME}/.local/state/v4l2-camera-diagnostic"
fi

if [[ "${CLEAN}" -eq 1 ]]; then
  run_remove rm -rf "${ROOT_DIR}/build"
  run_remove rm -rf "${ROOT_DIR}/source/frontend/dist"
  run_remove rm -rf "${ROOT_DIR}/source/frontend/node_modules"
  run_remove rm -f "${ROOT_DIR}/source/frontend/package-lock.json"
  # Load nvm so npm is reachable even when system Node is too old.
  if [[ -s "${NVM_DIR:-$HOME/.nvm}/nvm.sh" ]]; then
    # shellcheck source=/dev/null
    \. "${NVM_DIR:-$HOME/.nvm}/nvm.sh"
  fi
  if command -v npm &>/dev/null; then
    run_remove npm cache clean --force
  fi
fi

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
