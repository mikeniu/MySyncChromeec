/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Preserved ring buffers exists in noinit memory at the end of used ram. This
 * ring buffer is intended to be preserved across reboots and sysjumps.
 *
 * Start and end magic canaries along with preserved properties ensure the
 * buffer is uncorrupted. Corruption may be caused by power loss or
 * misalignment with RO ram.
 *
 * Preserved ring buffers are designed to be low overhead and fast, especially
 * for writes. This makes these ring buffers well suited for tracing and logging
 * use cases. Locking may be provided at a higher level.
 *
 * This ring buffer does not support dequeuing, it can only be cleared by
 * resetting the buffer. This buffer is intended to be dumped all at once.
 */

#ifndef PLATFORM_EC_INCLUDE_PRESERVED_RING_BUF_H_
#define PLATFORM_EC_INCLUDE_PRESERVED_RING_BUF_H_

#include "link_defs.h"

#define PRESERVED_RING_BUF_START_MAGIC 0x50524253
#define PRESERVED_RING_BUF_END_MAGIC 0x50524245

/* Static ring buffer properties */
typedef struct {
	/* Buffer capacity in bytes */
	const uint32_t capacity;
	/* User specified version. Update if higher level schema changes. */
	const uint32_t version;
} preserved_ring_buf_const_t;

/* Preserved ring buffer properties */
typedef struct {
	/* Start magic canary */
	uint32_t start_magic;
	/* Write head. Only increments. */
	uint32_t head;
	/* Capacity of buffer in bytes */
	uint32_t capacity;
	/* User specified version. Update if higher level schema changes. */
	uint32_t version;
} preserved_ring_buf_properties_t;

typedef struct {
	const preserved_ring_buf_const_t constants;
	preserved_ring_buf_properties_t *properties;
	uint8_t *buffer;
	uint32_t *end_magic;
} preserved_ring_buf_t;

#define DECLARE_PRESERVED_RING_BUF(name, _capacity, _version)                    \
	/* Buffer capacity must be a power of 2 */                               \
	BUILD_ASSERT((_capacity & (_capacity - 1)) == 0);                        \
	static struct {                                                          \
		preserved_ring_buf_properties_t properties;                      \
		uint8_t buffer[_capacity];                                       \
		uint32_t end_magic;                                              \
	} _preserved_ring_buf_##name##_storage __uncached __aligned(4)           \
		__noinit_end_of_ram(_preserved_ring_buf_##name##_storage);       \
	static const preserved_ring_buf_t _preserved_ring_buf_##name##_data = {\
		.buffer = _preserved_ring_buf_##name##_storage.buffer,         \
		.properties =                                                  \
			&_preserved_ring_buf_##name##_storage.properties,      \
		.end_magic = &_preserved_ring_buf_##name##_storage.end_magic,  \
		.constants = {                                                 \
			.capacity = _capacity,                                 \
			.version = _version,                                   \
		},                                                             \
	}; \
	static const preserved_ring_buf_t *const name =                          \
		&_preserved_ring_buf_##name##_data
/*
 * Zero the buffer and set all preserved properties to the constant
 * values.
 */
void preserved_ring_buf_reset(const preserved_ring_buf_t *buf);

/*
 * Check if buffer is valid.
 */
bool preserved_ring_buf_verify(const preserved_ring_buf_t *buf);

/*
 * Returns the length of the ring buffer.
 */
uint32_t preserved_ring_buf_len(const preserved_ring_buf_t *buf);

/*
 * Returns preserved user defined version
 */
uint32_t preserved_ring_buf_version(const preserved_ring_buf_t *buf);

/*
 * Returns preserved buffer capacity
 */
uint32_t preserved_ring_buf_capacity(const preserved_ring_buf_t *buf);

/*
 * Write a byte to the buffer and increment head.
 */
static inline void preserved_ring_buf_write(const preserved_ring_buf_t *buf,
					    const uint8_t byte)
{
	/* Capacity is a power of 2, so bitmask replaces modulo. */
	const uint32_t index =
		(buf->properties->head++ & (buf->constants.capacity - 1));

	buf->buffer[index] = byte;
}

/*
 * Returns the byte at the offset from the tail.
 */
uint8_t preserved_ring_buf_read(const preserved_ring_buf_t *buf,
				uint32_t offset);

#endif /* PLATFORM_EC_INCLUDE_PRESERVED_RING_BUF_H_ */
