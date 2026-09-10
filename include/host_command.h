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
#define DECLARE_HOST_COMMAND(id, handler, ver) \
	EC_HOST_CMD_HANDLER_UNBOUND(id, (ec_host_cmd_handler_cb)handler, ver)
#elif defined(CONFIG_PLATFORM_EC_HOSTCMD) || !defined(CONFIG_ZEPHYR)
#include "host_command_legacy.h"
#else
#include "host_command_stub.h"
#endif

#ifdef __cplusplus
template <bool B, typename T1, typename T2> struct _smallest_type_select {
	typedef T1 type;
};
template <typename T1, typename T2>
struct _smallest_type_select<false, T1, T2> {
	typedef T2 type;
};
template <typename T1, typename T2> struct _smallest_type_helper {
	typedef typename _smallest_type_select<(sizeof(T1) < sizeof(T2)), T1,
					       T2>::type type;
};

extern "C" {
#endif

/* TODO(b/559709343): Add per-version host command handler support upstream so
 * that handlers don't need SMALLEST_TYPE to handle multiple struct versions.
 */
#define _SMALLEST_TYPE_1(T) T
#ifdef __cplusplus
#define _SMALLEST_TYPE_2(T1, T2) typename _smallest_type_helper<T1, T2>::type
#else
#define _SMALLEST_TYPE_2(T1, T2)                                            \
	__typeof__(__builtin_choose_expr(sizeof(T1) < sizeof(T2), *(T1 *)0, \
					 *(T2 *)0))
#endif
#define _SMALLEST_TYPE_3(T1, ...) \
	_SMALLEST_TYPE_2(T1, _SMALLEST_TYPE_2(__VA_ARGS__))
#define _SMALLEST_TYPE_4(T1, ...) \
	_SMALLEST_TYPE_2(T1, _SMALLEST_TYPE_3(__VA_ARGS__))
#define _SMALLEST_TYPE_5(T1, ...) \
	_SMALLEST_TYPE_2(T1, _SMALLEST_TYPE_4(__VA_ARGS__))
#define _SMALLEST_TYPE_6(T1, ...) \
	_SMALLEST_TYPE_2(T1, _SMALLEST_TYPE_5(__VA_ARGS__))

#define _GET_SMALLEST_TYPE_MACRO(_1, _2, _3, _4, _5, _6, NAME, ...) NAME
#define SMALLEST_TYPE(...)                                           \
	_GET_SMALLEST_TYPE_MACRO(__VA_ARGS__, _SMALLEST_TYPE_6,      \
				 _SMALLEST_TYPE_5, _SMALLEST_TYPE_4, \
				 _SMALLEST_TYPE_3, _SMALLEST_TYPE_2, \
				 _SMALLEST_TYPE_1)(__VA_ARGS__)

/* Keep sync with zephyr ec_host_cmd_handler_args. Needed for any external code
 * that defines host command handlers. The union is formatted as
 *   union {
 *      <legacy_name>;
 *      <upstream_name>;
 *   }
 */
struct host_cmd_handler_args {
	/*
	 * The driver that receives the command sets up the send_response()
	 * handler. Once the command is processed this handler is called to
	 * send the response back to the host.
	 */
	union {
		void (*send_response)(struct host_cmd_handler_args *args);
		void *reserved;
	};
	uint16_t command; /* Command (e.g., EC_CMD_FLASH_GET_INFO) */
	uint8_t version; /* Version of command (0-31) */

	union {
		const void *params; /* Input parameters */
		const void *input_buf;
	};
	union {
		uint16_t params_size; /* Size of input parameters in bytes */
		uint16_t input_buf_size;
	};

	/*
	 * Pointer to output response data buffer. On input to the handler,
	 * points to a buffer of size response_max.
	 */
	union {
		void *response;
		void *output_buf;
	};

	/* Maximum size of response buffer provided to command handler */
	union {
		uint16_t response_max;
		uint16_t output_buf_max;
	};

	/*
	 * Size of data pointed to by response. Defaults to 0, so commands
	 * which do not produce response data do not need to set this.
	 */
	union {
		uint16_t response_size;
		uint16_t output_buf_size;
	};

#ifndef CONFIG_EC_HOST_CMD
	/*
	 * This is the result returned by command and therefore the status to
	 * be reported from the command execution to the host. The driver
	 * should set this to EC_RES_SUCCESS on receipt of a valid command.
	 * It is then passed back to the driver via send_response() when
	 * command execution is complete. The driver may still override this
	 * when sending the response back to the host if it detects an error
	 * in the response or in its own operation.
	 */
	uint16_t result;
#endif
};

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

/**
 * Called by host interface module when a command is received.
 */
void host_command_received(struct host_cmd_handler_args *args);

/**
 * Return the expected host packet size given its header.
 *
 * Also does some validity checking on the host request.
 *
 * @param r		Host request header
 * @return The expected packet size, or 0 if error.
 */
int host_request_expected_size(const struct ec_host_request *r);

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
#include "host_command_test_utils.h"
#endif /* CONFIG_ZTEST */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_HOST_COMMAND_H */
