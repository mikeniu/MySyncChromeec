/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "preserved_ring_buf.h"

#if defined(CONFIG_CROS_EC_RO)
#error Preserved ring buffer should not be enabled in RO images.
#endif

#ifndef CONFIG_NOINIT_END_OF_RAM_SECTION
#error Preserved ring buffer depends on CONFIG_NOINIT_END_OF_RAM_SECTION
#endif

/*
 * On reset, the buffer is zero'd and all properties are set to the constant
 * values.
 */
void preserved_ring_buf_reset(const preserved_ring_buf_t *buf)
{
	buf->properties->checksum = 0;
	buf->properties->head = 0;
	buf->properties->version = buf->constants.version;
	buf->properties->capacity = buf->constants.capacity;
	memset(buf->buffer, 0, buf->constants.capacity);
}

uint32_t preserved_ring_buf_capacity(const preserved_ring_buf_t *buf)
{
	return buf->properties->capacity;
}

uint32_t preserved_ring_buf_version(const preserved_ring_buf_t *buf)
{
	return buf->properties->version;
}

/*
 * Checksum is across entire buffer, even if partially filled. This works
 * because the buffer is zero'd on reset.
 */
static uint32_t
preserved_ring_buf_calc_checksum(const preserved_ring_buf_t *buf)
{
	uint32_t sum = 0;
	for (int i = 0; i < buf->constants.capacity; i++) {
		sum += buf->buffer[i];
	}
	return sum;
}

/*
 * A buffer is valid if the preserved properties match the expected
 * constant properties and the checksum matches.
 */
bool preserved_ring_buf_verify(const preserved_ring_buf_t *buf)
{
	return (buf->properties->version == buf->constants.version &&
		buf->properties->capacity == buf->constants.capacity &&
		buf->properties->checksum ==
			preserved_ring_buf_calc_checksum(buf));
}

/*
 * Returns the length of the ring buffer.
 * Before the buffer is filled, the length matches head.
 * After the buffer is filled, the length matches the buffer capacity.
 */
uint32_t preserved_ring_buf_len(const preserved_ring_buf_t *buf)
{
	return (buf->properties->head < buf->constants.capacity) ?
		       buf->properties->head :
		       buf->constants.capacity;
}

/*
 * Read a byte at a given offset.
 * Offset is wrapped to buffer capacity.
 * Tail is 0 until head passes buffer capacity, then it matches head.
 */
uint8_t preserved_ring_buf_read(const preserved_ring_buf_t *buf,
				const uint32_t offset)
{
	uint32_t tail = 0;
	if (buf->properties->head > buf->constants.capacity)
		tail = buf->properties->head;
	/* Capacity is a power of 2, so bitmask replaces modulo. */
	const uint32_t index = (tail + offset) & (buf->constants.capacity - 1);
	return buf->buffer[index];
}
