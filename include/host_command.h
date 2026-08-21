/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#ifndef __CROS_EC_HOST_COMMAND_H
#define __CROS_EC_HOST_COMMAND_H

#include "common.h"
#include "compiler.h"
#include "ec_commands.h"

#include <stdbool.h>

#ifdef CONFIG_EC_HOST_CMD
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#ifdef CONFIG_ZTEST
#include "host_command_legacy.h"
#endif
#else
#include "host_command_legacy.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t host_event_t;
#define HOST_EVENT_CPRINTS(str, e) CPRINTS("%s 0x%016" PRIx64, str, e)
#define HOST_EVENT_CCPRINTF(str, e) ccprintf("%s 0x%016" PRIx64 "\n", str, e)

/**
 * Initialize Host Command
 *
 * Initialize memmap memory and set needed host event. This function does not
 * initialize Host Command communication itself.
 */
void host_command_init(void);

/**
 * Return a pointer to the memory-mapped buffer.
 *
 * This buffer is EC_MEMMAP_SIZE bytes long, is writable at any time, and the
 * host can read it at any time.
 *
 * @param offset        Offset within the range to return
 * @return pointer to the buffer at that offset
 */
uint8_t *host_get_memmap(int offset);


/**
 * Set a single host event.
 *
 * @param event         Event to set (EC_HOST_EVENT_*).
 */
void host_set_single_event(enum host_event_code event);

/**
 * Clear one or more host event bits.
 *
 * @param mask          Event bits to clear (use EC_HOST_EVENT_MASK()).
 *                      Write 1 to a bit to clear it.
 */
void host_clear_events(host_event_t mask);

/**
 * Clear one or more host event bits from copy B.
 *
 * @param mask          Event bits to clear (use EC_HOST_EVENT_MASK()).
 *                      Write 1 to a bit to clear it.
 */
void host_clear_events_b(host_event_t mask);

/**
 * Return the raw event state.
 */
host_event_t host_get_events(void);

/**
 * Check a single host event.
 *
 * @param event		Event to check
 * @return true if <event> is set or false otherwise
 */
bool host_is_event_set(enum host_event_code event);

#ifdef CONFIG_HOSTCMD_X86

FORWARD_DECLARE_ENUM(power_state);

/*
 * Get lazy wake masks for the sleep state provided
 *
 * @param state Sleep state
 * @param mask  Lazy wake mask.
 *
 * @return EC_SUCCESS for success and EC_ERROR_INVAL for error
 */

int get_lazy_wake_mask(enum power_state state, host_event_t *mask);

/*
 * Check if active wake mask set by host
 *
 *
 * @return 1 if active wake mask set by host else return 0
 */
uint8_t lpc_is_active_wm_set_by_host(void);
#endif


#if defined(CONFIG_ZEPHYR)
#include "zephyr_host_command.h"
#endif

/**
 * Politely ask the CPU to enable/disable its own throttling.
 *
 * @param throttle	Enable (!=0) or disable(0) throttling
 */
void host_throttle_cpu(int throttle);

/**
 * Signal host command task to send status to PD MCU.
 *
 * @new_chg_state PD MCU charge state
 */
void host_command_pd_send_status(enum pd_charge_state new_chg_state);

/**
 * Signal host command task to inform PD MCU that the EC is going to hibernate,
 * which will normally cause the PD MCU to hibernate also.
 */
void host_command_pd_request_hibernate(void);

/**
 * Get the active charge port from the PD
 *
 * @return -1 == none/unknown, 0 == left, 1 == right.
 */
int pd_get_active_charge_port(void);

/**
 * Send host command to PD MCU.
 *
 * @param command Host command number
 * @param version Version of host command
 * @param outdata Pointer to buffer of out data
 * @param outsize Size of buffer to out data
 * @param indata Pointer to buffer to store response
 * @param insize Size of buffer to store response
 */
int pd_host_command(int command, int version, const void *outdata, int outsize,
		    void *indata, int insize);

/*
 * Sends an emulated sysrq to the host, used by button-based debug mode.
 * Only implemented on top of MKBP protocol.
 *
 * @param key		Key to be sent (e.g. 'x')
 */
void host_send_sysrq(uint8_t key);

/* Return the lower/higher part of the feature flags bitmap */
uint32_t get_feature_flags0(void);
uint32_t get_feature_flags1(void);

#ifdef CONFIG_ZTEST
static inline void
stub_send_response_callback(struct host_cmd_handler_args *args)
{
	ARG_UNUSED(args);
}

#define BUILD_HOST_COMMAND(CMD, VERSION, RESPONSE, PARAMS)          \
	{                                                           \
		.send_response = stub_send_response_callback,       \
		.command = (CMD),                                   \
		.version = (VERSION),                               \
		COND_CODE_0(IS_EMPTY(PARAMS),                       \
			    (.params = &(PARAMS),                   \
			     .params_size = sizeof(PARAMS)),        \
			    (.params = NULL, .params_size = 0)),    \
		COND_CODE_0(IS_EMPTY(RESPONSE),                     \
			    (.response = &(RESPONSE),               \
			     .response_max = sizeof(RESPONSE)),     \
			    (.response = NULL, .response_max = 0)), \
		.response_size = 0,                                 \
	}

#define BUILD_HOST_COMMAND_RESPONSE(CMD, VERSION, RESPONSE) \
	BUILD_HOST_COMMAND(CMD, VERSION, RESPONSE, EMPTY)

#define BUILD_HOST_COMMAND_PARAMS(CMD, VERSION, PARAMS) \
	BUILD_HOST_COMMAND(CMD, VERSION, EMPTY, PARAMS)

#define BUILD_HOST_COMMAND_SIMPLE(CMD, VERSION) \
	BUILD_HOST_COMMAND(CMD, VERSION, EMPTY, EMPTY)

#define CROS_EC_COMMAND_INFO struct host_cmd_handler_args

static inline int CROS_EC_COMMAND(CROS_EC_COMMAND_INFO *handle,
				  uint16_t command, uint8_t version,
				  const void *params, uint16_t params_size,
				  void *response, uint16_t response_size)
{
	struct host_cmd_handler_args args;
	int rv;

	if (handle == NULL)
		handle = &args;

	handle->send_response = stub_send_response_callback;
	handle->command = command;
	handle->version = version;
	handle->params = params;
	handle->params_size = params_size;
	handle->response = response;
	handle->response_max = response_size;
	handle->response_size = 0;
#ifndef CONFIG_EC_HOST_CMD
	handle->result = 0;
#endif

	rv = host_command_process(handle);
#ifndef CONFIG_EC_HOST_CMD
	if (handle->result != EC_RES_SUCCESS)
		return handle->result;
#endif

	return rv;
}

#include "ec_cmd_api.h"

#endif /* CONFIG_ZTEST */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_HOST_COMMAND_H */
