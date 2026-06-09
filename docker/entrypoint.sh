#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -e

REPO_BASE="https://chromium.googlesource.com/chromiumos"

echo "Entering Docker container..."

# Cache directory in the image
CACHE_BASE="/opt/repos"

# Function to safely update a repository if it has no local changes
update_repo() {
    local repo_dir="${1}"
    local repo_name="${2}"
    local remote="${3:-origin}"
    local branch="${4:-}"

    if (cd "${repo_dir}" && git diff --quiet && git diff --cached --quiet); then
        echo "Updating ${repo_name} (fetching deltas)..."
        if [ -z "${branch}" ]; then
            (cd "${repo_dir}" && git pull "${remote}")
        else
            (cd "${repo_dir}" && git pull "${remote}" "${branch}")
        fi
    else
        echo "Warning: ${repo_name} has local changes." \
             "Skipping automatic update to avoid conflicts."
    fi
}

# Function to clone or update a repository, using build-time cache if available
clone_or_update() {
    local repo_url="${1}"
    local target_dir="${2}"
    local repo_name="${3}"

    if [ ! -d "${target_dir}" ]; then
        # Check if we have a cached version in the image
        # Map /workspace to $CACHE_BASE
        local cached_dir="${target_dir//\/workspace/${CACHE_BASE}}"
        if [ -d "${cached_dir}" ]; then
            echo "Populating ${repo_name} from build-time cache..."
            mkdir -p "$(dirname "${target_dir}")"
            cp -a "${cached_dir}" "${target_dir}"
            update_repo "${target_dir}" "${repo_name}"
        else
            echo "Downloading the latest ${repo_name}..."
            git clone "${repo_url}" "${target_dir}"
        fi
    else
        echo "${repo_name} directory already exists. Checking for updates..."
        update_repo "${target_dir}" "${repo_name}"
    fi
}

# Specialized clone/update function for chromiumos-overlay (sparse checkout)
clone_overlay_sparse() {
    local repo_url="${REPO_BASE}/overlays/chromiumos-overlay"
    local target_dir="/workspace/src/third_party/chromiumos-overlay"
    local cached_dir="${CACHE_BASE}/src/third_party/chromiumos-overlay"

    if [ ! -d "${target_dir}" ]; then
        if [ -d "${cached_dir}" ]; then
            echo "Populating ChromiumOS Overlay from build-time cache..."
            mkdir -p "$(dirname "${target_dir}")"
            cp -a "${cached_dir}" "${target_dir}"
            update_repo "${target_dir}" "ChromiumOS Overlay" "origin" "main"
        else
            echo "Performing sparse checkout of ChromiumOS Overlay..."
            mkdir -p "${target_dir}"
            cd "${target_dir}" || exit 1
            git init
            git remote add origin "${repo_url}"
            git config core.sparseCheckout true

            # Define directories to include
            echo "eclass/" >> .git/info/sparse-checkout

            git pull --depth=1 origin main
            # Set up branch tracking so git pull works without arguments
            # next time
            git branch --set-upstream-to=origin/main master || \
                git branch --set-upstream-to=origin/main main || \
                true
            cd - > /dev/null
        fi
    else
        echo "ChromiumOS Overlay directory already exists." \
             "Checking for updates..."
        update_repo "${target_dir}" "ChromiumOS Overlay" "origin" "main"
    fi
}

# Clone or update the required repositories
clone_or_update "${REPO_BASE}/platform/ec" \
    "/workspace/src/platform/ec" "EC firmware"
clone_or_update "${REPO_BASE}/platform/dagwood" \
    "/workspace/src/platform/dagwood" "Dagwood"
clone_or_update "${REPO_BASE}/third_party/zephyrproject" \
    "/workspace/src/third_party/zephyrproject" "Zephyr Project"
clone_or_update "${REPO_BASE}/third_party/pigweed/pigweed" \
    "/workspace/src/third_party/pigweed" "Pigweed"
clone_or_update "${REPO_BASE}/third_party/u-boot" \
    "/workspace/src/third_party/u-boot" "U-Boot"
clone_overlay_sparse

# Set up python virtual environment if it doesn't exist
VENV_DIR="/workspace/.venv"
if [ ! -d "${VENV_DIR}" ]; then
    echo "Creating Python virtual environment..."
    python3 -m venv --system-site-packages "${VENV_DIR}"
fi

# Activate the virtual environment
echo "Activating virtual environment..."
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"

# Install zmake package in editable mode inside virtualenv
if [ -d "/workspace/src/platform/ec/zephyr/zmake" ]; then
    echo "Installing zmake tool in virtualenv..."
    python3 -m pip install --upgrade pip
    python3 -m pip install -e /workspace/src/platform/ec/zephyr/zmake

    # Install standard Zephyr dependencies to support twister executions
    ZEPHYR_REQS_DIR="/workspace/src/third_party/zephyrproject/zephyr/scripts"
    if [ -d "${ZEPHYR_REQS_DIR}" ]; then
        echo "Installing Zephyr dependencies..."
        python3 -m pip install -r "${ZEPHYR_REQS_DIR}/requirements.txt"
    fi

    # Parse .vpython3 and install dependencies
    VPYTHON_FILE="/workspace/src/platform/ec/zephyr/zmake/.vpython3"
    if [ -f "${VPYTHON_FILE}" ]; then
        echo "Installing dependencies from .vpython3..."
        packages=$(grep -o 'infra/python/wheels/[a-zA-Z0-9_-]*' \
            "${VPYTHON_FILE}" | \
            sed 's|infra/python/wheels/||g' | \
            sed 's|-py2_py3||g' | \
            sed 's|-py3||g' | \
            sort -u)
        for pkg in ${packages}; do
            case "${pkg}" in
                "pyyaml") pkg="PyYAML" ;;
                "python-dateutil") pkg="python-dateutil" ;;
                "ruamel_yaml") pkg="ruamel.yaml" ;;
                # Skip packages already installed by Zephyr's requirements.txt
                # above that would be downgraded based on the .vpython
                # requirements
                "ruamel_yaml_clib") continue ;;
                "coverage") continue ;;
                "pytest") continue ;;
                # Skip packages only used by zmake unit tests
                "hypothesis") continue ;;
                "testfixtures") continue ;;
            esac
            echo "Installing ${pkg}..."
            python3 -m pip install "${pkg}"
        done
    fi

    # Install standard Zephyr dependencies to support twister executions
    ZEPHYR_REQS_DIR="/workspace/src/third_party/zephyrproject/zephyr/scripts"
    if [ -d "${ZEPHYR_REQS_DIR}" ]; then
        echo "Installing Zephyr dependencies..."
        python3 -m pip install -r "${ZEPHYR_REQS_DIR}/requirements.txt"
    fi

    # Export U-Boot binman tools directory to PATH
    if [ -d "/workspace/src/third_party/u-boot/tools/binman" ]; then
        echo "Adding binman to PATH..."
        export PATH="/workspace/src/third_party/u-boot/tools/binman:${PATH}"
    fi

    # Set up Realtek monitor binary (rtk_flame)
    MONITOR_CACHE="/workspace/.cache/rts5915_flash_upload.bin"
    MONITOR_DEST_DIR="/usr/share/ec-devutils"
    MONITOR_DEST="${MONITOR_DEST_DIR}/rts5915_flash_upload.bin"

    if [ ! -f "${MONITOR_CACHE}" ]; then
        echo "Monitor binary not found in cache. Building rtk_flame..."
        if zmake --checkout /workspace build rtk_flame; then
            echo "Caching monitor binary..."
            mkdir -p "$(dirname "${MONITOR_CACHE}")"
            build_bin="/workspace/src/platform/ec/build/zephyr"
            build_bin="${build_bin}/rtk_flame/build-singleimage"
            build_bin="${build_bin}/rts5915_flash_upload.bin"
            cp "${build_bin}" "${MONITOR_CACHE}"
        else
            echo "Warning: Failed to build rtk_flame." \
                 "Realtek flashing may not work."
        fi
    fi

    if [ -f "${MONITOR_CACHE}" ]; then
        echo "Installing monitor binary to ${MONITOR_DEST}..."
        mkdir -p "${MONITOR_DEST_DIR}"
        cp "${MONITOR_CACHE}" "${MONITOR_DEST}"
    fi

    # Set up NPCX monitor binary (npcx_monitor)
    NPCX_MONITOR_CACHE="/workspace/.cache/npcx_monitor.bin"
    NPCX_MONITOR_DEST_DIR="/usr/share/ec-devutils"
    NPCX_MONITOR_DEST="${NPCX_MONITOR_DEST_DIR}/npcx_monitor.bin"

    if [ ! -f "${NPCX_MONITOR_CACHE}" ]; then
        echo "Monitor binary not found in cache. Building npcx_monitor..."
        if zmake --checkout /workspace build npcx_monitor; then
            echo "Caching monitor binary..."
            mkdir -p "$(dirname "${NPCX_MONITOR_CACHE}")"
            build_bin="/workspace/src/platform/ec/build/zephyr"
            build_bin="${build_bin}/npcx_monitor/build-singleimage"
            build_bin="${build_bin}/npcx_monitor.bin"
            cp "${build_bin}" "${NPCX_MONITOR_CACHE}"
        else
            echo "Warning: Failed to build npcx_monitor." \
                 "NPCX flashing may not work."
        fi
    fi

    if [ -f "${NPCX_MONITOR_CACHE}" ]; then
        echo "Installing monitor binary to ${NPCX_MONITOR_DEST}..."
        mkdir -p "${NPCX_MONITOR_DEST_DIR}"
        cp "${NPCX_MONITOR_CACHE}" "${NPCX_MONITOR_DEST}"
    fi
else
    echo "Warning: zmake directory not found. Skipping installation."
fi

# Run the requested command (if any), or drop to bash
if [ $# -eq 0 ]; then
    exec /bin/bash
else
    exec "$@"
fi
