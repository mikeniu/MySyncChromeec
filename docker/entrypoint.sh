#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -e

# If running as root and host UID/GID are provided, set up host user and re-exec
if [ "$(id -u)" = "0" ] && [ -n "${HOST_UID}" ] && \
   [ -n "${HOST_GID}" ] && [ "${HOST_UID}" != "0" ]; then
    groupadd -g "${HOST_GID}" hostuser 2>/dev/null || true
    useradd -u "${HOST_UID}" -g "${HOST_GID}" -m -s /bin/bash \
        hostuser 2>/dev/null || true
    # Grant access to serial TTYs and USB devices for flashing/debug
    usermod -aG dialout,plugdev hostuser 2>/dev/null || true
    # Prepare devutils directory for monitor binary installation
    mkdir -p /usr/share/ec-devutils
    chown -R "${HOST_UID}:${HOST_GID}" /usr/share/ec-devutils
    chmod 755 /entrypoint.sh
    exec gosu hostuser /bin/bash /entrypoint.sh "$@"
fi

REPO_BASE="https://chromium.googlesource.com/chromiumos"

# Parse --fast flag from positional arguments
ARGS=()
for arg in "$@"; do
    case "${arg}" in
        --fast)
            SKIP_UPDATE=1
            ;;
        *)
            ARGS+=("${arg}")
            ;;
    esac
done
set -- "${ARGS[@]}"

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
            (cd "${repo_dir}" && git pull --quiet "${remote}")
        else
            (cd "${repo_dir}" && git pull --quiet "${remote}" "${branch}")
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
            git clone --quiet "${repo_url}" "${target_dir}"
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
            cd "${target_dir}" || exit 1
            git init -b main -q
            git remote add origin "${repo_url}"
            git config core.sparseCheckout true

            # Define directories to include
            echo "/eclass/coreboot-sdk-ec-dependencies.eclass" \
                >> .git/info/sparse-checkout

            git pull --depth=1 --quiet origin main
            # Set up branch tracking so git pull works without arguments
            # next time
            git branch --set-upstream-to=origin/main main
            cd - > /dev/null
        fi
    else
        echo "ChromiumOS Overlay directory already exists." \
             "Checking for updates..."
        update_repo "${target_dir}" "ChromiumOS Overlay" "origin" "main"
    fi
}

# Specialized clone/update function for zephyrproject (sparse checkout)
clone_zephyrproject_sparse() {
    local repo_url="${REPO_BASE}/third_party/zephyrproject"
    local target_dir="/workspace/src/third_party/zephyrproject"
    local cached_dir="${CACHE_BASE}/src/third_party/zephyrproject"

    if [ ! -d "${target_dir}" ]; then
        if [ -d "${cached_dir}" ]; then
            echo "Populating Zephyr Project from build-time cache..."
            mkdir -p "$(dirname "${target_dir}")"
            cp -a "${cached_dir}" "${target_dir}"
            update_repo "${target_dir}" "Zephyr Project" "origin" "main"
        else
            echo "Performing sparse checkout of Zephyr Project..."
            mkdir -p "${target_dir}"
            cd "${target_dir}"
            git init -b main -q
            git remote add origin "${repo_url}"
            git config core.sparseCheckout true

            # Define directories to include
            {
                echo "/zephyr/"
                echo "/modules/hal/cmsis_6/"
                echo "/modules/hal/stm32/"
                echo "/modules/lib/picolibc/"
                echo "/modules/lib/nanopb/"
            } >> .git/info/sparse-checkout

            git pull --depth=1 --quiet origin main
            # Set up branch tracking so git pull works without arguments
            # next time
            git branch --set-upstream-to=origin/main main
            cd - > /dev/null
        fi
    else
        echo "Zephyr Project directory already exists." \
             "Checking for updates..."
        update_repo "${target_dir}" "Zephyr Project" "origin" "main"
    fi
}

# Specialized clone/update function for u-boot (sparse checkout)
clone_uboot_sparse() {
    local repo_url="${REPO_BASE}/third_party/u-boot"
    local target_dir="/workspace/src/third_party/u-boot"
    local cached_dir="${CACHE_BASE}/src/third_party/u-boot"

    if [ ! -d "${target_dir}" ]; then
        if [ -d "${cached_dir}" ]; then
            echo "Populating U-Boot from build-time cache..."
            mkdir -p "$(dirname "${target_dir}")"
            cp -a "${cached_dir}" "${target_dir}"
            update_repo "${target_dir}" "U-Boot"
        else
            echo "Performing sparse checkout of U-Boot..."
            mkdir -p "${target_dir}"
            cd "${target_dir}" || exit 1
            git clone --depth 1 --no-checkout --quiet "${repo_url}" .
            git config core.sparseCheckout true

            # Define directories to include
            {
                echo "/tools/binman/"
                echo "/tools/dtoc/"
                echo "/tools/patman/"
                echo "/tools/buildman/"
            } >> .git/info/sparse-checkout

            git checkout --quiet
            cd - > /dev/null
        fi
    else
        echo "U-Boot directory already exists. Checking for updates..."
        update_repo "${target_dir}" "U-Boot"
    fi
}

# Function to populate a repository from build-time cache if it doesn't exist
populate_if_missing() {
    local target_dir="${1}"
    local repo_name="${2}"
    if [ ! -d "${target_dir}" ]; then
        local cached_dir="${target_dir//\/workspace/${CACHE_BASE}}"
        if [ -d "${cached_dir}" ]; then
            echo "Populating ${repo_name} from build-time cache..."
            mkdir -p "$(dirname "${target_dir}")"
            cp -a "${cached_dir}" "${target_dir}"
        fi
    fi
}

# Clone or update repositories (parallelized for speed)
if [ -n "${SKIP_UPDATE}" ]; then
    echo "Skipping repository updates (fast startup enabled)..."
    populate_if_missing "/workspace/src/platform/ec" "EC firmware"
    populate_if_missing "/workspace/src/platform/dagwood" "Dagwood"
    populate_if_missing \
        "/workspace/src/third_party/zephyrproject" "Zephyr Project"
    populate_if_missing "/workspace/src/third_party/pigweed" "Pigweed"
    populate_if_missing "/workspace/src/third_party/u-boot" "U-Boot"
    populate_if_missing \
        "/workspace/src/third_party/chromiumos-overlay" "ChromiumOS Overlay"
else
    clone_or_update "${REPO_BASE}/platform/ec" \
        "/workspace/src/platform/ec" "EC firmware" &
    clone_or_update "${REPO_BASE}/platform/dagwood" \
        "/workspace/src/platform/dagwood" "Dagwood" &
    clone_zephyrproject_sparse &
    clone_or_update "${REPO_BASE}/third_party/pigweed/pigweed" \
        "/workspace/src/third_party/pigweed" "Pigweed" &
    clone_uboot_sparse &
    clone_overlay_sparse &
    wait
fi

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

# Configure PATH for U-Boot binman tools. Prioritize local workspace tools if
# present, falling back to the container cache.
export PATH="${PATH}:/opt/repos/src/third_party/u-boot/tools/binman"
export PATH="/workspace/src/third_party/u-boot/tools/binman:${PATH}"

# Set up Coreboot SDK cache directory symlink for the current user. The target
# lives under the bind-mounted workspace so that downloaded toolchains outlive
# the container. Create it before linking: nothing can write through a
# dangling symlink, so the SDK download would fail against a bare workspace.
mkdir -p /workspace/.cache/coreboot-sdk
mkdir -p "${HOME}/.cache"
ln -sfn /workspace/.cache/coreboot-sdk "${HOME}/.cache/coreboot-sdk"

# Function to query and export Coreboot SDK toolchain paths into environment.
# This ensures toolchain roots (e.g. COREBOOT_SDK_ROOT_arm) are available for
# zmake builds and twister runs inside the container.
setup_coreboot_sdk_env() {
    local sdk_script=""
    if [ -f "/workspace/src/platform/ec/util/coreboot_sdk.py" ]; then
        sdk_script="/workspace/src/platform/ec/util/coreboot_sdk.py"
    elif [ -f "/opt/repos/src/platform/ec/util/coreboot_sdk.py" ]; then
        sdk_script="/opt/repos/src/platform/ec/util/coreboot_sdk.py"
    fi

    if [ -n "${sdk_script}" ]; then
        eval "$(python3 -c '
import json, subprocess, sys
try:
    script = sys.argv[1]
    out = subprocess.check_output([sys.executable, script, "-j"]).decode()
    for k, v in json.loads(out).items():
        print(f"export {k}=\"{v}\"")
except Exception as e:
    sys.stderr.write(f"Warning: Failed to load Coreboot SDK: {e}\n")
' "${sdk_script}")"
    fi
}

# Function to compute sha256 hash of python requirement files
compute_reqs_hash() {
    local files=()
    local req_dir="/workspace/src/third_party/zephyrproject/zephyr/scripts"
    local vpython_file="/workspace/src/platform/ec/zephyr/zmake/.vpython3"

    if [ -d "${req_dir}" ]; then
        for f in "${req_dir}"/requirements*.txt; do
            [ -f "${f}" ] && files+=("${f}")
        done
    fi
    [ -f "${vpython_file}" ] && files+=("${vpython_file}")

    if [ ${#files[@]} -gt 0 ]; then
        sha256sum "${files[@]}" | sha256sum | awk '{print $1}'
    else
        echo "none"
    fi
}

# Install zmake package in editable mode inside virtualenv
if [ -d "/workspace/src/platform/ec/zephyr/zmake" ]; then
    HASH_FILE="${VENV_DIR}/.requirements.hash"
    CURRENT_HASH="$(compute_reqs_hash)"
    PAST_HASH=""
    [ -f "${HASH_FILE}" ] && PAST_HASH="$(cat "${HASH_FILE}")"

    if [ "${CURRENT_HASH}" != "none" ] && \
       [ "${CURRENT_HASH}" = "${PAST_HASH}" ]; then
        echo "Python dependencies up to date. Skipping pip install."
    else
        echo "Configuring zmake tool in virtualenv..."
        python3 -m pip install -q --no-deps \
            -e /workspace/src/platform/ec/zephyr/zmake

        if [ -z "${SKIP_UPDATE}" ]; then
            # Install standard Zephyr dependencies to support twister executions
            ZEPHYR_REQS_DIR="/workspace/src/third_party"
            ZEPHYR_REQS_DIR="${ZEPHYR_REQS_DIR}/zephyrproject/zephyr/scripts"
            if [ -d "${ZEPHYR_REQS_DIR}" ]; then
                python3 -m pip install -q --no-cache-dir \
                    -r "${ZEPHYR_REQS_DIR}/requirements.txt"
            fi

            # Parse .vpython3 and install dependencies
            VPYTHON_FILE="/workspace/src/platform/ec/zephyr/zmake/.vpython3"
            if [ -f "${VPYTHON_FILE}" ]; then
                packages=$(grep -o \
                    'infra/python/wheels/[a-zA-Z0-9_-]*' \
                    "${VPYTHON_FILE}" | \
                    sed 's|infra/python/wheels/||g' | \
                    sed 's|-py2_py3||g; s|-py3||g' | \
                    sort -u)
                to_install=""
                for pkg in ${packages}; do
                    case "${pkg}" in
                        "pyyaml") pkg="PyYAML" ;;
                        "python-dateutil") pkg="python-dateutil" ;;
                        "ruamel_yaml") pkg="ruamel.yaml" ;;
                        "ruamel_yaml_clib"|"coverage"|"pytest"| \
                        "hypothesis"|"testfixtures") continue ;;
                    esac
                    to_install="${to_install} ${pkg}"
                done
                if [ -n "${to_install}" ]; then
                    # shellcheck disable=SC2086
                    python3 -m pip install -q --no-cache-dir ${to_install}
                fi
            fi
        fi

        echo "${CURRENT_HASH}" > "${HASH_FILE}"
    fi

    # Configure Coreboot SDK toolchain environment variables
    setup_coreboot_sdk_env

    # Set up Realtek monitor binary (rtk_flame)
    MONITOR_CACHE="/workspace/.cache/rts5915_flash_upload.bin"
    MONITOR_DEST_DIR="/usr/share/ec-devutils"
    MONITOR_DEST="${MONITOR_DEST_DIR}/rts5915_flash_upload.bin"

    if [ ! -f "${MONITOR_CACHE}" ]; then
        if [ -f "${MONITOR_DEST}" ]; then
            echo "Copying pre-installed Realtek monitor binary to cache..."
            mkdir -p "$(dirname "${MONITOR_CACHE}")"
            cp "${MONITOR_DEST}" "${MONITOR_CACHE}"
        elif [ -z "${SKIP_UPDATE}" ]; then
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
    fi

    if [ -f "${MONITOR_CACHE}" ] && [ ! -f "${MONITOR_DEST}" ]; then
        echo "Installing monitor binary to ${MONITOR_DEST}..."
        mkdir -p "${MONITOR_DEST_DIR}"
        cp "${MONITOR_CACHE}" "${MONITOR_DEST}"
    fi

    # Set up NPCX monitor binary (npcx_monitor)
    NPCX_MONITOR_CACHE="/workspace/.cache/npcx_monitor.bin"
    NPCX_MONITOR_DEST_DIR="/usr/share/ec-devutils"
    NPCX_MONITOR_DEST="${NPCX_MONITOR_DEST_DIR}/npcx_monitor.bin"

    if [ ! -f "${NPCX_MONITOR_CACHE}" ]; then
        if [ -f "${NPCX_MONITOR_DEST}" ]; then
            echo "Copying pre-installed NPCX monitor binary to cache..."
            mkdir -p "$(dirname "${NPCX_MONITOR_CACHE}")"
            cp "${NPCX_MONITOR_DEST}" "${NPCX_MONITOR_CACHE}"
        elif [ -z "${SKIP_UPDATE}" ]; then
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
    fi

    if [ -f "${NPCX_MONITOR_CACHE}" ] && [ ! -f "${NPCX_MONITOR_DEST}" ]; then
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
