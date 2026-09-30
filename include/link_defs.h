/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Symbols from linker definitions
 */

#ifndef PLATFORM_EC_INCLUDE_LINK_DEFS_H_
#define PLATFORM_EC_INCLUDE_LINK_DEFS_H_

#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "task.h"
#include "test_util.h"

#include <linker.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Console commands */
extern const struct console_command __cmds[];
extern const struct console_command __cmds_end[];

/* Extension commands. */
extern const void *__extension_cmds;
extern const void *__extension_cmds_end;

/* I2C fake devices for unit testing */
extern const struct test_i2c_xfer __test_i2c_xfer[];
extern const struct test_i2c_xfer __test_i2c_xfer_end[];

#ifdef CONFIG_PLATFORM_EC_HOSTCMD
/* Host commands */
extern const struct host_command __hcmds[];
extern const struct host_command __hcmds_end[];
#endif

/* MKBP events */
extern const struct mkbp_event_source __mkbp_evt_srcs[];
extern const struct mkbp_event_source __mkbp_evt_srcs_end[];

/* Shared memory buffer.  Use via shared_mem.h interface. */
extern char __shared_mem_buf[];

/* Image sections. */
extern const void *__data_lma_start;
extern const void *__data_start;
extern const void *__data_end;

/* DRAM image sections. */
extern const void *__dram_data_lma_start;
extern void *__dram_data_start;
extern void *__dram_data_end;
extern void *__dram_bss_start;
extern void *__dram_bss_end;

#if defined(CHIP_VARIANT_MT8195) && defined(CONFIG_CHIP_MEMORY_REGIONS)
/* clear up NOLOAD region */
#define REGION(name, attr, start, size) \
	extern void *__##name##_start;  \
	extern void *__##name##_end;
#define REGION_LOAD(name, attr, start, size) \
	extern void *__##name##_start;       \
	extern void *__##name##_end;
#include "memory_regions.inc"
#undef REGION
#undef REGION_LOAD
#endif

/* Helper for special chip-specific memory sections */
#if defined(CONFIG_CHIP_MEMORY_REGIONS) || defined(CONFIG_DRAM_BASE)
#define __SECTION(name) __attribute__((section("." STRINGIFY(name) ".50_auto")))
#define __SECTION_KEEP(name) \
	__keep __attribute__((section("." STRINGIFY(name) ".keep.50_auto")))
#else
#define __SECTION(name)
#define __SECTION_KEEP(name)
#endif /* CONFIG_MEMORY_REGIONS */
#ifdef CONFIG_CHIP_UNCACHED_REGION
#define __uncached __SECTION(CONFIG_CHIP_UNCACHED_REGION)
#else
#define __uncached
#endif

#ifdef CONFIG_PRESERVE_LOGS
#define __preserved_logs(name) \
	__attribute__((section(".preserved_logs." STRINGIFY(name))))
/* preserved_logs section. */
extern const char __preserved_logs_start[];
extern const char __preserved_logs_size[];
#else
#define __preserved_logs(name)
#endif

/* __noinit_end_of_ram may not be used in RO */
#ifdef CONFIG_NOINIT_END_OF_RAM_SECTION
#define __noinit_end_of_ram(name) \
	__attribute__((section(".noinit_end_of_ram." STRINGIFY(name))))
extern const char __noinit_end_of_ram_start[];
extern const char __noinit_end_of_ram_end[];
#else
#define __noinit_end_of_ram(name) \
	BUILD_ASSERT(0, "Attempting to use noinit_end_of_ram when disabled")
#endif /* CONFIG_NOINIT_END_OF_RAM_SECTION */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_LINK_DEFS_H_ */
