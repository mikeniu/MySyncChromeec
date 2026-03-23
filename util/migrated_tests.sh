#!/bin/bash
#
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

exit_code=0

# Some tests were mainly copy paste to Zephyr. Add a warning to make sure
# fixes are applied for both versions.
migrated_tests="
test/abort.c
test/aes.cc
test/benchmark.cc
test/boringssl_crypto.cc
test/cortexm_fpu.c
test/crc.c
test/exception.cc
test/flash_write_protect.c
test/fp_transport.c
test/fpsensor_auth_crypto_stateful.cc
test/fpsensor_auth_crypto_stateless.cc
test/fpsensor_crypto.cc
test/fpsensor_debug.cc
test/fpsensor_hw.cc
test/fpsensor_utils.cc
test/ftrapv.c
test/libc_printf.c
test/libcxx.cc
test/malloc.c
test/otp_key.c
test/null_pointer.c
test/panic_data.c
test/panic.c
test/printf.c
test/queue.c
test/ram_lock.c
test/restricted_console.c
test/rng_benchmark.cc
test/rollback.c
test/rollback_entropy.c
test/sbrk.c
test/sha256.c
test/static_if.c
test/std_vector.cc
test/stdlib.c
test/system_is_locked.c
test/timer.cc
test/tpm_seed_clear.cc
test/unaligned_access.cc
test/utils_str.c
test/utils.c
test/watchdog.cc"

for file in "$@"; do
  ec_file="${file##**/platform/ec/}"

  if [[ ${migrated_tests} == *"${file}"* ]]; then
    echo -n "WARNING: ${ec_file} is not used in Zephyr EC. The test "
    echo -n "has been migrated to Zephyr. Make sure you apply the "
    echo -n "same fix for the Zephyr version in zephyr/test directory "
    echo "of the ec repo at src/platform/ec"
    exit_code=1
    continue
  fi
done

exit "${exit_code}"
