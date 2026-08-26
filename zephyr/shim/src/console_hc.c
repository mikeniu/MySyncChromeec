/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_EC_HOST_CMD)
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#elif defined(CONFIG_PLATFORM_EC_HOSTCMD)
#include "host_command.h"
#endif

#define HC_MAX_PACKET_SIZE 256
#define HC_CHUNK_RAW_BYTES 32
#define HC_CHUNK_HEX_MAX (HC_CHUNK_RAW_BYTES * 2)
#define HC_MIN_SHELL_CMD_BUFF_SIZE (HC_CHUNK_HEX_MAX + 16)
#define HC_CHUNK_TIMEOUT_MS 2000
#define HC_HOSTCMD_TIMEOUT_MS 3000

BUILD_ASSERT(CONFIG_SHELL_CMD_BUFF_SIZE >= HC_MIN_SHELL_CMD_BUFF_SIZE,
	     "CONFIG_SHELL_CMD_BUFF_SIZE too small for hc chunk size");

#if !defined(CONFIG_EC_HOST_CMD) && defined(CONFIG_PLATFORM_EC_HOSTCMD)
/* Synchronization semaphore for HOSTCMD task completion */
static K_SEM_DEFINE(hc_done_sem, 0, 1);
static struct host_packet hc_pkt;

static void hc_packet_send_response(struct host_packet *pkt)
{
	k_sem_give(&hc_done_sem);
}
#endif

/* Assembly buffer for incoming request chunks */
static uint8_t hc_req_buf[HC_MAX_PACKET_SIZE];
static size_t hc_req_len;
static uint32_t hc_req_curr_chunk;
static uint32_t hc_req_total_chunks;
static uint32_t hc_req_start_ms;

/* Output buffer for response packet */
static uint8_t hc_resp_buf[HC_MAX_PACKET_SIZE];

static void hc_emit_response(const struct shell *sh, const uint8_t *resp_buf,
			     size_t total_resp_len)
{
	size_t total_chunks =
		(total_resp_len + HC_CHUNK_RAW_BYTES - 1) / HC_CHUNK_RAW_BYTES;

	if (total_chunks == 0)
		total_chunks = 1;

	for (size_t chunk = 1; chunk <= total_chunks; chunk++) {
		size_t offset = (chunk - 1) * HC_CHUNK_RAW_BYTES;
		size_t chunk_bytes =
			MIN(HC_CHUNK_RAW_BYTES, total_resp_len - offset);
		char hex_str[HC_CHUNK_HEX_MAX + 1];

		bin2hex(resp_buf + offset, chunk_bytes, hex_str,
			sizeof(hex_str));

		if (sh) {
			if (total_chunks == 1) {
				shell_print(sh, "HC:%s", hex_str);
			} else {
				shell_print(sh, "HC:%x/%x:%s",
					    (unsigned int)chunk,
					    (unsigned int)total_chunks,
					    hex_str);
			}
		}
	}
}

#if defined(CONFIG_EC_HOST_CMD)
static void hc_emit_status_response(const struct shell *sh,
				    enum ec_status result)
{
	struct ec_host_response *r = (struct ec_host_response *)hc_resp_buf;
	uint8_t sum = 0;

	r->struct_version = 3;
	r->checksum = 0;
	r->result = result;
	r->data_len = 0;
	r->reserved = 0;

	for (size_t i = 0; i < sizeof(*r); i++) {
		sum += hc_resp_buf[i];
	}
	r->checksum = (uint8_t)(-sum);

	hc_emit_response(sh, hc_resp_buf, sizeof(*r));
}
#endif

#if defined(CONFIG_PLATFORM_EC_HOSTCMD) && defined(CONFIG_ZTEST) && \
	!defined(CONFIG_PLATFORM_EC_HOSTCMD_DEDICATED)
static void hc_cmd_send_response(struct host_cmd_handler_args *args)
{
	struct ec_host_response *r = (struct ec_host_response *)hc_resp_buf;
	uint8_t sum = 0;

	r->struct_version = 3;
	r->checksum = 0;
	r->result = args->result;
	r->data_len = args->response_size;
	r->reserved = 0;

	for (size_t i = 0; i < sizeof(*r) + r->data_len; i++) {
		sum += hc_resp_buf[i];
	}
	r->checksum = (uint8_t)(-sum);
	hc_pkt.response_size = sizeof(*r) + r->data_len;
}
#endif

static void hc_process_request(const struct shell *sh,
			       enum ec_status driver_result)
{
#if defined(CONFIG_EC_HOST_CMD)
	if (driver_result != EC_RES_SUCCESS) {
		hc_emit_status_response(sh, driver_result);
		return;
	}

	size_t resp_len = 0;
	ec_host_cmd_process_packet(hc_req_buf, hc_req_len, hc_resp_buf,
				   sizeof(hc_resp_buf), &resp_len);
	if (resp_len > 0) {
		hc_emit_response(sh, hc_resp_buf, resp_len);
	}
#elif defined(CONFIG_PLATFORM_EC_HOSTCMD)
	k_sem_reset(&hc_done_sem);

	hc_pkt.send_response = hc_packet_send_response;
	hc_pkt.request = hc_req_buf;
	hc_pkt.request_size = hc_req_len;
	hc_pkt.request_max = sizeof(hc_req_buf);
	hc_pkt.request_temp = NULL;
	hc_pkt.response = hc_resp_buf;
	hc_pkt.response_max = sizeof(hc_resp_buf);
	hc_pkt.response_size = 0;
	hc_pkt.driver_result = driver_result;

	/*
	 * Dispatch the packet through the standard CrOS EC host packet
	 * receive pipeline. When driver_result is non-zero,
	 * host_packet_receive() automatically formats the standard V3 error
	 * response and calculates the checksum.
	 */
	host_packet_receive(&hc_pkt);

#if defined(CONFIG_ZTEST) && !defined(CONFIG_PLATFORM_EC_HOSTCMD_DEDICATED)
	if (!driver_result && hc_pkt.response_size == 0) {
		struct host_cmd_handler_args args;
		const struct ec_host_request *r = (const void *)hc_req_buf;
		struct ec_host_response *resp = (void *)hc_resp_buf;

		args.command = r->command;
		args.version = r->command_version;
		args.params = hc_req_buf + sizeof(*r);
		args.params_size = r->data_len;
		args.response = hc_resp_buf + sizeof(*resp);
		args.response_max = sizeof(hc_resp_buf) - sizeof(*resp);
		args.response_size = 0;
		args.send_response = hc_cmd_send_response;

		args.result = host_command_process(&args);
		args.send_response(&args);
	}
#else
	if (k_sem_take(&hc_done_sem, K_MSEC(HC_HOSTCMD_TIMEOUT_MS)) != 0) {
		return;
	}
#endif

	hc_emit_response(sh, hc_resp_buf, hc_pkt.response_size);
#endif
}

static int cmd_hc(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t chunk_num = 1;
	uint32_t total_chunks = 1;
	const char *hex_payload;
	size_t hex_len;
	size_t decoded_len;
	uint32_t now_ms = k_uptime_get_32();
	enum ec_status err_res = EC_RES_INVALID_PARAM;

	if (argc == 2) {
		hex_payload = argv[1];
	} else if (argc == 3) {
		char *slash;
		char *endp;

		chunk_num = strtoul(argv[1], &slash, 16);
		if (!slash || *slash != '/')
			goto err_exit;

		total_chunks = strtoul(slash + 1, &endp, 16);
		if (!endp || *endp != '\0')
			goto err_exit;

		hex_payload = argv[2];
	} else {
		goto err_exit;
	}

	if (chunk_num == 0 || total_chunks == 0 || chunk_num > total_chunks ||
	    total_chunks > 0xf)
		goto err_exit;

	if (chunk_num == 1) {
		hc_req_len = 0;
		hc_req_curr_chunk = 1;
		hc_req_total_chunks = total_chunks;
		hc_req_start_ms = now_ms;
	} else {
		if (hc_req_curr_chunk == 0 ||
		    total_chunks != hc_req_total_chunks ||
		    chunk_num != hc_req_curr_chunk + 1 ||
		    (now_ms - hc_req_start_ms) > HC_CHUNK_TIMEOUT_MS) {
			err_res = EC_RES_REQUEST_TRUNCATED;
			goto err_exit;
		}
		hc_req_curr_chunk = chunk_num;
		hc_req_start_ms = now_ms;
	}

	hex_len = strlen(hex_payload);
	if ((hex_len % 2) != 0 || hex_len == 0)
		goto err_exit;

	if (hc_req_len + (hex_len / 2) > sizeof(hc_req_buf)) {
		err_res = EC_RES_OVERFLOW;
		goto err_exit;
	}

	decoded_len = hex2bin(hex_payload, hex_len, hc_req_buf + hc_req_len,
			      sizeof(hc_req_buf) - hc_req_len);
	if (decoded_len == 0)
		goto err_exit;

	hc_req_len += decoded_len;

	if (chunk_num < total_chunks)
		return 0;

	hc_req_curr_chunk = 0;
	hc_req_total_chunks = 0;
	hc_process_request(sh, EC_RES_SUCCESS);
	return 0;

err_exit:
	hc_req_len = 0;
	hc_req_curr_chunk = 0;
	hc_req_total_chunks = 0;
	hc_process_request(sh, err_res);
	return -EINVAL;
}

SHELL_CMD_ARG_REGISTER(hc, NULL, "Host command bridge", cmd_hc, 1, 2);
