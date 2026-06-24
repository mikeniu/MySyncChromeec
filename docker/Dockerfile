# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Use a suitable base image, e.g., Ubuntu
FROM ubuntu:24.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install necessary dependencies for building EC firmware
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    git \
    curl \
    wget \
    python3 \
    python3-pip \
    python3-venv \
    libftdi-dev \
    libusb-1.0-0-dev \
    pkg-config \
    cmake \
    ninja-build \
    python3-yaml \
    python3-jsonschema \
    python3-pykwalify \
    python3-dateutil \
    gcc-multilib \
    libc6-dev-i386 \
    device-tree-compiler \
    clang \
    llvm \
    libc++-dev \
    libc++abi-dev \
    zstd \
    u-boot-tools \
    python3-libfdt \
    jq \
    libssl-dev \
    uuid-dev \
    libyaml-dev \
    vim \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Set the working directory
WORKDIR /workspace

# Create directory for cached repos
RUN mkdir -p /opt/repos/src/platform /opt/repos/src/third_party

ARG REPO_BASE=https://chromium.googlesource.com/chromiumos

# Clone repos at build time
RUN git clone --depth 1 \
    ${REPO_BASE}/platform/ec \
    /opt/repos/src/platform/ec
RUN git clone --depth 1 \
    ${REPO_BASE}/platform/dagwood \
    /opt/repos/src/platform/dagwood
RUN git clone --depth 1 \
    ${REPO_BASE}/third_party/zephyrproject \
    /opt/repos/src/third_party/zephyrproject
RUN git clone --depth 1 \
    ${REPO_BASE}/third_party/pigweed/pigweed \
    /opt/repos/src/third_party/pigweed
RUN git clone --depth 1 \
    ${REPO_BASE}/third_party/u-boot \
    /opt/repos/src/third_party/u-boot

# Sparse checkout for chromiumos-overlay
RUN mkdir -p /opt/repos/src/third_party/chromiumos-overlay && \
    cd /opt/repos/src/third_party/chromiumos-overlay && \
    git init && \
    git remote add origin \
        ${REPO_BASE}/overlays/chromiumos-overlay && \
    git config core.sparseCheckout true && \
    echo "eclass/" >> .git/info/sparse-checkout && \
    git pull --depth=1 origin main && \
    (git branch --set-upstream-to=origin/main master || \
     git branch --set-upstream-to=origin/main main || \
     true)

# Pre-install python dependencies to speed up runtime setup
# Install Zephyr build/run test dependencies
RUN ZEPHYR_REQS_DIR="/opt/repos/src/third_party/zephyrproject/zephyr/scripts" \
    && if [ -d "$ZEPHYR_REQS_DIR" ]; then \
        python3 -m pip install --no-cache-dir --break-system-packages \
            -r "$ZEPHYR_REQS_DIR/requirements.txt"; \
    fi

# Install zmake dependencies from .vpython3
RUN VPYTHON_FILE="/opt/repos/src/platform/ec/zephyr/zmake/.vpython3" && \
    if [ -f "$VPYTHON_FILE" ]; then \
        packages=$(grep -o 'infra/python/wheels/[a-zA-Z0-9_-]*' \
            "$VPYTHON_FILE" | \
            sed 's|infra/python/wheels/||g' | \
            sed 's|-py2_py3||g' | \
            sed 's|-py3||g' | \
            sort -u); \
        for pkg in $packages; do \
            case "$pkg" in \
                "pyyaml") pkg="PyYAML" ;; \
                "python-dateutil") pkg="python-dateutil" ;; \
                "ruamel_yaml") pkg="ruamel.yaml" ;; \
                # Skip packages already installed by Zephyr's requirements.txt
                # above that would be downgraded based on the .vpython
                # requirements
                "ruamel_yaml_clib") continue ;; \
                "coverage") continue ;; \
                "pytest") continue ;; \
                # Skip packages only used by zmake unit tests
                "hypothesis") continue ;; \
                "testfixtures") continue ;; \
            esac; \
            python3 -m pip install --no-cache-dir \
                --break-system-packages "$pkg"; \
        done; \
    fi

# Build and install rtkupdate host utility
RUN g++ -O2 -Wall -o /usr/bin/rtkupdate \
    /opt/repos/src/platform/ec/util/rtkupdate.cc \
    /opt/repos/src/platform/ec/util/uart_utils.cc \
    -I/opt/repos/src/platform/ec/util

# Build and install uartupdatetool host utility
RUN g++ -O2 -Wall -o /usr/bin/uartupdatetool \
    /opt/repos/src/platform/ec/util/uut/main.cc \
    /opt/repos/src/platform/ec/util/uut/cmd.cc \
    /opt/repos/src/platform/ec/util/uut/opr.cc \
    /opt/repos/src/platform/ec/util/uut/l_com_port.cc \
    /opt/repos/src/platform/ec/util/uut/lib_crc.cc \
    -I/opt/repos/src/platform/ec/util \
    -I/opt/repos/src/platform/ec/include

# Build and install futility (which provides dump_fmap)
RUN git clone --depth 1 \
    ${REPO_BASE}/platform/vboot_reference \
    /tmp/vboot_reference && \
    cd /tmp/vboot_reference && \
    make -j$(nproc) futil WERROR= USE_FLASHROM=0 && \
    make futil_install WERROR= USE_FLASHROM=0 && \
    cd / && \
    rm -rf /tmp/vboot_reference


# Copy the entrypoint script
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Set the entrypoint
ENTRYPOINT ["/entrypoint.sh"]
