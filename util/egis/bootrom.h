/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef BOOTROM_H_
#define BOOTROM_H_

#include "usb_comm.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

typedef enum {
	CRYPTO_SHA_256 = 0x00,
	CRYPTO_HMAC_SHA256 = 0x01,
	CRYPTO_AES_GCM = 0x02,
} EGIS_CRYPTO_TYPE;

namespace EgisBootrom
{

// Bootrom commands
const uint8_t CMD_GET_BOOT_INFO = 0x10;
const uint8_t CMD_FLASH_INFO = 0x11;
const uint8_t CMD_SYSTEM_RESET = 0x12;

// Memory / Flash commands
const uint8_t CMD_MEM_READ = 0x20;
const uint8_t CMD_MEM_WRITE = 0x21;
const uint8_t CMD_FLASH_ERASE = 0x22;

// OTP commands
const uint8_t CMD_OTP_READ = 0x23;
const uint8_t CMD_OTP_WRITE = 0x24;

// Secure Command
const uint8_t CMD_SECURE_WRAPPED = 0xF4;

// SPI registers
const uint32_t SPI_REG_BASE = 0xf0b00000;
const uint32_t SPI_REG_ID = SPI_REG_BASE + 0x0;
const uint32_t SPI_REG_TFMAT = SPI_REG_BASE + 0x10;
const uint32_t SPI_REG_WRCNT = SPI_REG_BASE + 0x18;
const uint32_t SPI_REG_RDCNT = SPI_REG_BASE + 0x1C;
const uint32_t SPI_REG_TCTRL = SPI_REG_BASE + 0x20;
const uint32_t SPI_REG_CMD = SPI_REG_BASE + 0x24;
const uint32_t SPI_REG_ADDR = SPI_REG_BASE + 0x28;
const uint32_t SPI_REG_DATA = SPI_REG_BASE + 0x2C;

// TCTRL bit definitions
const uint32_t TCTRL_TRNS_MODE_OFFSET = 24;
const uint32_t TCTRL_ADDR_FMT_MSK = (1 << 28);
const uint32_t TCTRL_ADDR_EN_MSK = (1 << 29);
const uint32_t TCTRL_CMD_EN_MSK = (1 << 30);

const uint32_t TRNS_MODE_WRITE_ONLY = (1 << TCTRL_TRNS_MODE_OFFSET);
const uint32_t TRNS_MODE_READ_ONLY = (2 << TCTRL_TRNS_MODE_OFFSET);
const uint32_t TRNS_MODE_NONE_DATA = (7 << TCTRL_TRNS_MODE_OFFSET);

const uint32_t TFMAT = 0x00020700;

// Flash commands
const uint8_t FLASH_WREN = 0x06;
const uint8_t FLASH_WRSR = 0x01;
const uint8_t FLASH_RDSR = 0x05;

// Flash address map
const uint32_t FLASH_BASE_ADDR = 0x80000000;
const uint32_t FLASH_TOTAL_SIZE = 0x00200000; // 2 MB

// Watchdog / PWM registers
const uint32_t REG_PWM_CHNEN = 0xf040001c;
const uint32_t REG_PWM_CTRL = 0xf0400020;
const uint32_t REG_PWM_RELOAD = 0xf0400024;
const uint32_t REG_PWM_MUX = 0xf0e00100;
}

class BootromComm {
    public:
	static constexpr size_t kStatusHeaderSize = 2;
	static constexpr size_t kWrappedCommandSize = 4;
	static constexpr size_t kAesGcmIvSize = 12;
	static constexpr size_t kAesGcmTagSize = 16;
	static constexpr size_t kSha256DigestSize = 32;
	static constexpr size_t kScHeaderSizeSha =
		kStatusHeaderSize + kWrappedCommandSize + kSha256DigestSize;
	static constexpr size_t kScHeaderSizeAes =
		kStatusHeaderSize + kWrappedCommandSize + kAesGcmIvSize +
		kAesGcmTagSize;

	explicit BootromComm(UsbComm &usb)
		: usb_(usb)
	{
	}
	~BootromComm();
	void set_aes_key(const uint8_t *key)
	{
		std::memcpy(aes_key_.data(), key, aes_key_.size());
	}
	void set_raw_cmd_dump(std::ofstream *ptr)
	{
		raw_cmd_fw_ptr_ = ptr;
	}

	// --- Basic Bootrom commands ---
	int get_bootrom_info();
	int get_flash_info();
	int system_reset();

	// --- Security related ---
	int disable_watchdog();
	int disable_wp(); // Disable SPI write-protect

	// --- Memory / Flash operations (Secure Channel) ---
	int read_reg(uint32_t addr, uint32_t &value_out);
	int write_reg(uint32_t addr, uint32_t value);
	int backup_flash(const std::string &filename, uint32_t total_size);
	int read_flash(uint32_t offset, uint32_t length,
		       std::vector<uint8_t> &buffer);
	int erase_flash(uint32_t offset, uint32_t size);
	int write_flash(uint32_t offset, std::span<const uint8_t> data);
	int write_enable();
	uint32_t Wrapped_Algo = CRYPTO_SHA_256;

    private:
	std::array<uint8_t, 32> aes_key_{};
	std::ofstream *raw_cmd_fw_ptr_ = nullptr;

	UsbComm &usb_;
	// --- SPI / Flash status register ---
	uint8_t get_sr1();
	void set_sr1(uint8_t sr1);

	void dump_raw_cmd(const std::vector<uint8_t> &cmd);

	int send_bootrom_command_raw(const std::vector<uint8_t> &data,
				     size_t resp_size,
				     std::vector<uint8_t> &resp_out,
				     int timeout_ms = 200);

	int send_bootrom_command_sha(const std::vector<uint8_t> &data,
				     size_t resp_size,
				     std::vector<uint8_t> &resp_out,
				     int timeout_ms = 200);

	int send_bootrom_command_aes(const std::vector<uint8_t> &data,
				     size_t resp_size,
				     std::vector<uint8_t> &resp_out,
				     int timeout_ms = 200);

	int send_bootrom_command(const std::vector<uint8_t> &data,
				 size_t resp_size,
				 std::vector<uint8_t> &resp_out,
				 int timeout_ms = 200);
};

#endif // BOOTROM_H_
