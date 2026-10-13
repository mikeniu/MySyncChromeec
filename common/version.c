/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Embed firmware version number in the binary */

#include "common.h"
#include "compile_time_macros.h"
#include "cros_version.h"
#include "ec_commands.h"
#include "ec_version.h"
#include "stddef.h"
#include "system.h"

#include <stdint.h>

#ifndef CROS_FWID32
#define CROS_FWID32 ""
#endif

BUILD_ASSERT(CONFIG_ROLLBACK_VERSION >= 0);
BUILD_ASSERT(CONFIG_ROLLBACK_VERSION <= INT32_MAX);

const struct image_data __keep current_image_data FIXED_SECTION("ver") = {
	.cookie1 = CROS_EC_IMAGE_DATA_COOKIE1,
	.version = CROS_EC_VERSION32,
#ifndef TEST_BUILD
	.size = (const uintptr_t)&__image_size,
#endif
	.rollback_version = CONFIG_ROLLBACK_VERSION,
	.cookie2 = CROS_EC_IMAGE_DATA_COOKIE2,
	.cros_fwid = CROS_FWID32,
	.cookie3 = CROS_EC_IMAGE_DATA_COOKIE3,
};
BUILD_ASSERT(sizeof(CROS_EC_VERSION32) <= 32);
BUILD_ASSERT(sizeof(CROS_FWID32) <= 32);

#ifdef CONFIG_CROS_FWID_VERSION
const char build_info[] __keep __attribute__((section(".rodata.buildinfo"))) =
	VERSION " " CROS_FWID32 " " DATE " " BUILDER;
#else
const char build_info[] __keep __attribute__((section(".rodata.buildinfo"))) =
	VERSION " " DATE " " BUILDER;
#endif
