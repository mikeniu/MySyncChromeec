/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_BOARD_HYPERDEBUG_BOARD_UTIL_H_
#define PLATFORM_EC_BOARD_HYPERDEBUG_BOARD_UTIL_H_

#include "stddef.h"
#include "stdint.h"

/*
 * Calculate prescaler value (1-256) with each possible base frequencies, and
 * see which one gets closest to the requested frequency, without exceeding it.
 *
 * best_divisor will be the 0-255, one less than actual divisor.
 * best_base_frequency_index will be index into provided array.
 */
void find_best_divisor(uint32_t desired_freq, const uint32_t base_frequencies[],
		       size_t num_base_frequencies, uint8_t *best_divisor,
		       size_t *best_base_frequency_index);

#endif /* PLATFORM_EC_BOARD_HYPERDEBUG_BOARD_UTIL_H_ */
