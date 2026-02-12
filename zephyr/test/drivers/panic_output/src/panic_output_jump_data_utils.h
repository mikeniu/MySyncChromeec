/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __PANIC_OUTPUT_JUMP_DATA_UTILS_H__
#define __PANIC_OUTPUT_JUMP_DATA_UTILS_H__

#include <stdint.h>

#include <zephyr/ztest.h>

#include <sys/types.h>

/**
 * @brief Returns a pointer to an object (such as a struct jump_data) of type
 *        TYPE at the end of the mock_jump_data memory region, plus an optional,
 *        additional offset of OFFSET bytes. OFFSET can be used to help get the
 *        pointer after jump data has been moved by get_panic_data_write(), or
 *        left as zero to get the pre-move location.
 */
#define GET_JUMP_DATA_PTR(TYPE, OFFSET)                                    \
	((TYPE *)(mock_jump_data + sizeof(mock_jump_data) - sizeof(TYPE) + \
		  (OFFSET)))

/**
 * @brief Implements the fields of a version 1 jump_data header.
 *
 */
struct jump_data_v1 {
	/** V1 Jump data header, always goes at end. See sysjump.h for info */
	uint32_t reset_flags;
	int32_t version;
	int32_t magic;
} __packed;

/**
 * @brief Implements a V2 jump_data header plus some extra bytes in front that
 *        represent jump tag data. This reflects how they are stored in the EC's
 *        memory. The jump_tag_total field in jdata stored how many bytes of
 *        preceding jump tag data exists.
 */
struct jump_data_v2_plus_tags {
	/** Arbitrary amount of jump tag data. */
	uint8_t tag_data[8] __packed;
	/** V2 Jump data header, always goes at end. See sysjump.h for info */
	struct {
		int32_t jump_tag_total;
		uint32_t reset_flags;
		int32_t version;
		uint32_t magic;
	} __packed jdata;
} __packed;

/**
 * @brief Implements a V3 jump_data header with space in front for jump tag data
 */
struct jump_data_v3_plus_tags {
	/** Arbitrary amount of jump tag data. */
	uint8_t tag_data[8] __packed;
	/** V3 Jump data header, always goes at end. See sysjump.h for info */
	struct {
		uint32_t reserved0;
		int32_t struct_size;
		int32_t jump_tag_total;
		uint32_t reset_flags;
		int32_t version;
		uint32_t magic;
	} __packed jdata;
} __packed;

/**
 * @brief Implements a fake V99 jump_data header with space in front for jump
 * tag data
 */
struct jump_data_v99_plus_tags {
	/** Arbitrary amount of jump tag data. */
	uint8_t tag_data[8] __packed;
	/** Fake V99 Jump data header */
	struct {
		uint64_t extra_field;
		uint32_t reserved0;
		int32_t struct_size;
		int32_t jump_tag_total;
		uint32_t reset_flags;
		int32_t version;
		uint32_t magic;
	} __packed jdata;
} __packed;

#endif /* __PANIC_OUTPUT_JUMP_DATA_UTILS_H__ */