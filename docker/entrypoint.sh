#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -e

# Adopt the owner of /workspace as the build user, then re-exec as that
# user. A bind mount exposes the host inode, so this recovers the UID and
# GID of whoever created the workspace directory, and build artifacts land
# on the host owned by that user.
#
# The gosu below re-enters this same script as hostuser, so the root test is
# also what stops the second pass from repeating the setup.
if [ "$(id -u)" = "0" ]; then
    HOST_UID="$(stat -c '%u' /workspace)"
    HOST_GID="$(stat -c '%g' /workspace)"
    if [ "${HOST_UID}" != "0" ]; then
        # Reuse the image's group if it already has one with this GID.
        if ! getent group "${HOST_GID}" >/dev/null; then
            groupadd -g "${HOST_GID}" hostuser
        fi
        # useradd warns when the UID is outside the range it assigns by
        # itself, as large host UIDs often are, so only show its output
        # if it fails.
        if ! USERADD_OUTPUT="$(useradd -u "${HOST_UID}" -g "${HOST_GID}" \
                -m -s /bin/bash hostuser 2>&1)"; then
            echo "${USERADD_OUTPUT}" >&2
            exit 1
        fi
        # Grant access to serial TTYs and USB devices for flashing/debug
        usermod -aG dialout,plugdev hostuser
        # Prepare devutils directory for monitor binary installation
        mkdir -p /usr/share/ec-devutils
        chown -R "${HOST_UID}:${HOST_GID}" /usr/share/ec-devutils
        chmod 755 /entrypoint.sh
        exec gosu hostuser /bin/bash /entrypoint.sh "$@"
    fi
fi

# Default to skipping remote git pulls and pip checks; pass --update to
# pull the latest upstream changes.
UPDATE=""
ARGS=()
for arg in "$@"; do
    case "${arg}" in
        --update)
            UPDATE=1
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

# Populate a repository from the build-time cache if missing, and pull
# upstream changes when UPDATE is set.
setup_repo() {
    local target_dir="${1}"
    local repo_name="${2}"

    if [ ! -d "${target_dir}" ]; then
        local cached_dir="${target_dir//\/workspace/${CACHE_BASE}}"
        if [ ! -d "${cached_dir}" ]; then
            echo "Error: No ${repo_name} checkout in ${target_dir}" \
                 "or ${cached_dir}." >&2
            return 1
        fi
        echo "Populating ${repo_name} from build-time cache..."
        mkdir -p "$(dirname "${target_dir}")"
        cp -a "${cached_dir}" "${target_dir}"
    fi

    if [ -n "${UPDATE}" ]; then
        if (cd "${target_dir}" && git diff --quiet && \
            git diff --cached --quiet); then
            echo "Updating ${repo_name} (fetching deltas)..."
            (cd "${target_dir}" && git pull --quiet)
        else
            echo "Warning: ${repo_name} has local changes." \
                 "Skipping automatic update to avoid conflicts."
        fi
    fi
}

if [ -z "${UPDATE}" ]; then
    echo "Skipping repository updates (pass --update to sync)..."
fi

# Set up the repositories in parallel. A bare "wait" returns 0 however the
# jobs exit, so remember which repository each job handles and wait on each.
declare -A REPO_JOBS=()
start_setup_repo() {
    setup_repo "$@" &
    REPO_JOBS[$!]="${2}"
}
start_setup_repo "/workspace/src/platform/ec" "EC firmware"
start_setup_repo "/workspace/src/platform/dagwood" "Dagwood"
start_setup_repo "/workspace/src/third_party/zephyrproject" "Zephyr Project"
start_setup_repo "/workspace/src/third_party/pigweed" "Pigweed"
start_setup_repo "/workspace/src/third_party/u-boot" "U-Boot"
start_setup_repo "/workspace/src/third_party/chromiumos-overlay" \
    "ChromiumOS Overlay"
REPO_SETUP_FAILED=""
for pid in "${!REPO_JOBS[@]}"; do
    if ! wait "${pid}"; then
        echo "Error: Failed to set up ${REPO_JOBS[${pid}]}." >&2
        REPO_SETUP_FAILED=1
    fi
done
if [ -n "${REPO_SETUP_FAILED}" ]; then
    exit 1
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

    if [ -n "${UPDATE}" ]; then
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

# Set up Realtek monitor binary (rtk_flame)
MONITOR_CACHE="/workspace/.cache/rts5915_flash_upload.bin"
MONITOR_DEST_DIR="/usr/share/ec-devutils"
MONITOR_DEST="${MONITOR_DEST_DIR}/rts5915_flash_upload.bin"

if [ ! -f "${MONITOR_CACHE}" ]; then
    if [ -f "${MONITOR_DEST}" ]; then
        echo "Copying pre-installed Realtek monitor binary to cache..."
        mkdir -p "$(dirname "${MONITOR_CACHE}")"
        cp "${MONITOR_DEST}" "${MONITOR_CACHE}"
    else
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
    else
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

# Run the requested command (if any), or drop to bash
if [ $# -eq 0 ]; then
    exec /bin/bash
else
    exec "$@"
fi
