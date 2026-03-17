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
#include <ranges>
#include <thread>
#include <vector>

using EvpCipherCtxPtr =
	std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>;

int GenerateGcmIv(std::span<uint8_t> iv_buf)
{
	if (RAND_bytes(iv_buf.data(), iv_buf.size()) != 1) {
		std::println(stderr,
			     "[ERROR] Cryptographically secure RNG failed! "
			     "Critical security risk, aborting.");
		return -1;
	}
	return 0;
}

bool aes_gcm_encrypt_openssl(std::span<const uint8_t> key,
			     std::span<const uint8_t> iv,
			     std::span<const uint8_t> aad,
			     std::span<const uint8_t> plaintext,
			     std::span<uint8_t> ciphertext,
			     std::span<uint8_t> tag)
{
	EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &::EVP_CIPHER_CTX_free);
	if (!ctx)
		return false;

	int len;

	if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL, NULL,
			       NULL) != 1) {
		return false;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv.size(),
				NULL) != 1) {
		return false;
	}

	if (EVP_EncryptInit_ex(ctx.get(), NULL, NULL, key.data(), iv.data()) !=
	    1) {
		return false;
	}

	if (!aad.empty()) {
		if (EVP_EncryptUpdate(ctx.get(), NULL, &len, aad.data(),
				      aad.size()) != 1) {
			return false;
		}
	}

	if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &len,
			      plaintext.data(), plaintext.size()) != 1) {
		return false;
	}

	if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + len, &len) !=
	    1) {
		return false;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, tag.size(),
				tag.data()) != 1) {
		return false;
	}

	return true;
}

bool aes_gcm_decrypt_openssl(std::span<const uint8_t> key,
			     std::span<const uint8_t> iv,
			     std::span<const uint8_t> aad,
			     std::span<const uint8_t> ciphertext,
			     std::span<const uint8_t> tag,
			     std::span<uint8_t> plaintext)
{
	EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &::EVP_CIPHER_CTX_free);
	if (!ctx)
		return false;

	int len;

	if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL, NULL,
			       NULL) != 1) {
		return false;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv.size(),
				NULL) != 1) {
		return false;
	}

	if (EVP_DecryptInit_ex(ctx.get(), NULL, NULL, key.data(), iv.data()) !=
	    1) {
		return false;
	}

	if (!aad.empty()) {
		if (EVP_DecryptUpdate(ctx.get(), NULL, &len, aad.data(),
				      aad.size()) != 1) {
			return false;
		}
	}

	if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len,
			      ciphertext.data(), ciphertext.size()) != 1) {
		return false;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag.size(),
				const_cast<uint8_t *>(tag.data())) != 1) {
		return false;
	}

	if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len) <= 0) {
		return false;
	}

	return true;
}

// ========================================================
// Bootrom Communication Implementation
// ========================================================
BootromComm::~BootromComm()
{
	OPENSSL_cleanse(aes_key_.data(), aes_key_.size());
}

std::expected<std::vector<uint8_t>, int>
BootromComm::SendBootromCommandRaw(std::span<const uint8_t> data,
				   size_t resp_size, int timeout_ms)
{
	DumpRawCmd(data);
	std::vector<uint8_t> tx_data(data.begin(), data.end());

	if (usb_.BulkWrite(kBootromEpOutBulk, tx_data, timeout_ms) < 0)
		return std::unexpected(-1);

	std::vector<uint8_t> ret(4096 + kStatusHeaderSize, 0);
	int transferred = usb_.BulkRead(kBootromEpInBulk, ret, timeout_ms);

	if (transferred <= 0)
		return std::unexpected(-2);

	ret.resize(transferred);

	if (transferred < kStatusHeaderSize)
		return std::unexpected(-3);
	if (ret[0] != 0 || ret[1] != 0)
		return std::unexpected(-4);

	return std::vector<uint8_t>(ret.begin() + kStatusHeaderSize, ret.end());
}

std::expected<std::vector<uint8_t>, int>
BootromComm::SendBootromCommandSha(std::span<const uint8_t> data,
				   size_t resp_size, int timeout_ms)
{
	std::vector<uint8_t> cmd(kScHeaderSizeSha + data.size(), 0);
	uint8_t *cmd_ptr = cmd.data();

	// kStatusHeaderSize = 2
	cmd_ptr[0] = EgisBootrom::CMD_SECURE_WRAPPED;
	cmd_ptr[1] = BootromComm::wrapped_algo;

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

	DumpRawCmd(cmd);

	if (usb_.BulkWrite(kBootromEpOutBulk, cmd, timeout_ms) < 0)
		return std::unexpected(-1);

	std::vector<uint8_t> ret(4096 + kScHeaderSizeSha, 0);
	int transferred = usb_.BulkRead(kBootromEpInBulk, ret, timeout_ms);

	if (transferred <= 0)
		return std::unexpected(-2);

	ret.resize(transferred);

	if (transferred < kScHeaderSizeSha)
		return std::unexpected(-3);
	if (ret[0] != 0 || ret[1] != 0)
		return std::unexpected(-4);

	return std::vector<uint8_t>(ret.begin() + kScHeaderSizeSha, ret.end());
}

std::expected<std::vector<uint8_t>, int>
BootromComm::SendBootromCommandAes(std::span<const uint8_t> data,
				   size_t resp_size, int timeout_ms)
{
	std::vector<uint8_t> cmd(kScHeaderSizeAes + data.size(), 0);
	uint8_t *cmd_ptr = cmd.data();

	// kStatusHeaderSize = 2
	cmd_ptr[0] = EgisBootrom::CMD_SECURE_WRAPPED;
	cmd_ptr[1] = BootromComm::wrapped_algo;

	// kWrappedCommandSize = 4
	uint32_t payload_len = static_cast<uint32_t>(data.size());
	uint8_t *cmd_wrapped_size = cmd_ptr + kStatusHeaderSize;
	std::memcpy(cmd_wrapped_size, &payload_len, sizeof(payload_len));

	// kAesGcmIvSize = 12
	uint8_t *cmd_iv = cmd_ptr + kStatusHeaderSize + kWrappedCommandSize;

	// kAesGcmTagSize = 16
	uint8_t *cmd_tag = cmd_iv + kAesGcmIvSize;

	// Wrapped Command Buffer
	uint8_t *cmd_buffer = cmd_tag + kAesGcmTagSize;

	GenerateGcmIv(std::span<uint8_t>(cmd_iv, kAesGcmIvSize));
	// The Additional Authenticated Data (AAD) is required by the Bootrom
	// protocol. It represents the Chip ID and revision as a 32-bit
	// little-endian integer. {0x01, 0x00, 0x00, 0x00} explicitly sets this
	// identifier for AES-GCM authentication.
	uint8_t aad[4] = { 0x01, 0x00, 0x00, 0x00 };

	if (!aes_gcm_encrypt_openssl(
		    aes_key_, std::span<const uint8_t>(cmd_iv, kAesGcmIvSize),
		    aad, data, std::span<uint8_t>(cmd_buffer, data.size()),
		    std::span<uint8_t>(cmd_tag, kAesGcmTagSize))) {
		return std::unexpected(-1);
	}

	DumpRawCmd(cmd);

	if (usb_.BulkWrite(kBootromEpOutBulk, cmd, timeout_ms) < 0)
		return std::unexpected(-2);

	std::vector<uint8_t> ret(4096 + kScHeaderSizeAes, 0);
	int transferred = usb_.BulkRead(kBootromEpInBulk, ret, timeout_ms);
	if (transferred <= 0)
		return std::unexpected(-3);
	ret.resize(transferred);

	if (transferred < kScHeaderSizeAes)
		return std::unexpected(-4);

	if (ret[0] != 0 || ret[1] != 0)
		return std::unexpected(-5);

	uint32_t rsp_wrapped_len = 0;
	std::memcpy(&rsp_wrapped_len, &ret[kStatusHeaderSize],
		    sizeof(rsp_wrapped_len));

	uint8_t *rsp_iv = ret.data() + kStatusHeaderSize + kWrappedCommandSize;
	uint8_t *rsp_tag = rsp_iv + kAesGcmIvSize;
	uint8_t *rsp_payload = rsp_tag + kAesGcmTagSize;

	if (rsp_wrapped_len + kScHeaderSizeAes > transferred) {
		rsp_wrapped_len = transferred - kScHeaderSizeAes;
	}

	if (!aes_gcm_decrypt_openssl(
		    aes_key_, std::span<const uint8_t>(rsp_iv, kAesGcmIvSize),
		    aad, std::span<const uint8_t>(rsp_payload, rsp_wrapped_len),
		    std::span<const uint8_t>(rsp_tag, kAesGcmTagSize),
		    std::span<uint8_t>(rsp_payload, rsp_wrapped_len))) {
		std::println(
			stderr,
			"[ERROR] AES-GCM Authentication Failed (Tag mismatch)");
		return std::unexpected(-6);
	}

	return std::vector<uint8_t>(rsp_payload, rsp_payload + rsp_wrapped_len);
}

std::expected<std::vector<uint8_t>, int>
BootromComm::SendBootromCommand(std::span<const uint8_t> data, size_t resp_size,
				int timeout_ms)
{
	if ((data[0] >= EgisBootrom::CMD_MEM_READ) &&
	    (data[0] <= EgisBootrom::CMD_OTP_WRITE)) {
		switch (BootromComm::wrapped_algo) {
		case CRYPTO_SHA_256:
			return SendBootromCommandSha(data, resp_size,
						     timeout_ms);
		case CRYPTO_AES_GCM:
			return SendBootromCommandAes(data, resp_size,
						     timeout_ms);
		default:
			std::println(stderr,
				     "--- Wrapped_Algo is not defined ---");
			return std::unexpected(-1);
		}
	}
	return SendBootromCommandRaw(data, resp_size, timeout_ms);
}

int BootromComm::WriteReg(uint32_t addr, uint32_t value)
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

	auto resp = SendBootromCommand(cmd, 0);
	return resp.has_value() ? 0 : resp.error();
}

std::expected<uint32_t, int> BootromComm::ReadReg(uint32_t addr)
{
	std::vector<uint8_t> cmd;
	cmd.push_back(EgisBootrom::CMD_MEM_READ);
	for (int i = 0; i < 4; i++)
		cmd.push_back((addr >> (i * 8)) & 0xFF);
	cmd.push_back(0x04);
	for (int i = 0; i < 4; i++)
		cmd.push_back((1 >> (i * 8)) & 0xFF);

	auto resp = SendBootromCommand(cmd, 4);
	if (!resp.has_value() || resp->size() < 4)
		return std::unexpected(resp.error_or(-1));

	uint32_t value_out;
	std::memcpy(&value_out, resp->data(), 4);
	return value_out;
}

int BootromComm::WriteEnable()
{
	WriteReg(EgisBootrom::SPI_REG_TFMAT, 0);
	uint32_t tctrl = EgisBootrom::TRNS_MODE_NONE_DATA |
			 EgisBootrom::TCTRL_CMD_EN_MSK;
	WriteReg(EgisBootrom::SPI_REG_TCTRL, tctrl);
	WriteReg(EgisBootrom::SPI_REG_TFMAT, EgisBootrom::TFMAT);
	WriteReg(EgisBootrom::SPI_REG_CMD, EgisBootrom::FLASH_WREN);
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	return 0;
}

uint8_t BootromComm::GetSr1()
{
	uint32_t tmp;

	WriteReg(EgisBootrom::SPI_REG_RDCNT, 0);
	uint32_t tctrl = EgisBootrom::TRNS_MODE_READ_ONLY |
			 EgisBootrom::TCTRL_CMD_EN_MSK;
	WriteReg(EgisBootrom::SPI_REG_TCTRL, tctrl);
	WriteReg(EgisBootrom::SPI_REG_TFMAT, EgisBootrom::TFMAT);
	WriteReg(EgisBootrom::SPI_REG_CMD, EgisBootrom::FLASH_RDSR);
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	if (auto res = ReadReg(EgisBootrom::SPI_REG_DATA)) {
		tmp = res.value();
	} else {
		tmp = 0;
	}
	return static_cast<uint8_t>(tmp & 0xFF);
}

void BootromComm::SetSr1(uint8_t sr1)
{
	WriteEnable();

	WriteReg(EgisBootrom::SPI_REG_WRCNT, 0);
	uint32_t tctrl = EgisBootrom::TRNS_MODE_WRITE_ONLY |
			 EgisBootrom::TCTRL_CMD_EN_MSK;
	WriteReg(EgisBootrom::SPI_REG_TCTRL, tctrl);
	WriteReg(EgisBootrom::SPI_REG_TFMAT, EgisBootrom::TFMAT);
	WriteReg(EgisBootrom::SPI_REG_DATA, static_cast<uint32_t>(sr1));
	WriteReg(EgisBootrom::SPI_REG_CMD, EgisBootrom::FLASH_WRSR);

	std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void BootromComm::DumpRawCmd(std::span<const uint8_t> cmd)
{
	if (!raw_cmd_fw_ptr_ || !raw_cmd_fw_ptr_->is_open())
		return;

	raw_cmd_fw_ptr_->write(reinterpret_cast<const char *>(cmd.data()),
			       cmd.size());
	raw_cmd_fw_ptr_->flush();
}

int BootromComm::BackupFlash(std::string_view filename, uint32_t total_size)
{
	std::ofstream outfile(std::string(filename),
			      std::ios::binary | std::ios::trunc);
	if (!outfile.is_open())
		return -1;

	const uint32_t len_chunk = 4096;

	std::println("--- Executing CMD_MEM_READ (0x20) ---");

	for (uint32_t addr = 0; addr < total_size; addr += len_chunk) {
		uint32_t current_read = std::min(len_chunk, total_size - addr);

		std::vector<uint8_t> data;
		data.push_back(EgisBootrom::CMD_MEM_READ);
		uint32_t flash_addr = EgisBootrom::FLASH_BASE_ADDR + addr;
		for (int i = 0; i < 4; i++)
			data.push_back((flash_addr >> (i * 8)) & 0xFF);
		data.push_back(0x01);
		for (int i = 0; i < 4; i++)
			data.push_back((current_read >> (i * 8)) & 0xFF);

		auto resp = SendBootromCommand(data, current_read, 5000);
		if (!resp.has_value()) {
			std::println(stderr,
				     "\n[HALT] Failed at address 0x{:x}", addr);
			return -1;
		}

		size_t received = resp->size();

		if (received < current_read) {
			std::println(
				stderr,
				"\n[WARN] Incomplete payload received, only got {} bytes",
				received);
			return -1;
		}
		outfile.write(reinterpret_cast<const char *>(resp->data()),
			      received);

		std::print("\rBackup Progress: {} / {} ({:.1f}%)",
			   addr + current_read, total_size,
			   (100.0 * (addr + current_read) / total_size));
		std::fflush(stdout);

		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	outfile.close();
	std::println("\nFlash Backup Completed");
	return 0;
}

std::expected<std::vector<uint8_t>, int> BootromComm::ReadFlash(uint32_t offset,
								uint32_t length)
{
	std::vector<uint8_t> buffer;
	const uint32_t len_chunk = 1024;

	for (uint32_t addr_offset = 0; addr_offset < length;
	     addr_offset += len_chunk) {
		uint32_t current_read =
			std::min(len_chunk, length - addr_offset);
		uint32_t addr = offset + addr_offset;

		std::vector<uint8_t> cmd;
		cmd.push_back(EgisBootrom::CMD_MEM_READ);

		uint32_t flash_addr = EgisBootrom::FLASH_BASE_ADDR + addr;
		for (int i = 0; i < 4; i++)
			cmd.push_back((flash_addr >> (i * 8)) & 0xFF);

		cmd.push_back(0x01);
		for (int i = 0; i < 4; i++)
			cmd.push_back((current_read >> (i * 8)) & 0xFF);

		auto resp = SendBootromCommand(cmd, current_read, 5000);
		if (!resp.has_value()) {
			std::println(
				stderr,
				"[ERROR] read_flash failed at addr 0x{:x} with code {}",
				flash_addr, resp.error());
			return std::unexpected(-1);
		}
		if (resp->size() < current_read) {
			std::println(
				stderr,
				"[ERROR] read_flash failed at addr 0x{:x} with code {}",
				flash_addr, resp.error());
			return std::unexpected(-2);
		}

		buffer.insert(buffer.end(), resp->begin(), resp->end());
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return buffer;
}

int BootromComm::EraseFlash(uint32_t offset, uint32_t size)
{
	std::println("--- Executing CMD_FLASH_ERASE (0x22) ---");

	std::vector<uint8_t> data;
	data.push_back(EgisBootrom::CMD_FLASH_ERASE);

	uint32_t addr = EgisBootrom::FLASH_BASE_ADDR + offset;
	for (int i = 0; i < 4; i++)
		data.push_back((addr >> (i * 8)) & 0xFF);
	for (int i = 0; i < 4; i++)
		data.push_back((size >> (i * 8)) & 0xFF);

	auto resp = SendBootromCommand(data, 0, 5000);

	if (resp.has_value()) {
		std::println("Erase Success: {} bytes at 0x{:08x}", size, addr);
		return 0;
	} else {
		std::println(stderr, "Erase Failed!");
		return -1;
	}
}

int BootromComm::WriteFlash(uint32_t offset, std::span<const uint8_t> data)
{
	if (data.size() > 32 * 1024) {
		std::println(
			stderr,
			"[ERROR] Payload size {} exceeds max 32KB chunk limit",
			data.size());
		return -1;
	}

	std::vector<uint8_t> cmd;
	cmd.reserve(10 + data.size());

	uint32_t addr = EgisBootrom::FLASH_BASE_ADDR + offset;
	cmd.push_back(EgisBootrom::CMD_MEM_WRITE);
	for (int j = 0; j < 4; j++) {
		cmd.push_back((addr >> (j * 8)) & 0xFF);
	}
	cmd.push_back(0x01);
	for (int j = 0; j < 4; j++) {
		cmd.push_back((data.size() >> (j * 8)) & 0xFF);
	}
	cmd.insert(cmd.end(), data.begin(), data.end());

	auto resp = SendBootromCommand(cmd, 0, 5000);

	if (!resp.has_value()) {
		std::println(
			stderr,
			"\n[HALT] USB/Bulk transmission failed at 0x{:x}, ret={}",
			addr, resp.error());
		return -1;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	return 0;
}

int BootromComm::DisableWatchdog()
{
	std::println("--- Disabling Watchdog ---");
	if (WriteReg(EgisBootrom::REG_PWM_CTRL, 0x00000004) != 0)
		return -1;
	if (WriteReg(EgisBootrom::REG_PWM_RELOAD, 0x00070a21) != 0)
		return -2;
	if (WriteReg(EgisBootrom::REG_PWM_CHNEN, 0x00000008) != 0)
		return -3;
	if (WriteReg(EgisBootrom::REG_PWM_MUX, 0x0c000004) != 0)
		return -4;
	return 0;
}

int BootromComm::DisableWp()
{
	uint8_t sr1_old = GetSr1();
	std::println("Old SR1: 0x{:02x}", sr1_old);
	SetSr1(0);

	uint8_t sr1_new = GetSr1();
	std::println("New SR1: 0x{:02x}", sr1_new);

	if (sr1_new != 0) {
		std::println(stderr, "Failed to clear WP bits");
		return -1;
	}
	return 0;
}

int BootromComm::GetBootromInfo()
{
	std::vector<uint8_t> cmd = { EgisBootrom::CMD_GET_BOOT_INFO };
	auto resp = SendBootromCommand(cmd, 12);
	if (!resp.has_value())
		return -1;

	if (resp->size() >= 12) {
		uint32_t version, max_size, max_fw_upgrade_size;
		std::memcpy(&version, resp->data(), 4);
		std::memcpy(&max_size, resp->data() + 4, 4);
		std::memcpy(&max_fw_upgrade_size, resp->data() + 8, 4);
		std::println("Bootrom Version: 0x{:x}", version);
		std::println("Max Command Size: {} bytes", max_size);
		std::println("Max FW Upgrade Size: {} bytes",
			     max_fw_upgrade_size);
	}
	return 0;
}

int BootromComm::GetFlashInfo()
{
	std::vector<uint8_t> cmd = { EgisBootrom::CMD_FLASH_INFO };

	std::println("--- Fetching Flash Information ---");

	auto resp = SendBootromCommand(cmd, 40);
	if (!resp.has_value()) {
		std::println(stderr, "[ERROR] Unable to retrieve Flash info.");
		return -1;
	}

	if (resp->size() < 40) {
		std::println(stderr, "[ERROR] Response length insufficient: {}",
			     resp->size());
		return -2;
	}

	uint32_t flash_size = 0;
	std::memcpy(&flash_size, resp->data(), 4);

	std::array<char, 9> prod_name{};
	std::memcpy(prod_name.data(), resp->data() + 8, 8);

	std::println("[SUCCESS] Flash Data Received:");
	std::println("  Product Name  : {}", prod_name.data());
	std::println("  Flash Size  : {} bytes ({} MB)", flash_size,
		     flash_size / 1024 / 1024);
	return 0;
}

int BootromComm::SystemReset()
{
	std::println("--- Executing System Reset (0x12) ---");

	std::vector<uint8_t> cmd = { EgisBootrom::CMD_SYSTEM_RESET, 0x01 };

	auto resp = SendBootromCommand(cmd, 0, 500);

	if (resp.has_value()) {
		std::println("[SUCCESS] Reset command acknowledged.");
		return 0;
	} else {
		std::println("[INFO] Command sent, device is resetting...");
		return 0;
	}
}
