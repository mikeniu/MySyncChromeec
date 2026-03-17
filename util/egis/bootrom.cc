/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "bootrom.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <openssl/aes.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <print>
#include <thread>
#include <vector>

constexpr uint8_t BOOTROM_EP_OUT_BULK = 0x02;
constexpr uint8_t BOOTROM_EP_IN_BULK = 0x81;

int GCM_IV_GEN(int nSize, uint8_t *pIVBuf)
{
	if (RAND_bytes(pIVBuf, nSize) != 1) {
		std::println(stderr,
			     "[ERROR] Cryptographically secure RNG failed! "
			     "Critical security risk, aborting.");
		return -1;
	}
	return 0;
}

bool aes_gcm_encrypt_openssl(const uint8_t *key, const uint8_t *iv,
			     size_t iv_len, const uint8_t *aad, size_t aad_len,
			     const uint8_t *plaintext, size_t plaintext_len,
			     uint8_t *ciphertext, uint8_t *tag)
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		return false;

	int len;

	if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL) !=
	    1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (aad_len > 0) {
		if (EVP_EncryptUpdate(ctx, NULL, &len, aad, aad_len) != 1) {
			EVP_CIPHER_CTX_free(ctx);
			return false;
		}
	}

	if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext,
			      plaintext_len) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	EVP_CIPHER_CTX_free(ctx);
	return true;
}

bool aes_gcm_decrypt_openssl(const uint8_t *key, const uint8_t *iv,
			     size_t iv_len, const uint8_t *aad, size_t aad_len,
			     const uint8_t *ciphertext, size_t ciphertext_len,
			     const uint8_t *tag, uint8_t *plaintext)
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		return false;

	int len;

	if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL) !=
	    1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (aad_len > 0) {
		if (EVP_DecryptUpdate(ctx, NULL, &len, aad, aad_len) != 1) {
			EVP_CIPHER_CTX_free(ctx);
			return false;
		}
	}

	if (EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext,
			      ciphertext_len) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
				const_cast<uint8_t *>(tag)) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	if (EVP_DecryptFinal_ex(ctx, plaintext + len, &len) <= 0) {
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	EVP_CIPHER_CTX_free(ctx);
	return true;
}

// ========================================================
// Bootrom Communication Implementation
// ========================================================
BootromComm::~BootromComm()
{
	OPENSSL_cleanse(aes_key_.data(), aes_key_.size());
}

int BootromComm::send_bootrom_command_raw(const std::vector<uint8_t> &data,
					  size_t resp_size,
					  std::vector<uint8_t> &resp_out,
					  int timeout_ms)
{
	resp_out.clear();

	dump_raw_cmd(data);

	if (usb_.bulk_write(BOOTROM_EP_OUT_BULK, data, timeout_ms) < 0)
		return -1;

	std::vector<uint8_t> ret(4096 + kStatusHeaderSize, 0);
	int transferred = usb_.bulk_read(BOOTROM_EP_IN_BULK, ret, timeout_ms);

	if (transferred <= 0)
		return -2;

	ret.resize(transferred);

	if (transferred < kStatusHeaderSize)
		return -3;
	if (ret[0] != 0 || ret[1] != 0)
		return -4;

	resp_out.assign(ret.begin() + kStatusHeaderSize, ret.end());

	return 0;
}

int BootromComm::send_bootrom_command_sha(const std::vector<uint8_t> &data,
					  size_t resp_size,
					  std::vector<uint8_t> &resp_out,
					  int timeout_ms)
{
	resp_out.clear();
	std::vector<uint8_t> cmd(kScHeaderSizeSha + data.size(), 0);
	uint8_t *cmd_ptr = cmd.data();

	// kStatusHeaderSize = 2
	cmd_ptr[0] = EgisBootrom::CMD_SECURE_WRAPPED;
	cmd_ptr[1] = BootromComm::Wrapped_Algo;

	// kWrappedCommandSize = 4
	uint32_t payload_len = static_cast<uint32_t>(data.size());
	uint8_t *cmd_wrapped_size = cmd_ptr + kStatusHeaderSize;
	std::memcpy(cmd_wrapped_size, &payload_len, sizeof(payload_len));

	// kSha256DigestSize = 32
	std::array<uint8_t, kSha256DigestSize> hash{};
	SHA256(data.data(), data.size(), hash.data());
	uint8_t *cmd_hash = cmd_ptr + kStatusHeaderSize + kWrappedCommandSize;
	std::memcpy(cmd_hash, hash.data(), hash.size());

	// Wrapped Command Buffer
	uint8_t *cmd_buffer = cmd_hash + kSha256DigestSize;
	std::memcpy(cmd_buffer, data.data(), data.size());

	dump_raw_cmd(cmd);

	if (usb_.bulk_write(BOOTROM_EP_OUT_BULK, cmd, timeout_ms) < 0)
		return -1;

	std::vector<uint8_t> ret(4096 + kScHeaderSizeSha, 0);
	int transferred = usb_.bulk_read(BOOTROM_EP_IN_BULK, ret, timeout_ms);

	if (transferred <= 0)
		return -2;

	ret.resize(transferred);

	if (transferred < kScHeaderSizeSha)
		return -3;
	if (ret[0] != 0 || ret[1] != 0)
		return -4;

	resp_out.assign(ret.begin() + kScHeaderSizeSha, ret.end());
	return 0;
}

int BootromComm::send_bootrom_command_aes(const std::vector<uint8_t> &data,
					  size_t resp_size,
					  std::vector<uint8_t> &resp_out,
					  int timeout_ms)
{
	resp_out.clear();
	std::vector<uint8_t> cmd(kScHeaderSizeAes + data.size(), 0);
	uint8_t *cmd_ptr = cmd.data();

	// kStatusHeaderSize = 2
	cmd_ptr[0] = EgisBootrom::CMD_SECURE_WRAPPED;
	cmd_ptr[1] = BootromComm::Wrapped_Algo;

	// kWrappedCommandSize = 4
	uint32_t payload_len = static_cast<uint32_t>(data.size());
	uint8_t *cmd_wrapped_size = cmd_ptr + kStatusHeaderSize;
	std::memcpy(cmd_wrapped_size, &payload_len, sizeof(payload_len));

	// kAesGcmIvSize = 12
	uint8_t *cmd_IV = cmd_ptr + kStatusHeaderSize + kWrappedCommandSize;

	// kAesGcmTagSize = 16
	uint8_t *cmd_tag = cmd_IV + kAesGcmIvSize;

	// Warpped Command Buffer
	uint8_t *cmd_buffer = cmd_tag + kAesGcmTagSize;
	std::memcpy(cmd_buffer, data.data(), data.size());

	GCM_IV_GEN(kAesGcmIvSize, cmd_IV);
	// The Additional Authenticated Data (AAD) is required by the Bootrom
	// protocol. It represents the Chip ID and revision as a 32-bit
	// little-endian integer. {0x01, 0x00, 0x00, 0x00} explicitly sets this
	// identifier for AES-GCM authentication.
	uint8_t AAD[4] = { 0x01, 0x00, 0x00, 0x00 };

	if (!aes_gcm_encrypt_openssl(aes_key_.data(), cmd_IV, kAesGcmIvSize,
				     AAD, sizeof(AAD), data.data(), data.size(),
				     cmd_buffer, cmd_tag)) {
		return -1;
	}

	dump_raw_cmd(cmd);

	if (usb_.bulk_write(BOOTROM_EP_OUT_BULK, cmd, timeout_ms) < 0)
		return -2;

	std::vector<uint8_t> ret(4096 + kScHeaderSizeAes, 0);
	int transferred = usb_.bulk_read(BOOTROM_EP_IN_BULK, ret, timeout_ms);
	if (transferred <= 0)
		return -3;
	ret.resize(transferred);

	if (transferred < kScHeaderSizeAes)
		return -4;

	if (ret[0] != 0 || ret[1] != 0)
		return -5;

	uint32_t rsp_wrapped_len = 0;
	std::memcpy(&rsp_wrapped_len, &ret[kStatusHeaderSize],
		    sizeof(rsp_wrapped_len));

	uint8_t *rsp_IV = ret.data() + kStatusHeaderSize + kWrappedCommandSize;
	uint8_t *rsp_tag = rsp_IV + kAesGcmIvSize;
	uint8_t *rsp_payload = rsp_tag + kAesGcmTagSize;

	if (rsp_wrapped_len + kScHeaderSizeAes > transferred) {
		rsp_wrapped_len = transferred - kScHeaderSizeAes;
	}

	if (!aes_gcm_decrypt_openssl(aes_key_.data(), rsp_IV, kAesGcmIvSize,
				     AAD, sizeof(AAD), rsp_payload,
				     rsp_wrapped_len, rsp_tag, rsp_payload)) {
		std::println(
			stderr,
			"[ERROR] AES-GCM Authentication Failed (Tag mismatch)");
		return -6;
	}

	resp_out.assign(rsp_payload, rsp_payload + rsp_wrapped_len);

	return 0;
}

int BootromComm::send_bootrom_command(const std::vector<uint8_t> &data,
				      size_t resp_size,
				      std::vector<uint8_t> &resp_out,
				      int timeout_ms)
{
	if ((data[0] >= EgisBootrom::CMD_MEM_READ) &&
	    (data[0] <= EgisBootrom::CMD_OTP_WRITE)) {
		switch (BootromComm::Wrapped_Algo) {
		case CRYPTO_SHA_256:
			return send_bootrom_command_sha(data, resp_size,
							resp_out, timeout_ms);
		case CRYPTO_AES_GCM:
			return send_bootrom_command_aes(data, resp_size,
							resp_out, timeout_ms);
		default:
			std::println(stderr,
				     "--- Wrapped_Algo is not defined ---");
			return -1;
		}
	}
	return send_bootrom_command_raw(data, resp_size, resp_out, timeout_ms);
}

int BootromComm::write_reg(uint32_t addr, uint32_t value)
{
	std::vector<uint8_t> cmd;
	cmd.push_back(EgisBootrom::CMD_MEM_WRITE);
	for (int i = 0; i < 4; ++i)
		cmd.push_back((addr >> (i * 8)) & 0xFF);
	cmd.push_back(0x04);
	uint32_t count = 1;
	for (int i = 0; i < 4; ++i)
		cmd.push_back((count >> (i * 8)) & 0xFF);
	for (int i = 0; i < 4; ++i)
		cmd.push_back((value >> (i * 8)) & 0xFF);

	std::vector<uint8_t> resp;
	return send_bootrom_command(cmd, 0, resp);
}

int BootromComm::read_reg(uint32_t addr, uint32_t &value_out)
{
	std::vector<uint8_t> cmd;
	cmd.push_back(EgisBootrom::CMD_MEM_READ);
	for (int i = 0; i < 4; i++)
		cmd.push_back((addr >> (i * 8)) & 0xFF);
	cmd.push_back(0x04);
	for (int i = 0; i < 4; i++)
		cmd.push_back((1 >> (i * 8)) & 0xFF);

	std::vector<uint8_t> resp;
	if (send_bootrom_command(cmd, 4, resp) != 0 || resp.size() < 4)
		return -1;

	std::memcpy(&value_out, resp.data(), 4);
	return 0;
}

int BootromComm::write_enable()
{
	write_reg(EgisBootrom::SPI_REG_TFMAT, 0);
	uint32_t tctrl = EgisBootrom::TRNS_MODE_NONE_DATA |
			 EgisBootrom::TCTRL_CMD_EN_MSK;
	write_reg(EgisBootrom::SPI_REG_TCTRL, tctrl);
	write_reg(EgisBootrom::SPI_REG_TFMAT, EgisBootrom::TFMAT);
	write_reg(EgisBootrom::SPI_REG_CMD, EgisBootrom::FLASH_WREN);
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	return 0;
}

uint8_t BootromComm::get_sr1()
{
	uint32_t tmp;

	write_reg(EgisBootrom::SPI_REG_RDCNT, 0);
	uint32_t tctrl = EgisBootrom::TRNS_MODE_READ_ONLY |
			 EgisBootrom::TCTRL_CMD_EN_MSK;
	write_reg(EgisBootrom::SPI_REG_TCTRL, tctrl);
	write_reg(EgisBootrom::SPI_REG_TFMAT, EgisBootrom::TFMAT);
	write_reg(EgisBootrom::SPI_REG_CMD, EgisBootrom::FLASH_RDSR);
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	read_reg(EgisBootrom::SPI_REG_DATA, tmp);
	return static_cast<uint8_t>(tmp & 0xFF);
}

void BootromComm::set_sr1(uint8_t sr1)
{
	write_enable();

	write_reg(EgisBootrom::SPI_REG_WRCNT, 0);
	uint32_t tctrl = EgisBootrom::TRNS_MODE_WRITE_ONLY |
			 EgisBootrom::TCTRL_CMD_EN_MSK;
	write_reg(EgisBootrom::SPI_REG_TCTRL, tctrl);
	write_reg(EgisBootrom::SPI_REG_TFMAT, EgisBootrom::TFMAT);
	write_reg(EgisBootrom::SPI_REG_DATA, static_cast<uint32_t>(sr1));
	write_reg(EgisBootrom::SPI_REG_CMD, EgisBootrom::FLASH_WRSR);

	std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void BootromComm::dump_raw_cmd(const std::vector<uint8_t> &cmd)
{
	if (!raw_cmd_fw_ptr_ || !raw_cmd_fw_ptr_->is_open())
		return;

	raw_cmd_fw_ptr_->write(reinterpret_cast<const char *>(cmd.data()),
			       cmd.size());
	raw_cmd_fw_ptr_->flush();
	std::println("[DUMP] cmd_size={}", cmd.size());
}

int BootromComm::backup_flash(const std::string &filename, uint32_t total_size)
{
	std::ofstream outfile(filename, std::ios::binary | std::ios::trunc);
	if (!outfile.is_open())
		return -1;

	uint32_t addr = 0;
	const uint32_t len_chunk = 4096;
	std::vector<uint8_t> resp;

	std::println("--- Executing CMD_MEM_READ (0x20) ---");

	while (addr < total_size) {
		uint32_t current_read = std::min(len_chunk, total_size - addr);

		std::vector<uint8_t> data;
		data.push_back(EgisBootrom::CMD_MEM_READ);
		uint32_t flash_addr = EgisBootrom::FLASH_BASE_ADDR + addr;
		for (int i = 0; i < 4; i++)
			data.push_back((flash_addr >> (i * 8)) & 0xFF);
		data.push_back(0x01);
		for (int i = 0; i < 4; i++)
			data.push_back((current_read >> (i * 8)) & 0xFF);

		int ret = send_bootrom_command(data, current_read, resp, 5000);
		if (ret != 0) {
			std::println(stderr,
				     "\n[HALT] Failed at address 0x{:x}", addr);
			return -1;
		}

		size_t received = resp.size();

		if (received < current_read) {
			std::println(
				stderr,
				"\n[WARN] Incomplete payload received, only got {} bytes",
				received);
			return -1;
		}
		outfile.write(reinterpret_cast<const char *>(resp.data()),
			      received);

		addr += current_read;
		std::print("\rBackup Progress: {} / {} ({:.1f}%)", addr,
			   total_size, (100.0 * addr / total_size));
		std::fflush(stdout);

		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	outfile.close();
	std::println("\nFlash Backup Completed");
	return 0;
}

int BootromComm::read_flash(uint32_t offset, uint32_t length,
			    std::vector<uint8_t> &buffer)
{
	buffer.clear();
	uint32_t addr = offset;
	const uint32_t len_chunk = 1024;
	std::vector<uint8_t> resp;

	while (addr < offset + length) {
		uint32_t current_read =
			std::min(len_chunk, offset + length - addr);

		std::vector<uint8_t> cmd;
		cmd.push_back(EgisBootrom::CMD_MEM_READ);

		uint32_t flash_addr = EgisBootrom::FLASH_BASE_ADDR + addr;
		for (int i = 0; i < 4; i++)
			cmd.push_back((flash_addr >> (i * 8)) & 0xFF);

		cmd.push_back(0x01);
		for (int i = 0; i < 4; i++)
			cmd.push_back((current_read >> (i * 8)) & 0xFF);

		int ret = send_bootrom_command(cmd, current_read, resp, 5000);
		if (ret != 0) {
			std::println(
				stderr,
				"[ERROR] read_flash failed at addr 0x{:x} with code {}",
				flash_addr, ret);
			return -1;
		}
		if (resp.size() < current_read) {
			std::println(
				stderr,
				"[ERROR] read_flash failed at addr 0x{:x} with code {}",
				flash_addr, ret);
			return -2;
		}

		buffer.insert(buffer.end(), resp.begin(), resp.end());
		addr += current_read;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return 0;
}

int BootromComm::erase_flash(uint32_t offset, uint32_t size)
{
	std::println("--- Executing CMD_FLASH_ERASE (0x22) ---");

	std::vector<uint8_t> data;
	data.push_back(EgisBootrom::CMD_FLASH_ERASE);

	uint32_t addr = EgisBootrom::FLASH_BASE_ADDR + offset;
	for (int i = 0; i < 4; i++)
		data.push_back((addr >> (i * 8)) & 0xFF);
	for (int i = 0; i < 4; i++)
		data.push_back((size >> (i * 8)) & 0xFF);

	std::vector<uint8_t> resp;
	int ret = send_bootrom_command(data, 0, resp, 5000);

	if (ret == 0) {
		std::println("Erase Success: {} bytes at 0x{:08x}", size, addr);
		return 0;
	} else {
		std::println(stderr, "Erase Failed!");
		return -1;
	}
}

int BootromComm::write_flash(uint32_t offset, std::span<const uint8_t> data)
{
	uint32_t total_size = data.size();
	uint32_t done = 0;
	const uint32_t len_chunk = 32 * 1024;

	std::println("--- Executing CMD_MEM_WRITE (0x21) Total: {} bytes ---",
		     total_size);

	std::vector<uint8_t> cmd;
	std::vector<uint8_t> resp;
	cmd.reserve(10 + len_chunk);
	resp.reserve(16);

	while (done < total_size) {
		uint32_t current_chunk = std::min(len_chunk, total_size - done);
		uint32_t addr = EgisBootrom::FLASH_BASE_ADDR + offset + done;

		cmd.clear();
		resp.clear();
		cmd.push_back(EgisBootrom::CMD_MEM_WRITE);
		for (int i = 0; i < 4; i++) {
			cmd.push_back((addr >> (i * 8)) & 0xFF);
		}
		cmd.push_back(0x01);
		for (int i = 0; i < 4; i++) {
			cmd.push_back((current_chunk >> (i * 8)) & 0xFF);
		}
		auto chunk = data.subspan(done, current_chunk);
		cmd.insert(cmd.end(), chunk.begin(), chunk.end());

		int ret = send_bootrom_command(cmd, 0, resp, 5000);

		if (ret != 0) {
			std::println(
				stderr,
				"[HALT] USB/Bulk transmission failed at 0x{:x}, ret={}",
				addr, ret);
			return -1;
		}
		done += current_chunk;
		std::fflush(stdout);

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return 0;
}

int BootromComm::disable_watchdog()
{
	std::println("--- Disabling Watchdog ---");
	if (write_reg(EgisBootrom::REG_PWM_CTRL, 0x00000004) != 0)
		return -1;
	if (write_reg(EgisBootrom::REG_PWM_RELOAD, 0x00070a21) != 0)
		return -2;
	if (write_reg(EgisBootrom::REG_PWM_CHNEN, 0x00000008) != 0)
		return -3;
	if (write_reg(EgisBootrom::REG_PWM_MUX, 0x0c000004) != 0)
		return -4;
	return 0;
}

int BootromComm::disable_wp()
{
	uint8_t sr1_old = get_sr1();
	std::println("Old SR1: 0x{:02x}", sr1_old);
	set_sr1(0);

	uint8_t sr1_new = get_sr1();
	std::println("New SR1: 0x{:02x}", sr1_new);

	if (sr1_new != 0) {
		std::println(stderr, "Failed to clear WP bits");
		return -1;
	}
	return 0;
}

int BootromComm::get_bootrom_info()
{
	std::vector<uint8_t> cmd = { EgisBootrom::CMD_GET_BOOT_INFO };
	std::vector<uint8_t> resp;
	if (send_bootrom_command(cmd, 12, resp) != 0)
		return -1;

	if (resp.size() >= 12) {
		uint32_t version, max_size, max_fw_upgrade_size;
		memcpy(&version, &resp[0], 4); //  0xC0010102
		memcpy(&max_size, &resp[4], 4); //  327680
		memcpy(&max_fw_upgrade_size, &resp[8], 4); // 311232
		std::println("Bootrom Version: 0x{:x}", version);
		std::println("Max Command Size: {} bytes", max_size);
		std::println("Max FW Upgrade Size: {} bytes",
			     max_fw_upgrade_size);
	}
	return 0;
}

int BootromComm::get_flash_info()
{
	std::vector<uint8_t> cmd = { EgisBootrom::CMD_FLASH_INFO };
	std::vector<uint8_t> resp;

	std::println("--- Fetching Flash Information ---");

	if (send_bootrom_command(cmd, 40, resp) != 0) {
		std::println(stderr, "[ERROR] Unable to retrieve Flash info.");
		return -1;
	}

	if (resp.size() < 40) {
		std::println(stderr, "[ERROR] Response length insufficient: {}",
			     resp.size());
		return -2;
	}

	uint32_t flash_size = 0;
	memcpy(&flash_size, &resp[0], 4);

	char prod_name[9] = { 0 };
	memcpy(prod_name, &resp[8], 8);

	std::println("[SUCCESS] Flash Data Received:");
	std::println("  Product Name  : {}", prod_name);
	std::println("  Flash Size  : {} bytes ({} MB)", flash_size,
		     flash_size / 1024 / 1024);
	return 0;
}

int BootromComm::system_reset()
{
	std::println("--- Executing System Reset (0x12) ---");

	std::vector<uint8_t> cmd = { EgisBootrom::CMD_SYSTEM_RESET, 0x01 };
	std::vector<uint8_t> resp;

	int ret = send_bootrom_command(cmd, 0, resp, 500);

	if (ret == 0) {
		std::println("[SUCCESS] Reset command acknowledged.");
		return 0;
	} else {
		std::println("[INFO] Command sent, device is resetting...");
		return 0;
	}
}
