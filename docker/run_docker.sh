#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Thin wrapper around "docker run" for the EC firmware container, adding the
# workspace mount and hardware device forwarding (CCD/Servo and Serial TTYs).

set -e

# Resolve the workspace against this script's location, not the caller's
# working directory.
WORKSPACE_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")/workspace")"

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

# Only allocate a TTY when attached to an interactive terminal, so that the
# wrapper stays usable from scripts and CI.
TTY_ARGS=()
if [ -t 0 ] && [ -t 1 ]; then
  TTY_ARGS=( -it )
fi

exec docker run --rm \
  "${TTY_ARGS[@]}" \
  --device-cgroup-rule="c *:* rmw" \
  -v "${WORKSPACE_DIR}:/workspace" \
  -v /dev:/dev \
  ec-builder "$@"
