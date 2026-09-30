/*
 * Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_IOEXPANDER_H_
#define PLATFORM_EC_INCLUDE_IOEXPANDER_H_

#define ioex_signal gpio_signal
#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* IO expander signal definition structure */
struct ioex_info {
	/* Signal name */
	const char *name;

	/* IO expander port number */
	uint16_t ioex;

	/* IO port number in IO expander */
	uint16_t port;

	/* Bitmask on that port (1 << N) */
	uint32_t mask;

	/* Flags - the same as the GPIO flags */
	uint32_t flags;
};

/* Signal information from board.c.  Must match order from enum ioex_signal. */
extern const struct ioex_info ioex_list[];
extern void (*const ioex_irq_handlers[])(enum ioex_signal signal);
extern const int ioex_ih_count;

/* Get ioex_info structure for specified signal */
#define IOEX_GET_INFO(signal) (ioex_list + (signal) - IOEX_SIGNAL_START)

struct ioexpander_drv {
	/* Initialize IO expander chip/driver */
	int (*init)(int ioex);
	/* Get the current level of the IOEX pin */
	int (*get_level)(int ioex, int port, int mask, int *val);
	/* Set the level of the IOEX pin */
	int (*set_level)(int ioex, int port, int mask, int val);
	/* Get flags for the IOEX pin */
	int (*get_flags_by_mask)(int ioex, int port, int mask, int *flags);
	/* Set flags for the IOEX pin */
	int (*set_flags_by_mask)(int ioex, int port, int mask, int flags);
	/* Enable/disable interrupt for the IOEX pin */
	int (*enable_interrupt)(int ioex, int port, int mask, int enable);
#ifdef CONFIG_IO_EXPANDER_SUPPORT_GET_PORT
	/* Read levels for whole IOEX port */
	int (*get_port)(int ioex, int port, int *val);
#endif
};

/* IO expander default init disabled. No I2C communication will be attempted. */
#define IOEX_FLAGS_DEFAULT_INIT_DISABLED BIT(0)
/* IO Expander has been initialized */
#define IOEX_FLAGS_INITIALIZED BIT(1)

/*
 * BITS 24 to 31 are used by io-expander drivers that need to control multiple
 * devices
 */
#define IOEX_FLAGS_CUSTOM_BIT(x) BUILD_CHECK_INLINE(BIT(x), BIT(x) & 0xff000000)

struct ioexpander_config_t {
	/* Physical I2C port connects to the IO expander chip. */
	int i2c_host_port;
	/* I2C address */
	int i2c_addr_flags;
	/*
	 * Pointer to the specific IO expander chip's ops defined in
	 * the struct ioexpander_drv.
	 */
	const struct ioexpander_drv *drv;
	/* Config flags for this IO expander chip. See IOEX_FLAGS_* */
	uint32_t flags;
};

extern struct ioexpander_config_t ioex_config[];

#define ioex_enable_interrupt gpio_enable_interrupt
#define ioex_disable_interrupt gpio_disable_interrupt

#ifdef CONFIG_GPIO_GET_EXTENDED
static inline int ioex_get_flags(enum gpio_signal signal, int *flags)
{
	*flags = gpio_get_flags(signal);
	return EC_SUCCESS;
}
#endif

static inline int ioex_set_flags(enum gpio_signal signal, int flags)
{
	gpio_set_flags(signal, flags);
	return EC_SUCCESS;
}

static inline int ioex_get_level(enum gpio_signal signal, int *val)
{
	*val = gpio_get_level(signal);
	return EC_SUCCESS;
}

static inline int ioex_set_level(enum gpio_signal signal, int val)
{
	gpio_set_level(signal, val);
	return EC_SUCCESS;
}

int ioex_init(int ioex);

static inline const char *ioex_get_name(enum ioex_signal signal)
{
	return gpio_get_name(signal);
}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_IOEXPANDER_H_ */
