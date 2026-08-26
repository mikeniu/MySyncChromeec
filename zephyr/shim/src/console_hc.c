/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "host_command.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(console_hc, LOG_LEVEL_ERR);

#define HC_MAX_PACKET_SIZE 544
#define HC_CHUNK_RAW_BYTES 64
#define HC_CHUNK_TIMEOUT_MS 2000
#define HC_HOSTCMD_TIMEOUT_MS 3000

/* Synchronization semaphore for HOSTCMD task completion */
static K_SEM_DEFINE(hc_done_sem, 0, 1);
static struct host_packet hc_pkt;

/* Assembly buffer for incoming request chunks */
static uint8_t hc_req_buf[HC_MAX_PACKET_SIZE];
static size_t hc_req_len;
static uint32_t hc_req_curr_chunk;
static uint32_t hc_req_total_chunks;
static uint32_t hc_req_start_ms;

/* Output buffer for response packet */
static uint8_t hc_resp_buf[HC_MAX_PACKET_SIZE];

static void hc_packet_send_response(struct host_packet *pkt)
{
	k_sem_give(&hc_done_sem);
}

static void hc_emit_response(const struct shell *sh, const uint8_t *resp_buf,
			     size_t total_resp_len)
{
	size_t total_chunks;

	if (total_resp_len == 0) {
		LOG_ERR("Empty response packet");
		return;
	}

	total_chunks =
		(total_resp_len + HC_CHUNK_RAW_BYTES - 1) / HC_CHUNK_RAW_BYTES;
	if (total_chunks == 0)
		total_chunks = 1;

	for (size_t chunk = 1; chunk <= total_chunks; chunk++) {
		size_t offset = (chunk - 1) * HC_CHUNK_RAW_BYTES;
		size_t chunk_bytes =
			MIN(HC_CHUNK_RAW_BYTES, total_resp_len - offset);
		char hex_str[HC_CHUNK_RAW_BYTES * 2 + 1];
		size_t hex_len;

		hex_len = bin2hex(resp_buf + offset, chunk_bytes, hex_str,
				  sizeof(hex_str));
		if (hex_len == 0 && chunk_bytes > 0) {
			LOG_ERR("bin2hex conversion failed for chunk %zu",
				chunk);
			return;
		}

		if (sh) {
			shell_print(sh, "HC(%u/%u):%s", (unsigned int)chunk,
				    (unsigned int)total_chunks, hex_str);
		}
	}
}

static void hc_process_request(const struct shell *sh)
{
	k_sem_reset(&hc_done_sem);

	hc_pkt.send_response = hc_packet_send_response;
	hc_pkt.request = hc_req_buf;
	hc_pkt.request_size = hc_req_len;
	hc_pkt.request_max = sizeof(hc_req_buf);
	hc_pkt.request_temp = NULL;
	hc_pkt.response = hc_resp_buf;
	hc_pkt.response_max = sizeof(hc_resp_buf);
	hc_pkt.response_size = 0;
	hc_pkt.driver_result = 0;

	/*
	 * Dispatch the packet through the standard CrOS EC host packet
	 * receive pipeline. This executes on the dedicated HOSTCMD task.
	 */
	host_packet_receive(&hc_pkt);

	if (k_sem_take(&hc_done_sem, K_MSEC(HC_HOSTCMD_TIMEOUT_MS)) != 0) {
		LOG_ERR("Timed out waiting for HOSTCMD task execution");
		shell_error(sh, "HC:ERR:TIMEOUT");
		return;
	}

	hc_emit_response(sh, hc_resp_buf, hc_pkt.response_size);
}

static int cmd_hc(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t chunk_num = 1;
	uint32_t total_chunks = 1;
	const char *hex_payload;
	size_t hex_len;
	size_t decoded_len;
	uint32_t now_ms = k_uptime_get_32();

	if (argc == 2) {
		hex_payload = argv[1];
		chunk_num = 1;
		total_chunks = 1;
	} else if (argc == 3) {
		char *slash;
		char *endp;

		chunk_num = strtoul(argv[1], &slash, 10);
		if (!slash || *slash != '/') {
			shell_error(sh,
				    "Usage: hc [<chunk>/<total>] <raw_hex>");
			return -EINVAL;
		}
		total_chunks = strtoul(slash + 1, &endp, 10);
		if (!endp || *endp != '\0') {
			shell_error(sh, "Invalid total chunk count");
			return -EINVAL;
		}
		hex_payload = argv[2];
	} else {
		shell_error(sh, "Usage: hc [<chunk>/<total>] <raw_hex>");
		return -EINVAL;
	}

	if (chunk_num == 0 || total_chunks == 0 || chunk_num > total_chunks) {
		LOG_ERR("Invalid chunk numbers: %u/%u", chunk_num,
			total_chunks);
		shell_error(sh, "Invalid chunk numbers");
		return -EINVAL;
	}

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
			LOG_WRN("HC: chunk out of order (%u/%u, expected %u)",
				chunk_num, total_chunks, hc_req_curr_chunk + 1);
			hc_req_len = 0;
			hc_req_curr_chunk = 0;
			hc_req_total_chunks = 0;
			shell_error(sh, "HC:ERR:CHUNK_OUT_OF_ORDER");
			return -EINVAL;
		}
		hc_req_curr_chunk = chunk_num;
		hc_req_start_ms = now_ms;
	}

	hex_len = strlen(hex_payload);
	if ((hex_len % 2) != 0 || hex_len == 0) {
		LOG_ERR("Invalid hex length: %zu", hex_len);
		hc_req_len = 0;
		hc_req_curr_chunk = 0;
		hc_req_total_chunks = 0;
		shell_error(sh, "HC:ERR:INVALID_HEX");
		return -EINVAL;
	}

	if (hc_req_len + (hex_len / 2) > sizeof(hc_req_buf)) {
		LOG_ERR("HC request buffer overflow: %zu + %zu > %zu",
			hc_req_len, hex_len / 2, sizeof(hc_req_buf));
		hc_req_len = 0;
		hc_req_curr_chunk = 0;
		hc_req_total_chunks = 0;
		shell_error(sh, "HC:ERR:OVERFLOW");
		return -ENOSPC;
	}

	decoded_len = hex2bin(hex_payload, hex_len, hc_req_buf + hc_req_len,
			      sizeof(hc_req_buf) - hc_req_len);
	if (decoded_len == 0 && hex_len > 0) {
		LOG_ERR("hex2bin failed to decode hex payload");
		hc_req_len = 0;
		hc_req_curr_chunk = 0;
		hc_req_total_chunks = 0;
		shell_error(sh, "HC:ERR:INVALID_HEX");
		return -EINVAL;
	}

	hc_req_len += decoded_len;

	if (chunk_num < total_chunks) {
		return 0;
	}

	hc_req_curr_chunk = 0;
	hc_req_total_chunks = 0;
	hc_process_request(sh);

	return 0;
}

SHELL_CMD_ARG_REGISTER(hc, NULL,
		       "Execute raw host command over UART\n"
		       "Usage: hc [<chunk>/<total>] <raw_hex>",
		       cmd_hc, 2, 1);
