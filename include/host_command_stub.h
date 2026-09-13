/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_HOST_COMMAND_STUB_H
#define __CROS_EC_HOST_COMMAND_STUB_H

/*
 * Stub definitions for the host command interface.
 *
 * This header is included when neither CONFIG_EC_HOST_CMD (Zephyr host command
 * subsystem) nor CONFIG_PLATFORM_EC_HOSTCMD (legacy host command shim) is
 * enabled. It provides stub type definitions and no-op registration macros so
 * that source files implementing host commands can compile without requiring
 * preprocessor guards around every individual command definition.
 *
 * Unreferenced handler functions are discarded at link time via linker section
 * garbage collection (--gc-sections).
 */

#include "common.h"
#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ec_host_cmd_handler_args host_cmd_handler_args
#define ec_host_cmd_status ec_status

#define EC_HOST_CMD_SUCCESS EC_RES_SUCCESS
#define EC_HOST_CMD_INVALID_COMMAND EC_RES_INVALID_COMMAND
#define EC_HOST_CMD_ERROR EC_RES_ERROR
#define EC_HOST_CMD_INVALID_PARAM EC_RES_INVALID_PARAM
#define EC_HOST_CMD_ACCESS_DENIED EC_RES_ACCESS_DENIED
#define EC_HOST_CMD_INVALID_RESPONSE EC_RES_INVALID_RESPONSE
#define EC_HOST_CMD_INVALID_VERSION EC_RES_INVALID_VERSION
#define EC_HOST_CMD_INVALID_CHECKSUM EC_RES_INVALID_CHECKSUM
#define EC_HOST_CMD_IN_PROGRESS EC_RES_IN_PROGRESS
#define EC_HOST_CMD_UNAVAILABLE EC_RES_UNAVAILABLE
#define EC_HOST_CMD_TIMEOUT EC_RES_TIMEOUT
#define EC_HOST_CMD_OVERFLOW EC_RES_OVERFLOW
#define EC_HOST_CMD_INVALID_HEADER EC_RES_INVALID_HEADER
#define EC_HOST_CMD_REQUEST_TRUNCATED EC_RES_REQUEST_TRUNCATED
#define EC_HOST_CMD_RESPONSE_TOO_BIG EC_RES_RESPONSE_TOO_BIG
#define EC_HOST_CMD_BUS_ERROR EC_RES_BUS_ERROR
#define EC_HOST_CMD_BUSY EC_RES_BUSY
#define EC_HOST_CMD_INVALID_HEADER_VERSION EC_RES_INVALID_HEADER_VERSION
#define EC_HOST_CMD_INVALID_HEADER_CRC EC_RES_INVALID_HEADER_CRC
#define EC_HOST_CMD_INVALID_DATA_CRC EC_RES_INVALID_DATA_CRC
#define EC_HOST_CMD_DUP_UNAVAILABLE EC_RES_DUP_UNAVAILABLE
#define EC_HOST_CMD_MAX EC_RES_MAX

#define EC_HOST_CMD_HANDLER(id, function, version_mask, ...) \
	__maybe_unused static void _stub_hc_##function(void) \
	{                                                    \
		(void)function;                              \
	}

#define EC_HOST_CMD_HANDLER_REQ_ONLY(id, function, version_mask, request_type) \
	EC_HOST_CMD_HANDLER(id, function, version_mask)

#define EC_HOST_CMD_HANDLER_RESP_ONLY(id, function, version_mask, \
				      response_type)              \
	EC_HOST_CMD_HANDLER(id, function, version_mask)

#define EC_HOST_CMD_HANDLER_UNBOUND(id, function, version_mask) \
	EC_HOST_CMD_HANDLER(id, function, version_mask)

#define DECLARE_HOST_COMMAND(command, routine, version_mask) \
	__maybe_unused static void _stub_dhc_##routine(void) \
	{                                                    \
		(void)routine;                               \
	}

#define DECLARE_PRIVATE_HOST_COMMAND(command, routine, version_mask) \
	DECLARE_HOST_COMMAND(command, routine, version_mask)

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_HOST_COMMAND_STUB_H */
