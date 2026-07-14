#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Helper script to run the EC Firmware Docker environment with
# persistent caching and hardware device forwarding (CCD/Servo and
# Serial TTYs).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="${SCRIPT_DIR}/workspace"
CACHE_DIR="${WORKSPACE_DIR}/.cache/coreboot-sdk"
IMAGE_NAME="ec-builder"

# Ensure directories exist on host
mkdir -p "${WORKSPACE_DIR}"
mkdir -p "${CACHE_DIR}"

# Base Docker run arguments
DOCKER_ARGS=(
  -it
  --rm
  --privileged
  -v "${WORKSPACE_DIR}:/workspace"
  -v "${CACHE_DIR}:/root/.cache/coreboot-sdk"
)

# 1. Forward USB subsystem for CCD/Servo interface (vendor ID 18d1,
# device ID 5214)
if [ -d "/dev/bus/usb" ]; then
  echo "Configuring USB subsystem forwarding..."
  DOCKER_ARGS+=( -v "/dev/bus/usb:/dev/bus/usb" )
else
  echo "Warning: /dev/bus/usb not found. USB forwarding will be disabled."
fi

# 2. Forward TTY serial devices if they exist on the host
for tty_dev in /dev/ttyACM0 /dev/ttyACM1 /dev/ttyACM2; do
  if [ -c "${tty_dev}" ]; then
    echo "Forwarding serial device: ${tty_dev}"
    DOCKER_ARGS+=( --device="${tty_dev}:${tty_dev}" )
  else
    echo "Notice: Serial device ${tty_dev} not active on host, skipping..."
  fi
done

# Execute the container run
echo "Launching Docker container..."
exec docker run "${DOCKER_ARGS[@]}" "${IMAGE_NAME}" "$@"
