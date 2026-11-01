#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Pre-approve installs via -y / --assume-yes / --yes, or FUNKE_SETUP_ASSUME_YES=1.
ASSUME_YES="${FUNKE_SETUP_ASSUME_YES:-}"
for arg in "$@"; do
    case "$arg" in
        -y | --yes | --assume-yes) ASSUME_YES=1 ;;
    esac
done

# Finds a Python 3.10+ and stores its path in PYTHON_CMD. Homebrew's versioned formulae
# (python@3.13) only install python3.13, and macOS' own python3 is 3.9.
PYTHON_CMD=""
find_python() {
    local candidate
    for candidate in python3 python3.15 python3.14 python3.13 python3.12 python3.11 python3.10; do
        if command -v "$candidate" >/dev/null 2>&1 &&
            "$candidate" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' >/dev/null 2>&1; then
            PYTHON_CMD="$(command -v "$candidate")"
            return 0
        fi
    done
    return 1
}

# Echo the package-manager command that installs Python, or return 1.
detect_python_install_cmd() {
    if command -v brew >/dev/null 2>&1; then echo "brew install python3"; return 0; fi
    if command -v apt-get >/dev/null 2>&1; then echo "apt-get install -y python3"; return 0; fi
    if command -v dnf >/dev/null 2>&1; then echo "dnf install -y python3"; return 0; fi
    if command -v pacman >/dev/null 2>&1; then echo "pacman -S --noconfirm python"; return 0; fi
    if command -v zypper >/dev/null 2>&1; then echo "zypper install -y python3"; return 0; fi
    return 1
}

install_python() {
    local cmd
    if ! cmd="$(detect_python_install_cmd)"; then
        echo "[Funke Setup] No supported package manager (Homebrew/apt/dnf/pacman/zypper) was found."
        echo "[Funke Setup] Install Python 3.10+ manually, then re-run setup."
        return 1
    fi
    # Homebrew refuses to run as root; the system package managers need it.
    if [ "$(id -u)" -ne 0 ] && [ "${cmd#brew }" = "$cmd" ]; then
        if command -v sudo >/dev/null 2>&1; then
            cmd="sudo $cmd"
        else
            echo "[Funke Setup] Root privileges are required but 'sudo' is unavailable."
            return 1
        fi
    fi
    echo "[Funke Setup] Installing Python via: $cmd"
    # shellcheck disable=SC2086
    $cmd
}

if ! find_python; then
    echo "[Funke Setup] Python 3.10+ was not found."
    do_install=0
    if [ -n "$ASSUME_YES" ]; then
        do_install=1
    elif [ -t 0 ]; then
        read -r -p "[Funke Setup] Install Python now? [Y/n] " answer
        case "${answer:-y}" in
            "" | y | Y | yes | Yes) do_install=1 ;;
        esac
    fi
    if [ "$do_install" -eq 1 ]; then
        install_python || true
    fi
    # Homebrew may have installed into a prefix this shell's PATH does not list yet.
    for prefix in /opt/homebrew/bin /usr/local/bin; do
        case ":$PATH:" in
            *":$prefix:"*) ;;
            *) if [ -d "$prefix" ]; then PATH="$PATH:$prefix"; fi ;;
        esac
    done
    if ! find_python; then
        echo "[Funke Setup] ERROR: Python 3.10+ is still unavailable. Aborting."
        exit 1
    fi
fi

# Premake-time steps and the generated build commands reuse this interpreter (premake/compat.lua).
export FUNKE_PYTHON="$PYTHON_CMD"
"$PYTHON_CMD" "$SCRIPT_DIR/Setup.py" "$@"
