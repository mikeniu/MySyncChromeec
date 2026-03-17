/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#pragma once

#include <cstdint>

enum class EcCommandResult : uint8_t {
	SUCCESS = 0,
	INVALID_COMMAND = 1,
	ERROR = 2,
	INVALID_PARAM = 3,
	ACCESS_DENIED = 4,
	INVALID_RESPONSE = 5,
	INVALID_VERSION = 6,
	INVALID_CHECKSUM = 7,
	IN_PROGRESS = 8,
	UNAVAILABLE = 9,
	TIMEOUT = 10,
	OVERFLOW = 11,
	INVALID_HEADER = 12,
	REQUEST_TRUNCATED = 13,
	RESPONSE_TOO_BIG = 14,
	BUS_ERROR = 15,
	BUSY = 16,
	INVALID_HEADER_VERSION = 17,
	INVALID_HEADER_CRC = 18,
	INVALID_DATA_CRC = 19,
	DUP_UNAVAILABLE = 20,
	UNKNOWN = 255,
};

enum class ECCommandId : uint16_t {
	EC_CMD_GET_VERSION = 0x0002,
	EC_CMD_GET_VERSIONS = 0x0008,
	EC_CMD_PROTOCOL_INFO = 0x000B,
	EC_CMD_FLASH_INFO = 0x0010,
	EC_CMD_FLASH_READ = 0x0011,
	EC_CMD_FLASH_WRITE = 0x0012,
	EC_CMD_FLASH_ERASE = 0x0013,
	EC_CMD_FLASH_REGION_INFO = 0x0016,
	EC_CMD_REBOOT_EC = 0x00D2,
	EC_CMD_ENTER_BOOTLOADER = 0x00E2,
	EC_CMD_FP_MODE = 0x0402,
	EC_CMD_FP_INFO = 0x0403,
};

struct EcHostCommandRequest {
	uint8_t protocol_version; // Protocol version (typically 3)
	uint8_t checksum; // Packet checksum
	uint16_t command_id; // Command ID (little-endian)
	uint8_t command_version; // Command version
	uint8_t reserved; // Reserved / padding
	uint16_t data_length; // Payload length (little-endian)
} __attribute__((packed));

struct EcHostCommandResponse {
	uint8_t protocol_version; // Protocol version (typically 3)
	uint8_t checksum; // Packet checksum
	uint16_t result; // Command result (EcCommandResult)
	uint16_t data_length; // Payload length
	uint16_t reserved; // Reserved
} __attribute__((packed));

struct GetVersionResponse {
	char ro_ver[32];
	char rw_ver[32];
	char fwid_ro[32];
	char fwid_rw[32];
	uint32_t current_image;
} __attribute__((packed));
