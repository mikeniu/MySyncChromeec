/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "timer.h"
#include "util.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define HC_MAX_PACKET_SIZE 544
#define HC_CHUNK_RAW_BYTES 64
#define HC_CHUNK_TIMEOUT_MS 2000

static const char hex_digits[] = "0123456789abcdef";

static int parse_hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_max,
			size_t *out_len)
{
	size_t count = 0;

	while (*hex) {
		int high, low;

		if (!*(hex + 1))
			return EC_ERROR_INVAL;

		high = parse_hex_digit(*hex++);
		low = parse_hex_digit(*hex++);

		if (high < 0 || low < 0)
			return EC_ERROR_INVAL;

		if (count >= out_max)
			return EC_ERROR_OVERFLOW;

		out[count++] = (uint8_t)((high << 4) | low);
	}

	*out_len = count;
	return EC_SUCCESS;
}

/* Assembly buffer for incoming request chunks */
static uint8_t hc_req_buf[HC_MAX_PACKET_SIZE];
static size_t hc_req_len;
static uint32_t hc_req_curr_chunk;
static uint32_t hc_req_total_chunks;
static uint32_t hc_req_start_ms;

/* Output buffer for response */
static uint8_t hc_resp_buf[HC_MAX_PACKET_SIZE];

static void hc_emit_response(uint16_t result, const uint8_t *resp_data,
			     size_t resp_data_len)
{
	struct ec_host_response *resp_header = (void *)hc_resp_buf;
	size_t total_resp_len = sizeof(struct ec_host_response) + resp_data_len;
	uint8_t csum = 0;
	size_t total_chunks;

	if (total_resp_len > sizeof(hc_resp_buf)) {
		result = EC_RES_RESPONSE_TOO_BIG;
		resp_data_len = 0;
		total_resp_len = sizeof(struct ec_host_response);
	}

	resp_header->struct_version = 3;
	resp_header->checksum = 0;
	resp_header->result = result;
	resp_header->data_len = (uint16_t)resp_data_len;
	resp_header->reserved = 0;

	if (resp_data_len > 0 &&
	    resp_data != (hc_resp_buf + sizeof(struct ec_host_response))) {
		memcpy(hc_resp_buf + sizeof(struct ec_host_response), resp_data,
		       resp_data_len);
	}

	for (size_t i = 0; i < total_resp_len; i++) {
		csum += hc_resp_buf[i];
	}
	resp_header->checksum = (uint8_t)(-csum);

	total_chunks =
		(total_resp_len + HC_CHUNK_RAW_BYTES - 1) / HC_CHUNK_RAW_BYTES;
	if (total_chunks == 0)
		total_chunks = 1;

	for (size_t chunk = 1; chunk <= total_chunks; chunk++) {
		size_t offset = (chunk - 1) * HC_CHUNK_RAW_BYTES;
		size_t chunk_bytes =
			MIN(HC_CHUNK_RAW_BYTES, total_resp_len - offset);
		char hex_str[HC_CHUNK_RAW_BYTES * 2 + 1];

		for (size_t i = 0; i < chunk_bytes; i++) {
			uint8_t b = hc_resp_buf[offset + i];
			hex_str[i * 2] = hex_digits[(b >> 4) & 0x0f];
			hex_str[i * 2 + 1] = hex_digits[b & 0x0f];
		}
		hex_str[chunk_bytes * 2] = '\0';

		ccprintf("HC(%u/%u):%s\n", (unsigned int)chunk,
			 (unsigned int)total_chunks, hex_str);
	}
	cflush();
}

static void hc_process_request(void)
{
	struct ec_host_request *req_header;
	struct host_cmd_handler_args args;
	uint8_t csum = 0;
	uint16_t result;

	if (hc_req_len < sizeof(struct ec_host_request)) {
		hc_emit_response(EC_RES_INVALID_HEADER, NULL, 0);
		return;
	}

	req_header = (struct ec_host_request *)hc_req_buf;

	if (req_header->struct_version != 3) {
		hc_emit_response(EC_RES_INVALID_HEADER, NULL, 0);
		return;
	}

	if (sizeof(struct ec_host_request) + req_header->data_len !=
	    hc_req_len) {
		hc_emit_response(EC_RES_INVALID_HEADER, NULL, 0);
		return;
	}

	for (size_t i = 0; i < hc_req_len; i++) {
		csum += hc_req_buf[i];
	}
	if (csum != 0) {
		hc_emit_response(EC_RES_INVALID_CHECKSUM, NULL, 0);
		return;
	}

	args.version = req_header->command_version;
	args.command = req_header->command;
	args.params = hc_req_buf + sizeof(struct ec_host_request);
	args.params_size = req_header->data_len;
	args.response = hc_resp_buf + sizeof(struct ec_host_response);
	args.response_max =
		sizeof(hc_resp_buf) - sizeof(struct ec_host_response);
	args.response_size = 0;

	result = host_command_process(&args);

	hc_emit_response(result, hc_resp_buf + sizeof(struct ec_host_response),
			 args.response_size);
}

static int command_hc(int argc, const char **argv)
{
	uint32_t chunk_num = 1;
	uint32_t total_chunks = 1;
	const char *hex_payload;
	size_t decoded_len = 0;
	uint32_t now_ms = k_uptime_get_32();
	int rv;

	if (argc < 2) {
		ccputs("Usage: hc [<chunk>/<total>] <raw_hex>\n");
		return EC_ERROR_PARAM_COUNT;
	}

	if (argc == 2) {
		hex_payload = argv[1];
		chunk_num = 1;
		total_chunks = 1;
	} else {
		char *slash;
		char *endp;

		chunk_num = strtoi(argv[1], &slash, 10);
		if (!slash || *slash != '/') {
			ccputs("Usage: hc [<chunk>/<total>] <raw_hex>\n");
			return EC_ERROR_PARAM1;
		}
		total_chunks = strtoi(slash + 1, &endp, 10);
		if (!endp || *endp != '\0') {
			ccputs("Invalid total chunk count\n");
			return EC_ERROR_PARAM1;
		}
		hex_payload = argv[2];
	}

	if (chunk_num == 0 || total_chunks == 0 || chunk_num > total_chunks) {
		ccputs("Invalid chunk numbers\n");
		return EC_ERROR_INVAL;
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
			hc_req_len = 0;
			hc_req_curr_chunk = 0;
			hc_req_total_chunks = 0;
			ccputs("HC:ERR:CHUNK_OUT_OF_ORDER\n");
			return EC_ERROR_INVAL;
		}
		hc_req_curr_chunk = chunk_num;
		hc_req_start_ms = now_ms;
	}

	rv = hex_to_bytes(hex_payload, hc_req_buf + hc_req_len,
			  sizeof(hc_req_buf) - hc_req_len, &decoded_len);
	if (rv != EC_SUCCESS) {
		hc_req_len = 0;
		hc_req_curr_chunk = 0;
		hc_req_total_chunks = 0;
		ccputs("HC:ERR:INVALID_HEX\n");
		return rv;
	}

	hc_req_len += decoded_len;

	if (chunk_num < total_chunks) {
		return EC_SUCCESS;
	}

	hc_req_curr_chunk = 0;
	hc_req_total_chunks = 0;
	hc_process_request();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hc, command_hc, "[<chunk>/<total>] <raw_hex>",
			"Raw Host Command over UART");
