#!/bin/bash
#
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Calculate hash of fips module and inject it into the .elf file.

SCRIPT="$(basename "$0")"

main() {
  local space_file="${1}"
  local buffer="${2}"
  local space="0"

  if [ ! -f "${space_file}" ] ; then
    echo "${space_file} doesn't exist"
    return 1
  fi
  space=$(cat "${space_file}")
  if (( space < buffer )); then
    echo "${space_file}: space in flash does not support ${buffer} byte buffer"
    return 1
  fi
  exit 0
}

main "$@"
