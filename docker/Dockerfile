# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Use a suitable base image, e.g., Ubuntu
FROM ubuntu:24.04

# Prevent interactive prompts during package installation and allow pip to
# break system packages
ENV DEBIAN_FRONTEND=noninteractive \
    PIP_BREAK_SYSTEM_PACKAGES=1

# Install necessary dependencies for building EC firmware
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    git \
    curl \
    wget \
    python3 \
    python3-dev \
    python3-pip \
    python3-venv \
    libftdi-dev \
    libusb-1.0-0-dev \
    pkg-config \
    cmake \
    ninja-build \
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
# Sparse checkout for zephyrproject
RUN mkdir -p /opt/repos/src/third_party/zephyrproject && \
    cd /opt/repos/src/third_party/zephyrproject && \
    git init -b main && \
    git remote add origin \
        ${REPO_BASE}/third_party/zephyrproject && \
    git config core.sparseCheckout true && \
    echo "/zephyr/" >> .git/info/sparse-checkout && \
    echo "/modules/hal/cmsis_6/" >> .git/info/sparse-checkout && \
    echo "/modules/lib/picolibc/" >> .git/info/sparse-checkout && \
    echo "/modules/lib/nanopb/" >> .git/info/sparse-checkout && \
    git pull --depth=1 origin main && \
    git branch --set-upstream-to=origin/main main
RUN git clone --depth 1 \
    ${REPO_BASE}/third_party/pigweed/pigweed \
    /opt/repos/src/third_party/pigweed
# Sparse checkout for u-boot
RUN mkdir -p /opt/repos/src/third_party/u-boot && \
    cd /opt/repos/src/third_party/u-boot && \
    git clone --depth 1 --no-checkout \
        ${REPO_BASE}/third_party/u-boot \
        . && \
    git config core.sparseCheckout true && \
    echo "/tools/binman/" >> .git/info/sparse-checkout && \
    echo "/tools/dtoc/" >> .git/info/sparse-checkout && \
    echo "/tools/patman/" >> .git/info/sparse-checkout && \
    echo "/tools/buildman/" >> .git/info/sparse-checkout && \
    git checkout

# Sparse checkout for chromiumos-overlay
RUN mkdir -p /opt/repos/src/third_party/chromiumos-overlay && \
    cd /opt/repos/src/third_party/chromiumos-overlay && \
    git init -b main && \
    git remote add origin \
        ${REPO_BASE}/overlays/chromiumos-overlay && \
    git config core.sparseCheckout true && \
    echo "/eclass/coreboot-sdk-ec-dependencies.eclass" \
        >> .git/info/sparse-checkout && \
    git pull --depth=1 origin main && \
    git branch --set-upstream-to=origin/main main

# Pre-install python dependencies to speed up runtime setup
# Install Zephyr build/run test dependencies
RUN ZEPHYR_REQS_DIR="/opt/repos/src/third_party/zephyrproject/zephyr/scripts" \
    && if [ -d "$ZEPHYR_REQS_DIR" ]; then \
        python3 -m pip install \
            --no-cache-dir \
            --break-system-packages \
            --ignore-installed \
            -r "$ZEPHYR_REQS_DIR/requirements.txt"; \
    fi

# Install zmake dependencies from .vpython3 in a single batch
RUN VPYTHON_FILE="/opt/repos/src/platform/ec/zephyr/zmake/.vpython3" && \
    if [ -f "$VPYTHON_FILE" ]; then \
        packages=$(grep -o 'infra/python/wheels/[a-zA-Z0-9_-]*' \
            "$VPYTHON_FILE" | \
            sed 's|infra/python/wheels/||g; s|-py2_py3||g; s|-py3||g' | \
            sort -u); \
        to_install=""; \
        for pkg in $packages; do \
            case "$pkg" in \
                "pyyaml") pkg="PyYAML" ;; \
                "python-dateutil") pkg="python-dateutil" ;; \
                "ruamel_yaml") pkg="ruamel.yaml" ;; \
                "ruamel_yaml_clib"|"coverage"|"pytest"| \
                "hypothesis"|"testfixtures") continue ;; \
            esac; \
            to_install="${to_install} ${pkg}"; \
        done; \
        if [ -n "${to_install}" ]; then \
            python3 -m pip install --no-cache-dir \
                --break-system-packages --ignore-installed ${to_install}; \
        fi; \
    fi

# Install zmake in system Python
RUN python3 -m pip install --no-cache-dir --no-deps \
    /opt/repos/src/platform/ec/zephyr/zmake

# Build and install rtkupdate host utility
RUN g++ -O2 -Wall -o /usr/bin/rtkupdate \
    /opt/repos/src/platform/ec/util/rtkupdate.cc \
    /opt/repos/src/platform/ec/util/uart_utils.cc \
    -I/opt/repos/src/platform/ec/util

# Build and install itecomdbgr host utility
RUN g++ -O2 -Wall -o /usr/bin/itecomdbgr \
    /opt/repos/src/platform/ec/util/itecomdbgr.cc \
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
