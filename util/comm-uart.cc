/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "comm-host.h"
#include "cros_ec_dev.h"
#include "ec_commands.h"
#include "misc_util.h"
#include "uart_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#define UART_CHUNK_SIZE 32
#define UART_MAX_BUF 256
#define UART_TIMEOUT_MS 3000

static int uart_fd = -1;

static int sum_bytes(const void *data, int length)
{
	const uint8_t *bytes = (const uint8_t *)data;
	int sum = 0;

	for (int i = 0; i < length; i++)
		sum += bytes[i];
	return sum;
}

static int hex_to_bin(const char *hex, uint8_t *bin, size_t max_bin)
{
	size_t len = strlen(hex);

	if ((len % 2) || (len / 2 > max_bin))
		return -1;
	for (size_t i = 0; i < len / 2; i++) {
		if (sscanf(hex + 2 * i, "%2hhx", &bin[i]) != 1)
			return -1;
	}
	return len / 2;
}

static void bin_to_hex(const uint8_t *bin, size_t len, char *hex)
{
	for (size_t i = 0; i < len; i++)
		sprintf(hex + 2 * i, "%02x", bin[i]);
}

static int uart_read_line(int fd, char *buf, size_t max_len, int timeout_ms)
{
	size_t pos = 0;

	while (pos < max_len - 1) {
		fd_set fds;
		struct timeval tv = { timeout_ms / 1000,
				      (timeout_ms % 1000) * 1000 };

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0)
			return -ETIMEDOUT;

		char ch;

		if (read(fd, &ch, 1) <= 0)
			return -EIO;
		if (ch == '\r')
			continue;
		if (ch == '\n') {
			if (pos == 0)
				continue;
			break;
		}
		buf[pos++] = ch;
	}
	buf[pos] = '\0';
	return pos;
}

static int ec_command_uart_send_recv(int command, int version,
				     const void *outdata, int outsize,
				     void *indata, int insize)
{
	uint8_t req[sizeof(struct ec_host_request) + UART_MAX_BUF];
	uint8_t resp[sizeof(struct ec_host_response) + UART_MAX_BUF];
	struct ec_host_request *rq = (struct ec_host_request *)req;
	size_t req_len = sizeof(*rq) + outsize;

	if (req_len > UART_MAX_BUF)
		return -EC_RES_REQUEST_TRUNCATED;

	rq->struct_version = EC_HOST_REQUEST_VERSION;
	rq->checksum = 0;
	rq->command = command;
	rq->command_version = version;
	rq->reserved = 0;
	rq->data_len = outsize;
	if (outsize > 0 && outdata)
		memcpy(req + sizeof(*rq), outdata, outsize);
	rq->checksum = (uint8_t)(-sum_bytes(req, req_len));

	uart_flush(uart_fd);

	/* Transmit chunks */
	size_t total_chunks = (req_len + UART_CHUNK_SIZE - 1) / UART_CHUNK_SIZE;

	if (!total_chunks)
		total_chunks = 1;

	for (size_t chunk = 1; chunk <= total_chunks; chunk++) {
		size_t off = (chunk - 1) * UART_CHUNK_SIZE;
		size_t n = MIN((size_t)UART_CHUNK_SIZE, req_len - off);
		char hex[UART_CHUNK_SIZE * 2 + 1];
		char cmd[UART_CHUNK_SIZE * 2 + 32];

		bin_to_hex(req + off, n, hex);
		if (total_chunks == 1)
			snprintf(cmd, sizeof(cmd), "hc %s\n", hex);
		else
			snprintf(cmd, sizeof(cmd), "hc %zx/%zx %s\n", chunk,
				 total_chunks, hex);

		if (write(uart_fd, cmd, strlen(cmd)) < 0)
			return -EC_RES_ERROR;
		if (chunk < total_chunks)
			usleep(5000);
	}

	/* Receive chunks */
	size_t rx_chunks = 0, total_rx_chunks = 0, resp_len = 0;
	char line[512];

	while (rx_chunks == 0 || rx_chunks < total_rx_chunks) {
		if (uart_read_line(uart_fd, line, sizeof(line),
				   UART_TIMEOUT_MS) < 0)
			return -EC_RES_TIMEOUT;

		char *p = strstr(line, "HC:");

		if (!p)
			continue;
		p += 3;

		unsigned int chunk = 1, total = 1;
		char *slash = strchr(p, '/'), *colon = strchr(p, ':');
		const char *hex = p;

		if (slash && colon && slash < colon) {
			chunk = strtoul(p, NULL, 16);
			total = strtoul(slash + 1, NULL, 16);
			hex = colon + 1;
		}

		if (rx_chunks == 0)
			total_rx_chunks = total;
		else if (total != total_rx_chunks)
			return -EC_RES_INVALID_RESPONSE;

		size_t off = (chunk - 1) * UART_CHUNK_SIZE;
		int dec = hex_to_bin(hex, resp + off, sizeof(resp) - off);

		if (dec < 0)
			return -EC_RES_INVALID_RESPONSE;

		resp_len += dec;
		rx_chunks++;
	}

	if (resp_len < sizeof(struct ec_host_response))
		return -EC_RES_INVALID_RESPONSE;

	struct ec_host_response *rs = (struct ec_host_response *)resp;

	if (rs->struct_version != EC_HOST_RESPONSE_VERSION ||
	    (uint8_t)sum_bytes(resp, sizeof(*rs) + rs->data_len) != 0)
		return -EC_RES_INVALID_CHECKSUM;

	if (rs->result != EC_RES_SUCCESS)
		return -EECRESULT - rs->result;

	if (rs->data_len > (size_t)insize)
		return -EC_RES_RESPONSE_TOO_BIG;

	if (rs->data_len > 0 && indata)
		memcpy(indata, resp + sizeof(*rs), rs->data_len);

	return rs->data_len;
}

static int ec_command_uart(int command, int version, const void *outdata,
			   int outsize, void *indata, int insize)
{
	int ret = ec_command_uart_send_recv(command, version, outdata, outsize,
					    indata, insize);

	if (ret == -EECRESULT - EC_RES_IN_PROGRESS) {
		struct ec_response_get_comms_status st;

		for (int tries = 0; tries < 50; tries++) {
			usleep(50000);
			if (ec_command_uart_send_recv(EC_CMD_GET_COMMS_STATUS,
						      0, NULL, 0, &st,
						      sizeof(st)) >= 0 &&
			    !(st.flags & EC_COMMS_STATUS_PROCESSING)) {
				return ec_command_uart_send_recv(
					command, version, outdata, outsize,
					indata, insize);
			}
		}
	}
	return ret;
}

int comm_init_uart(const char *device_path)
{
	const char *path = (device_path && *device_path &&
			    strcmp(device_path, CROS_EC_DEV_NAME) != 0) ?
				   device_path :
				   "/dev/ttyUSB0";
	const char *colon = strchr(path, ':');

	if (colon && !strchr(path, '/')) {
		char host[128] = { 0 };

		strncpy(host, path,
			MIN(sizeof(host) - 1, (size_t)(colon - path)));
		struct addrinfo hints = {}, *res = NULL;

		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(host, colon + 1, &hints, &res) != 0)
			return -1;
		uart_fd = socket(res->ai_family, res->ai_socktype,
				 res->ai_protocol);
		if (uart_fd >= 0 &&
		    connect(uart_fd, res->ai_addr, res->ai_addrlen) < 0) {
			close(uart_fd);
			uart_fd = -1;
		}
		freeaddrinfo(res);
	} else {
		uart_fd = open(path, O_RDWR | O_NOCTTY | O_SYNC);
		if (uart_fd >= 0) {
			struct termios tio;

			tcgetattr(uart_fd, &tio);
			cfmakeraw(&tio);
			cfsetspeed(&tio, B115200);
			tio.c_cc[VMIN] = 0;
			tio.c_cc[VTIME] = 5;
			tcsetattr(uart_fd, TCSANOW, &tio);
		}
	}

	if (uart_fd < 0) {
		fprintf(stderr, "Cannot open UART device '%s': %s\n", path,
			strerror(errno));
		return -1;
	}

	ec_command_proto = ec_command_uart;
	ec_max_outsize = UART_MAX_BUF - sizeof(struct ec_host_request);
	ec_max_insize = UART_MAX_BUF - sizeof(struct ec_host_response);
	return 0;
}
