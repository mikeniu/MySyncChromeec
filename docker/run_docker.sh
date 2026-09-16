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

# Hardware access is granted in two independent layers:
#
# 1. Bind-mounting /dev makes the host device nodes visible, including any
#    device hotplugged or re-enumerated after the container starts. A device
#    that resets into a bootloader and comes back as a different ttyACM
#    number stays usable.
# 2. The device cgroup controller still denies open() on anything outside
#    Docker's small default allowlist, so a rule is needed as well.
#    "c *:* rmw" grants character devices only: serial ports and usbfs work,
#    while block devices (disks) stay inaccessible and the default seccomp,
#    AppArmor and capability restrictions remain in place. This is why
#    --privileged, which drops all of those protections, is not used.
#
# To scope this down further, the rule can be replaced by one rule per driver
# major (the flag is repeatable):
#   --device-cgroup-rule="c 166:* rmw"   # ttyACM* (CDC-ACM: Dagwood, servo)
#   --device-cgroup-rule="c 188:* rmw"   # ttyUSB* (FTDI, e.g. servo v2)
#   --device-cgroup-rule="c 189:* rmw"   # /dev/bus/usb/* (libusb, CCD)
# Those majors are statically assigned so they are stable across distros, but
# the list has to be extended for hardware using any other driver, so the
# wildcard is the default.
DOCKER_ARGS=(
  --rm
  --device-cgroup-rule="c *:* rmw"
  -e "HOST_UID=$(id -u)"
  -e "HOST_GID=$(id -g)"
  -v "${WORKSPACE_DIR}:/workspace"
  -v "/dev:/dev"
)

# Only allocate TTY if attached to an interactive terminal
if [ -t 0 ] && [ -t 1 ]; then
  DOCKER_ARGS+=( -it )
fi

# Execute the container run
echo "Launching Docker container..."
exec docker run "${DOCKER_ARGS[@]}" "${IMAGE_NAME}" "$@"
