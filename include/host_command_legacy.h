/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_HOST_COMMAND_LEGACY_H_
#define PLATFORM_EC_INCLUDE_HOST_COMMAND_LEGACY_H_

#include "common.h"
#include "ec_commands.h"

#include <zephyr/linker/iterable_sections.h>

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

struct host_cmd_handler_args;

typedef enum ec_status (*ec_host_cmd_handler_cb)(
	struct host_cmd_handler_args *args);

#define EC_HOST_CMD_HANDLER(id, function, version_mask, request_type, \
			    response_type)                            \
	DECLARE_HOST_COMMAND(id, (ec_host_cmd_handler_cb)function, version_mask)

#define EC_HOST_CMD_HANDLER_REQ_ONLY(id, function, version_mask, request_type) \
	DECLARE_HOST_COMMAND(id, (ec_host_cmd_handler_cb)function, version_mask)

#define EC_HOST_CMD_HANDLER_RESP_ONLY(id, function, version_mask, \
				      response_type)              \
	DECLARE_HOST_COMMAND(id, (ec_host_cmd_handler_cb)function, version_mask)

#define EC_HOST_CMD_HANDLER_UNBOUND(id, function, version_mask) \
	DECLARE_HOST_COMMAND(id, (ec_host_cmd_handler_cb)function, version_mask)

/* Args for host packet handler */
struct host_packet {
	/*
	 * The driver that receives the command sets up the send_response()
	 * handler. Once the command is processed this handler is called to
	 * send the response back to the host.
	 */
	void (*send_response)(struct host_packet *pkt);

	/*
	 * Input request data. If request and response buffers overlap,
	 * then request_temp must be non-null and be large enough to store the
	 * entire request buffer. The request_temp buffer will then be used
	 * as the buffer passed into the command handlers.
	 */
	const void *request;

	/*
	 * Input request temp buffer. If this is non-null, the data has not
	 * been copied from here into the request buffer yet. The host command
	 * handler should do so while verifying the command. The interface
	 * can't do it, because it doesn't know how much to copy.
	 */
	void *request_temp;

	/*
	 * Maximum size of request the interface can handle, in bytes. The
	 * buffers pointed to by *request and *request_temp must be at least
	 * this big.
	 */
	uint16_t request_max;

	/* Size of input request data, in bytes */
	uint16_t request_size;

	/* Pointer to output response data buffer */
	void *response;

	/* Maximum size of response buffer provided to command handler */
	uint16_t response_max;

	/* Size of output response data, in bytes */
	uint16_t response_size;

	/*
	 * Error from driver; if this is non-zero, host command handler will
	 * return a properly formatted error response packet rather than
	 * calling a command handler.
	 */
	uint16_t driver_result;
};

/* Host command */
struct host_command {
	/*
	 * Handler for the command. Args points to context for handler.
	 * Returns result status (EC_RES_*).
	 */
	ec_host_cmd_handler_cb handler;
	/* Command code */
	int command;
	/* Mask of supported versions */
	int version_mask;
};

/**
 * Find a command by command number.
 *
 * @param command Command number to find
 * @return The command structure, or NULL if no match found.
 */
const struct host_command *find_host_command(int command);

/**
 * Send a response to the relevant driver for transmission
 *
 * Once command processing is complete, this is used to send a response
 * back to the host.
 *
 * @param args Contains response to send
 */
void host_send_response(struct host_cmd_handler_args *args);

/**
 * Return the expected host packet size given its header.
 *
 * Also does some validity checking on the host request.
 *
 * @param r Host request header
 * @return The expected packet size, or 0 if error.
 */
int host_request_expected_size(const struct ec_host_request *r);

/**
 * Handle a received host packet.
 *
 * @param packet Host packet args
 */
void host_packet_receive(struct host_packet *pkt);

/**
 * Process a host command and return its response
 *
 * @param args	        Command handler args
 * @return resulting status. Note that while this returns an ec_status enum, we
 * are intentionally specifying the return type as a uint16_t, to prevent issues
 * related to compiler optimizations affecting the range of values returnable
 * from this function.
 */
uint16_t host_command_process(struct host_cmd_handler_args *args);

/**
 * Check if a Host Command that sent EC_HOST_CMD_IN_PROGRESS status has ended.
 *
 * @return True if the command has ended, False if not.
 */
bool host_command_in_process_ended(void);

/**
 * Get saved result of the command that has sent IN_PROGRESS status.
 *
 * This routine returns the save result and clears it.
 *
 * @return Save result.
 */
uint8_t host_command_get_saved_result(void);

struct host_command *zephyr_find_host_command(int command);

#define DECLARE_HOST_COMMAND(_command, _routine, _version_mask)         \
	static const STRUCT_SECTION_ITERABLE(host_command,              \
					     _cros_hcmd_##_command) = { \
		.handler = _routine,                                    \
		.command = _command,                                    \
		.version_mask = _version_mask,                          \
	}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_HOST_COMMAND_LEGACY_H_ */
