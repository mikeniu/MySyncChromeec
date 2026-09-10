/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(PLATFORM_EC_INCLUDE_MKBP_EVENT_H_) || \
	defined(PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_ZEPHYR_MKBP_EVENT_H_)
#error "This file must only be included from mkbp_event.h. " \
	"Include mkbp_event.h directly"
#endif

#ifndef PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_ZEPHYR_MKBP_EVENT_H_
#define PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_ZEPHYR_MKBP_EVENT_H_

const struct mkbp_event_source *
zephyr_find_mkbp_event_source(uint8_t event_type);

/**
 * See include/mkbp_event.h for documentation.
 */
#define DECLARE_EVENT_SOURCE(_type, _func)                             \
	static const STRUCT_SECTION_ITERABLE(mkbp_event_source,        \
					     _cros_evtsrc_##_func) = { \
		.event_type = _type,                                   \
		.get_data = _func,                                     \
	}

#endif /* PLATFORM_EC_ZEPHYR_SHIM_INCLUDE_ZEPHYR_MKBP_EVENT_H_ */
