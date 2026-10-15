/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "event_log.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "usb_pd.h"
#include "util.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/**
 * @brief This is the maximum size of a single log entry.
 *
 * Each entry must contain some common data + up to 16 bytes of additional type
 * specific data.
 */
#define MAX_EVENT_LOG_ENTRY_SIZE (sizeof(struct event_log_entry) + 16)

/**
 * @brief This is the maximum size of a response pd log entry.
 *
 * Each entry must contain some common data + up to 16 bytes of additional type
 * specific data.
 */
#define MAX_RESPONSE_PD_LOG_ENTRY_SIZE (sizeof(struct ec_response_pd_log) + 16)

/**
 * @brief The size of the PD log entry data
 *
 * Logs from the PD include an additional 8 bytes of data to be sent to the AP.
 */
#define PD_LOG_ENTRY_DATA_SIZE (8)

struct pd_log_fixture {
	union {
		uint8_t event_log_buffer[MAX_EVENT_LOG_ENTRY_SIZE];
		struct event_log_entry log_entry;
	};
};

static void *pd_log_setup(void)
{
	static struct pd_log_fixture fixture;

	return &fixture;
}

static void pd_log_before(void *f)
{
	struct pd_log_fixture *fixture = f;

	while (log_dequeue_event(&fixture->log_entry) != 0) {
		if (fixture->log_entry.type == EVENT_LOG_NO_ENTRY) {
			break;
		}
	}
}

ZTEST_SUITE(pd_log, drivers_predicate_post_main, pd_log_setup, pd_log_before,
	    NULL, NULL);

ZTEST_USER(pd_log, test_bad_type)
{
	struct ec_params_pd_write_log_entry params = {
		.type = PD_EVENT_ACC_BASE,
	};

	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_pd_write_log_entry(NULL, &params), NULL);
}

ZTEST_USER(pd_log, test_bad_port)
{
	struct ec_params_pd_write_log_entry params = {
		.type = PD_EVENT_MCU_BASE,
	};

	params.port = board_get_usb_pd_port_count() + 1;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_pd_write_log_entry(NULL, &params), NULL);
}

ZTEST_USER_F(pd_log, test_mcu_charge)
{
	struct ec_params_pd_write_log_entry params = {
		.type = PD_EVENT_MCU_CHARGE,
		.port = 0,
	};

	zassert_ok(ec_cmd_pd_write_log_entry(NULL, &params), NULL);
	zassert_equal(sizeof(struct event_log_entry) + PD_LOG_ENTRY_DATA_SIZE,
		      log_dequeue_event(&fixture->log_entry), NULL);
	zassert_equal(params.type, fixture->log_entry.type, NULL);
	zassert_equal(PD_LOG_ENTRY_DATA_SIZE, fixture->log_entry.size, NULL);
	zassert_equal(0, fixture->log_entry.data, NULL);
	zassert_within(0, (int64_t)fixture->log_entry.timestamp, 10,
		       "Expected timestamp %" PRIi64
		       " to be within 10 ms of now",
		       (int64_t)fixture->log_entry.timestamp);
}
ZTEST_USER_F(pd_log, test_mcu_connect)
{
	struct ec_params_pd_write_log_entry params = {
		.type = PD_EVENT_MCU_CONNECT,
		.port = 0,
	};

	zassert_ok(ec_cmd_pd_write_log_entry(NULL, &params), NULL);
	zassert_equal(sizeof(struct event_log_entry),
		      log_dequeue_event(&fixture->log_entry), NULL);
	zassert_equal(params.type, fixture->log_entry.type, NULL);
	zassert_equal(0, fixture->log_entry.size, NULL);
	zassert_equal(0, fixture->log_entry.data, NULL);
	zassert_within(0, (int64_t)fixture->log_entry.timestamp, 10,
		       "Expected timestamp %" PRIi64
		       " to be within 10 ms of now",
		       (int64_t)fixture->log_entry.timestamp);
}

ZTEST_USER_F(pd_log, test_read_log_entry)
{
	uint8_t response_buffer[MAX_RESPONSE_PD_LOG_ENTRY_SIZE];
	struct ec_response_pd_log *response =
		(struct ec_response_pd_log *)response_buffer;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_PD_GET_LOG_ENTRY, UINT8_C(0));

	args.response = response;
	args.response_max = sizeof(response_buffer);

	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(sizeof(struct event_log_entry), args.response_size, NULL);
	zassert_equal(PD_EVENT_NO_ENTRY, response->type, NULL);
}

ZTEST_USER_F(pd_log, test_log_recv_vdm)
{
	uint8_t response_buffer[MAX_RESPONSE_PD_LOG_ENTRY_SIZE];
	uint32_t *payload = (uint32_t *)response_buffer;
	struct ec_response_pd_log *response =
		(struct ec_response_pd_log *)&payload[1];

	memset(response_buffer, 0, sizeof(response_buffer));
	payload[0] |= VDO_SRC_RESPONDER;
	response->type = PD_EVENT_MCU_CONNECT;
	response->size_port = 8;
	int cnt = DIV_ROUND_UP(response->size_port, sizeof(uint32_t)) + 2;

	pd_log_recv_vdm(0, cnt, payload);

	(void)log_dequeue_event(&fixture->log_entry);

	zassert_equal(response->type, fixture->log_entry.type,
		      "type=%d, received=%d", response->type,
		      fixture->log_entry.type);
}

ZTEST_USER_F(pd_log, test_fifo_wrap_and_overflow)
{
	uint8_t payload[PD_LOG_ENTRY_DATA_SIZE] = {
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	};
	const int unit_count =
		CONFIG_EVENT_LOG_SIZE / sizeof(struct event_log_entry);

	/*
	 * Fill 63 of the 64 units with 1-unit (0-byte payload) entries so the
	 * next entry starts at the last slot (index 63) and wraps around to
	 * index 0.
	 */
	for (int i = 0; i < unit_count - 1; i++) {
		log_add_event(PD_EVENT_MCU_CONNECT, 0, i, NULL, 0);
	}

	/*
	 * Enqueue a 2-unit entry (header at index 63, 8-byte payload wrapped to
	 * index 0). This overflows the 64-unit FIFO by 1 unit, discarding
	 * entry 0 (at index 0) and advancing log_head to entry 1 (at index 1).
	 */
	log_add_event(PD_EVENT_MCU_CHARGE, sizeof(payload), 0xbeef, payload, 0);

	/*
	 * Verify entry 1 (at index 1) was not corrupted by the wrap-around
	 * payload copy into index 0.
	 */
	for (int i = 1; i < unit_count - 1; i++) {
		zassert_equal(sizeof(struct event_log_entry),
			      log_dequeue_event(&fixture->log_entry));
		zassert_equal(PD_EVENT_MCU_CONNECT, fixture->log_entry.type);
		zassert_equal(0, fixture->log_entry.size);
		zassert_equal(i, fixture->log_entry.data);
	}

	/* Verify the wrapped 2-unit entry at the tail */
	zassert_equal(sizeof(struct event_log_entry) + sizeof(payload),
		      log_dequeue_event(&fixture->log_entry));
	zassert_equal(PD_EVENT_MCU_CHARGE, fixture->log_entry.type);
	zassert_equal(sizeof(payload), fixture->log_entry.size);
	zassert_equal(0xbeef, fixture->log_entry.data);
	zassert_mem_equal(fixture->log_entry.payload, payload, sizeof(payload));

	/* FIFO should now be empty */
	zassert_equal(sizeof(struct event_log_entry),
		      log_dequeue_event(&fixture->log_entry));
	zassert_equal(EVENT_LOG_NO_ENTRY, fixture->log_entry.type);
}
