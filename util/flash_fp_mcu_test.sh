#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
FLASH_FP_MCU_SOURCE_ONLY=1 . "${SCRIPT_DIR}/flash_fp_mcu"

assert_eq() {
  local expected="$1"
  local actual="$2"
  local msg="$3"
  if [[ "${expected}" != "${actual}" ]]; then
    echo "FAIL: ${msg}: expected '${expected}', got '${actual}'" >&2
    exit 1
  fi
}

# 1. Defaults
parse_args
assert_eq "${FLAGS_FALSE}" "${FLAGS_read}" "default FLAGS_read"
assert_eq "${FLAGS_TRUE}" "${FLAGS_remove_flash_read_protect}" \
  "default FLAGS_remove_flash_read_protect"
assert_eq "${FLAGS_TRUE}" "${FLAGS_remove_flash_write_protect}" \
  "default FLAGS_remove_flash_write_protect"
assert_eq "4" "${FLAGS_retries}" "default FLAGS_retries"
assert_eq "115200" "${FLAGS_baudrate}" "default FLAGS_baudrate"
assert_eq "${FLAGS_FALSE}" "${FLAGS_hello}" "default FLAGS_hello"
assert_eq "${FLAGS_TRUE}" "${FLAGS_services}" "default FLAGS_services"
assert_eq "0" "${#POSITIONAL_ARGS[@]}" "default POSITIONAL_ARGS count"

# 2. Short flags and positional argument
parse_args -r -R 10 -B 9600 -H ec.bin
assert_eq "${FLAGS_TRUE}" "${FLAGS_read}" "-r sets FLAGS_read"
assert_eq "10" "${FLAGS_retries}" "-R sets FLAGS_retries"
assert_eq "9600" "${FLAGS_baudrate}" "-B sets FLAGS_baudrate"
assert_eq "${FLAGS_TRUE}" "${FLAGS_hello}" "-H sets FLAGS_hello"
assert_eq "1" "${#POSITIONAL_ARGS[@]}" "positional arg count"
assert_eq "ec.bin" "${POSITIONAL_ARGS[0]}" "positional arg value"

# 3. Long flags, negated boolean flags, and --key=value
parse_args --read --noread --noremove_flash_read_protect \
  --noremove_flash_write_protect --retries=8 --baudrate=57600 \
  --hello --nohello --noservices -- custom_fw.bin
assert_eq "${FLAGS_FALSE}" "${FLAGS_read}" "--noread sets FLAGS_read"
assert_eq "${FLAGS_FALSE}" "${FLAGS_remove_flash_read_protect}" \
  "--noremove_flash_read_protect"
assert_eq "${FLAGS_FALSE}" "${FLAGS_remove_flash_write_protect}" \
  "--noremove_flash_write_protect"
assert_eq "8" "${FLAGS_retries}" "--retries=8"
assert_eq "57600" "${FLAGS_baudrate}" "--baudrate=57600"
assert_eq "${FLAGS_FALSE}" "${FLAGS_hello}" "--nohello"
assert_eq "${FLAGS_FALSE}" "${FLAGS_services}" "--noservices"
assert_eq "custom_fw.bin" "${POSITIONAL_ARGS[0]}" "positional arg after --"

# 4. Error handling: unknown flag and missing values
ret=0
parse_args --unknown-flag 2>/dev/null || ret=$?
assert_eq "${EXIT_ARGUMENT}" "${ret}" \
  "expected unknown flag to return EXIT_ARGUMENT"

ret=0
parse_args -R 2>/dev/null || ret=$?
assert_eq "${EXIT_ARGUMENT}" "${ret}" \
  "expected missing -R argument to fail with EXIT_ARGUMENT"

ret=0
parse_args -B 2>/dev/null || ret=$?
assert_eq "${EXIT_ARGUMENT}" "${ret}" \
  "expected missing -B argument to fail with EXIT_ARGUMENT"

echo "PASS"
