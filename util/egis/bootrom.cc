/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "bootrom.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <thread>
#include <vector>

static void die(const std::string &msg)
{
	std::cerr << "[BOOTROM FATAL] " << msg << std::endl;
	std::exit(EXIT_FAILURE);
}

int GCM_IV_GEN(int nSize, uint8_t *pIVBuf)
{
	if (RAND_bytes(pIVBuf, nSize) != 1) {
		for (int i = 0; i < nSize; i++)
			*(pIVBuf + i) = (rand() % 256);
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
	int len;

	EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL);
	EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv);

	if (aad_len > 0)
		EVP_EncryptUpdate(ctx, NULL, &len, aad, aad_len);

	EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len);
	EVP_EncryptFinal_ex(ctx, ciphertext + len, &len);

	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);

	EVP_CIPHER_CTX_free(ctx);
	return true;
}

bool aes_gcm_decrypt_openssl(const uint8_t *key, const uint8_t *iv,
			     size_t iv_len, const uint8_t *aad, size_t aad_len,
			     const uint8_t *ciphertext, size_t ciphertext_len,
			     const uint8_t *tag, uint8_t *plaintext)
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	int len, ret;

	EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL);
	EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv);

	if (aad_len > 0)
		EVP_DecryptUpdate(ctx, NULL, &len, aad, aad_len);

	EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len);

	EVP_CIPHER_CTX_ctrl(
		ctx, EVP_CTRL_GCM_SET_TAG, 16,
		const_cast<void *>(reinterpret_cast<const void *>(tag)));

	ret = EVP_DecryptFinal_ex(ctx, plaintext + len, &len);

	EVP_CIPHER_CTX_free(ctx);
	return (ret > 0);
}

// ========================================================
// Bootrom Communication Implementation
// ========================================================
int BootromComm::send_bootrom_command_sha_hmac(const std::vector<uint8_t> &data,
					       size_t expected_payload_size,
					       std::vector<uint8_t> &resp_out,
					       int timeout_ms,
					       bool secure_channel)
{
	resp_out.clear();
	std::vector<uint8_t> cmd;
	if (secure_channel) {
		// Build Secure Channel Header
		cmd.push_back(0xF4); // Magic Number
		cmd.push_back(BootromComm::Wrapped_Algo);
		uint32_t len = static_cast<uint32_t>(data.size());
		for (int i = 0; i < 4; ++i)
			cmd.push_back((len >> (i * 8)) & 0xFF);

		uint8_t hash[32];
		SHA256(data.data(), data.size(), hash);
		cmd.insert(cmd.end(), hash, hash + 32);
	}
	cmd.insert(cmd.end(), data.begin(), data.end());

	// === dump AES wrapped raw command ===
	dump_raw_cmd(cmd);

	// USB Bulk Write to Endpoint 0x02
	if (usb_.bulk_write(0x02, cmd, timeout_ms) < 0)
		return -1;

	// Buffer for receiving: Data + Header + 2 bytes status
	std::vector<uint8_t> ret(4096 + kScHeaderSizeSha + 2, 0);
	int transferred = usb_.bulk_read(0x81, ret, timeout_ms);

	if (transferred <= 0)
		return -2;

	ret.resize(transferred); // Resize to actual received length

	if (secure_channel) {
		if (transferred < kScHeaderSizeSha)
			return -3;
		// Check status code bytes [0, 1]
		if (ret[0] != 0 || ret[1] != 0)
			return -4;

		// Assign response payload (skipping Header)
		resp_out.assign(ret.begin() + kScHeaderSizeSha, ret.end());
	} else {
		resp_out = ret;
	}

	return 0;
}

int BootromComm::send_bootrom_command_aes(const std::vector<uint8_t> &data,
					  size_t expected_payload_size,
					  std::vector<uint8_t> &resp_out,
					  int timeout_ms)
{
	resp_out.clear();
	std::vector<uint8_t> cmd(kScHeaderSizeAes + data.size(), 0);
	uint8_t *cmd_ptr = cmd.data();

	// Header
	cmd_ptr[0] = 0xF4;
	cmd_ptr[1] = BootromComm::Wrapped_Algo; // 0x02 for AES-GCM
	uint32_t payload_len = static_cast<uint32_t>(data.size());
	memcpy(cmd_ptr + 2, &payload_len, sizeof(payload_len));

	uint8_t *cmd_IV = cmd_ptr + 2 + 4;
	uint8_t *cmd_tag = cmd_IV + kAesGcmIvSize;
	uint8_t *cmd_payload = cmd_tag + kAesGcmTagSize;

	memcpy(cmd_payload, data.data(), data.size());

	GCM_IV_GEN(kAesGcmIvSize, cmd_IV);
	uint8_t AAD[4] = { 0x01, 0x00, 0x00, 0x00 };

	if (!aes_gcm_encrypt_openssl(aes_key_, cmd_IV, kAesGcmIvSize, AAD,
				     sizeof(AAD), data.data(), data.size(),
				     cmd_payload, cmd_tag)) {
		return -1;
	}

	// === dump AES wrapped raw command ===
	dump_raw_cmd(cmd);

	if (usb_.bulk_write(0x02, cmd, timeout_ms) < 0)
		return -2;

	std::vector<uint8_t> ret(4096 + kScHeaderSizeAes + 2, 0);
	int transferred = usb_.bulk_read(0x81, ret, timeout_ms);
	if (transferred <= 0)
		return -3;
	ret.resize(transferred);

	if (transferred < kScHeaderSizeAes)
		return -4;

	if (ret[0] != 0 || ret[1] != 0)
		return -5;

	uint32_t rsp_wrapped_len = *reinterpret_cast<const uint32_t *>(&ret[2]);
	uint8_t *rsp_IV = ret.data() + 2 + 4;
	uint8_t *rsp_tag = rsp_IV + kAesGcmIvSize;
	uint8_t *rsp_payload = rsp_tag + kAesGcmTagSize;

	if (rsp_wrapped_len + kScHeaderSizeAes > transferred) {
		rsp_wrapped_len = transferred - kScHeaderSizeAes;
	}

	if (!aes_gcm_decrypt_openssl(aes_key_, rsp_IV, kAesGcmIvSize, AAD,
				     sizeof(AAD), rsp_payload, rsp_wrapped_len,
				     rsp_tag, rsp_payload)) {
		std::cerr
			<< "[ERROR] AES-GCM Authentication Failed (Tag mismatch)"
			<< std::endl;
		return -6;
	}

	resp_out.assign(rsp_payload, rsp_payload + rsp_wrapped_len);

	return 0;
}

int BootromComm::send_bootrom_command(const std::vector<uint8_t> &data,
				      size_t expected_payload_size,
				      std::vector<uint8_t> &resp_out,
				      int timeout_ms)
{
	if (data[0] >= 0x20 && data[0] <= 0x26) {
		switch (BootromComm::Wrapped_Algo) {
		case CRYPTO_SHA_256:
		case CRYPTO_HMAC_SHA256:
			return send_bootrom_command_sha_hmac(
				data, expected_payload_size, resp_out,
				timeout_ms, true);

		case CRYPTO_AES_GCM:
			return send_bootrom_command_aes(data,
							expected_payload_size,
							resp_out, timeout_ms);

		default:
			std::cout << "--- Wrapped_Algo is not defined ---"
				  << std::endl;
			return -1;
		}
	}
	return send_bootrom_command_sha_hmac(data, expected_payload_size,
					     resp_out, timeout_ms, false);
}

int BootromComm::write_reg(uint32_t addr, uint32_t value)
{
	std::vector<uint8_t> cmd;
	cmd.push_back(EgisBootrom::CMD_MEM_WRITE);
	for (int i = 0; i < 4; ++i)
		cmd.push_back((addr >> (i * 8)) & 0xFF);
	cmd.push_back(0x04); // Access Mode: 32-bit
	uint32_t count = 1;
	for (int i = 0; i < 4; ++i)
		cmd.push_back((count >> (i * 8)) & 0xFF);
	for (int i = 0; i < 4; ++i)
		cmd.push_back((value >> (i * 8)) & 0xFF);

	std::vector<uint8_t> resp;
	// MEM_WRITE expects a 2-byte status code [0x00, 0x00]
	return send_bootrom_command(cmd, 2, resp);
}

int BootromComm::read_reg(uint32_t addr, uint32_t &value_out)
{
	std::vector<uint8_t> cmd;
	cmd.push_back(EgisBootrom::CMD_MEM_READ);
	for (int i = 0; i < 4; i++)
		cmd.push_back((addr >> (i * 8)) & 0xFF);
	cmd.push_back(0x04); // 32-bit mode
	for (int i = 0; i < 4; i++)
		cmd.push_back((1 >> (i * 8)) & 0xFF); // Count 1
	std::vector<uint8_t> resp;
	if (send_bootrom_command(cmd, 4, resp) != 0 || resp.size() < 4)
		return -1;
	memcpy(&value_out, resp.data(), 4);
	return 0;
}

int BootromComm::write_enable()
{
	write_reg(EgisBootrom::SPI_REG_TFMAT, 0);
	uint32_t tctrl = EgisBootrom::TRNS_MODE_NONE_DATA |
			 EgisBootrom::TCTRL_CMD_EN_MSK;
	write_reg(EgisBootrom::SPI_REG_TCTRL, tctrl);
	write_reg(EgisBootrom::SPI_REG_TFMAT, EgisBootrom::TFMAT);
	write_reg(EgisBootrom::SPI_REG_CMD, EgisBootrom::FLASH_WREN); // Send
								      // Write
								      // Enable
								      // Command
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	return 0;
}

uint8_t BootromComm::get_sr1()
{
	uint32_t tmp;

	write_reg(EgisBootrom::SPI_REG_BASE + 0x1C, 0); // SPI_REG_RDCNT
	uint32_t tctrl = EgisBootrom::TRNS_MODE_READ_ONLY |
			 EgisBootrom::TCTRL_CMD_EN_MSK;
	write_reg(EgisBootrom::SPI_REG_BASE + 0x20, tctrl); // SPI_REG_TCTRL
	write_reg(EgisBootrom::SPI_REG_BASE + 0x10,
		  EgisBootrom::TFMAT); // SPI_REG_TFMAT
	write_reg(EgisBootrom::SPI_REG_BASE + 0x24,
		  EgisBootrom::FLASH_RDSR); // SPI_REG_CMD (Read Status
					    // Register)
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	read_reg(EgisBootrom::SPI_REG_BASE + 0x2C, tmp); // SPI_REG_DATA
	return static_cast<uint8_t>(tmp & 0xFF);
}

void BootromComm::set_sr1(uint8_t sr1)
{
	write_enable();

	write_reg(EgisBootrom::SPI_REG_BASE + 0x18, 0); // SPI_REG_WRCNT
	uint32_t tctrl = EgisBootrom::TRNS_MODE_WRITE_ONLY |
			 EgisBootrom::TCTRL_CMD_EN_MSK;
	write_reg(EgisBootrom::SPI_REG_BASE + 0x20, tctrl); // SPI_REG_TCTRL
	write_reg(EgisBootrom::SPI_REG_BASE + 0x10,
		  EgisBootrom::TFMAT); // SPI_REG_TFMAT
	write_reg(EgisBootrom::SPI_REG_BASE + 0x2C, static_cast<uint32_t>(sr1));
	write_reg(EgisBootrom::SPI_REG_BASE + 0x24,
		  EgisBootrom::FLASH_WRSR); // SPI_REG_CMD (Write Status
					    // Register)

	std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void BootromComm::dump_raw_cmd(const std::vector<uint8_t> &cmd)
{
	if (!raw_cmd_fw_ptr_ || !raw_cmd_fw_ptr_->is_open())
		return;

	raw_cmd_fw_ptr_->write(reinterpret_cast<const char *>(cmd.data()),
			       cmd.size());
	raw_cmd_fw_ptr_->flush();
	std::cout << "[DUMP] cmd_size=" << cmd.size() << std::endl;
}

int BootromComm::backup_flash(const std::string &filename, uint32_t total_size)
{
	std::ofstream outfile(filename, std::ios::binary | std::ios::trunc);
	if (!outfile.is_open())
		return -1;

	uint32_t addr = 0;
	const uint32_t len_chunk = 4096;
	std::vector<uint8_t> resp;

	std::cout << "--- Executing CMD_READ_FLASH (0x20) ---" << std::endl;

	while (addr < total_size) {
		uint32_t current_read = std::min(len_chunk, total_size - addr);

		std::vector<uint8_t> data;
		data.push_back(0x20); // CMD_READ_FLASH
		uint32_t flash_addr = EgisBootrom::FLASH_BASE_ADDR + addr;
		for (int i = 0; i < 4; i++)
			data.push_back((flash_addr >> (i * 8)) & 0xFF);
		data.push_back(0x01); // Flash Type
		for (int i = 0; i < 4; i++)
			data.push_back((current_read >> (i * 8)) & 0xFF);

		int ret = send_bootrom_command(data, 2 + current_read, resp,
					       5000);
		if (ret != 0 || resp.size() < 2) {
			std::cerr << "\n[HALT] Failed at address 0x" << std::hex
				  << addr << std::dec << std::endl;
			return -1;
		}

		if (resp[0] != 0 || resp[1] != 0) {
			std::cerr << "\n[ERROR] Bootrom status code abnormal: "
				  << static_cast<int>(resp[0]) << " "
				  << static_cast<int>(resp[1]) << std::endl;
			return -1;
		}

		size_t received = resp.size() - 2;
		if (received > 0)
			outfile.write(reinterpret_cast<const char *>(&resp[2]),
				      received);
		if (received < current_read) {
			std::cerr
				<< "\n[WARN] Incomplete payload received, only got "
				<< received << " bytes\n";
			return -1;
		}

		addr += current_read;
		std::cout << "\rBackup Progress: " << addr << " / "
			  << total_size << " (" << std::fixed
			  << std::setprecision(1) << (100.0 * addr / total_size)
			  << "%)" << std::flush;

		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	outfile.close();
	std::cout << "\nFlash Backup Completed\n";
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

		cmd.push_back(0x01); // Flash Type
		for (int i = 0; i < 4; i++)
			cmd.push_back((current_read >> (i * 8)) & 0xFF);

		int ret =
			send_bootrom_command(cmd, 2 + current_read, resp, 5000);
		if (ret != 0 || resp.size() < 2)
			return -1;
		if (resp[0] != 0 || resp[1] != 0)
			return -1;

		buffer.insert(buffer.end(), resp.begin() + 2,
			      resp.begin() + 2 + current_read);
		addr += current_read;

		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return 0;
}

int BootromComm::erase_flash(uint32_t offset, uint32_t size)
{
	std::cout << "--- Executing CMD_FLASH_ERASE (0x22) ---" << std::endl;

	// Command format: [0x22] + [Addr(4)] + [Len(4)]
	std::vector<uint8_t> data;
	data.push_back(EgisBootrom::CMD_FLASH_ERASE);

	uint32_t addr = EgisBootrom::FLASH_BASE_ADDR + offset;
	for (int i = 0; i < 4; i++)
		data.push_back((addr >> (i * 8)) & 0xFF);
	for (int i = 0; i < 4; i++)
		data.push_back((size >> (i * 8)) & 0xFF);

	std::vector<uint8_t> resp;
	int ret = send_bootrom_command(data, 2, resp, 5000);

	if (ret == 0 && resp.size() >= 2 && resp[0] == 0 && resp[1] == 0) {
		std::cout << "Erase Success: " << size << " bytes at 0x"
			  << std::hex << addr << std::dec << std::endl;
		return 0;
	} else {
		std::cerr << "Erase Failed!" << std::endl;
		return -1;
	}
}

int BootromComm::write_flash(uint32_t offset, const std::vector<uint8_t> &data)
{
	uint32_t total_size = data.size();
	uint32_t done = 0;
	const uint32_t len_chunk = 32 * 1024; // 32KB chunking

	std::cout << "--- Executing CMD_MEM_WRITE (0x21) Total: " << total_size
		  << " bytes ---" << std::endl;

	while (done < total_size) {
		uint32_t current_chunk = std::min(len_chunk, total_size - done);
		uint32_t addr = EgisBootrom::FLASH_BASE_ADDR + offset + done;

		// Construct Command Packet
		std::vector<uint8_t> cmd;
		cmd.push_back(EgisBootrom::CMD_MEM_WRITE);
		for (int i = 0; i < 4; i++)
			cmd.push_back((addr >> (i * 8)) & 0xFF);
		cmd.push_back(0x01); // Flash type
		for (int i = 0; i < 4; i++)
			cmd.push_back((current_chunk >> (i * 8)) & 0xFF);
		cmd.insert(cmd.end(), data.begin() + done,
			   data.begin() + done + current_chunk);

		std::vector<uint8_t> resp;
		int ret = send_bootrom_command(cmd, 2, resp, 5000);

		if (ret != 0) {
			std::cerr << "[HALT] USB/Bulk transmission failed at 0x"
				  << std::hex << addr << std::dec
				  << ", ret=" << ret << std::endl;
			return -1;
		}
		if (resp.size() < 2) {
			std::cerr << "[HALT] Return length insufficient at 0x"
				  << std::hex << addr << std::dec
				  << ", resp.size=" << resp.size() << std::endl;
			return -1;
		}
		if (resp[0] != 0 || resp[1] != 0) {
			std::cerr << "[HALT] Bootrom status code error at 0x"
				  << std::hex << addr << std::dec
				  << ", status=" << std::hex
				  << static_cast<int>(resp[0]) << " "
				  << static_cast<int>(resp[1]) << std::dec
				  << std::endl;
			return -1;
		}

		done += current_chunk;
		std::cout << std::flush;

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return 0;
}

int BootromComm::disable_watchdog()
{
	std::cout << "--- Disabling Watchdog ---" << std::endl;
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
	std::cout << "Old SR1: 0x" << std::hex << static_cast<int>(sr1_old)
		  << std::dec << std::endl;
	set_sr1(0); // Clear Status Register bits (unprotect)

	uint8_t sr1_new = get_sr1();
	std::cout << "New SR1: 0x" << std::hex << static_cast<int>(sr1_new)
		  << std::dec << std::endl;

	if (sr1_new != 0) {
		std::cerr << "Failed to clear WP bits" << std::endl;
		return -1;
	}
	return 0;
}

int BootromComm::get_bootrom_info()
{
	std::vector<uint8_t> cmd = { EgisBootrom::CMD_GET_BOOT_INFO };
	std::vector<uint8_t> resp;
	if (send_bootrom_command(cmd, 14, resp) != 0)
		return -1;

	if (resp.size() >= 10) {
		uint32_t version, max_size;
		memcpy(&version, &resp[2], 4);
		memcpy(&max_size, &resp[6], 4);
		std::cout << "Bootrom Version: 0x" << std::hex << version
			  << std::dec << std::endl;
		std::cout << "Max Command Size: " << max_size << " bytes"
			  << std::endl;
	}
	return 0;
}

int BootromComm::get_flash_info()
{
	std::vector<uint8_t> cmd = { EgisBootrom::CMD_FLASH_INFO };
	std::vector<uint8_t> resp;

	std::cout << "--- Fetching Flash Information ---" << std::endl;

	if (send_bootrom_command(cmd, 42, resp) != 0) {
		std::cerr << "[ERROR] Unable to retrieve Flash info."
			  << std::endl;
		return -1;
	}

	if (resp.size() < 42) {
		std::cerr << "[ERROR] Response length insufficient: "
			  << resp.size() << std::endl;
		return -2;
	}

	// Offset 2-6: Flash 1 Size
	uint32_t flash1_size = 0;
	memcpy(&flash1_size, &resp[2], 4);

	// Offset 6-10: Flash 2 Size
	uint32_t flash2_size = 0;
	memcpy(&flash2_size, &resp[6], 4);

	// Offset 10-18: Product Name (8 bytes string)
	char prod_name[9] = { 0 };
	memcpy(prod_name, &resp[10], 8);

	// Offset 18-22: Bootloader Version
	uint32_t bl_ver = 0;
	memcpy(&bl_ver, &resp[18], 4);

	// Offset 22-26: Flash ID (Manufacturer & Device ID)
	uint32_t flash_id = 0;
	memcpy(&flash_id, &resp[22], 4);

	std::cout << "[SUCCESS] Flash Data Received:" << std::endl;
	std::cout << "  Product Name  : " << prod_name << std::endl;
	std::cout << "  Flash ID      : 0x" << std::hex << (flash_id & 0xFFFFFF)
		  << std::dec << std::endl;
	std::cout << "  Flash 1 Size  : " << flash1_size << " bytes ("
		  << flash1_size / 1024 / 1024 << " MB)" << std::endl;
	std::cout << "  Flash 2 Size  : " << flash2_size << " bytes"
		  << std::endl;
	std::cout << "  Bootloader Ver: 0x" << std::hex << bl_ver << std::dec
		  << std::endl;

	return 0;
}

int BootromComm::system_reset()
{
	std::cout << "--- Executing System Reset (0x12) ---" << std::endl;

	std::vector<uint8_t> cmd = { EgisBootrom::CMD_SYSTEM_RESET, 0x01 };
	std::vector<uint8_t> resp;

	int ret = send_bootrom_command(cmd, 2, resp, 500);

	// Note: If reset is successful, USB read may fail due to disconnection
	if (ret == 0) {
		std::cout << "[SUCCESS] Reset command acknowledged."
			  << std::endl;
		return 0;
	} else {
		std::cout << "[INFO] Command sent, device is resetting..."
			  << std::endl;
		return 0;
	}
}

int BootromComm::get_otp_info()
{
	std::cout << "---- Executing CMD_OTP_READ (0x23) ---" << std::endl;
	struct OtpCmd {
		std::string name;
		std::vector<uint8_t> cmd;
		size_t data_len;
	};

	std::vector<OtpCmd> cmds = {
		{ "HW setting", { 0x23, 0x04, 0x00, 0x01, 0x00 }, 6 },
		{ "BROM sett1", { 0x23, 0x07, 0x00, 0x01, 0x00 }, 6 },
		{ "BROM sett3", { 0x23, 0x09, 0x00, 0x01, 0x00 }, 6 },

		{ "SK_D", { 0x23, 0x4C, 0x00, 0x08, 0x00 }, 34 },
		{ "SK_D2", { 0x23, 0x54, 0x00, 0x08, 0x00 }, 34 },
		{ "SK_D3", { 0x23, 0x5C, 0x00, 0x08, 0x00 }, 34 },

		{ "EGIS_SKEY", { 0x23, 0x64, 0x00, 0x08, 0x00 }, 34 },
		{ "EGIS_SKEY2", { 0x23, 0x6C, 0x00, 0x08, 0x00 }, 34 },
		{ "EGIS_SKEY3", { 0x23, 0x74, 0x00, 0x08, 0x00 }, 34 },
	};

	for (const auto &c : cmds) {
		std::vector<uint8_t> resp;
		size_t payload_size = 2 + c.data_len; // First two bytes are
						      // status

		send_bootrom_command(c.cmd, payload_size, resp, 1000);
		std::cout << "[" << c.name << "] (" << resp.size() << " bytes)"
			  << std::endl;

		if (resp.size() < 2) {
			std::cerr << "  Response too short!" << std::endl;
			continue;
		}

		if (resp[0] != 0 || resp[1] != 0) {
			std::cerr << "[ERROR] Status code abnormal: "
				  << static_cast<int>(resp[0]) << " "
				  << static_cast<int>(resp[1]) << std::endl;
		}

		// Hex dump (skip first 2 status bytes)
		for (size_t i = 2; i < resp.size(); ++i) {
			std::cout << std::hex << std::uppercase << std::setw(2)
				  << std::setfill('0')
				  << static_cast<int>(resp[i]) << " ";
			if ((i - 1) % 16 == 0) {
				std::cout << std::endl;
			}
		}
		if ((resp.size() - 2) % 16 != 0) {
			std::cout << std::dec << std::endl;
		} else {
			std::cout << std::dec;
		}
		// HW setting parsing
		if (c.name == "HW setting" && resp.size() >= 4) {
			uint8_t b0 = resp[2];
			std::cout << "  Force BROM       : " << (b0 & 0x01)
				  << std::endl;
			std::cout << "  Debug Disable    : "
				  << (b0 & 0x02 ? 1 : 0) << std::endl;
			std::cout << "  Debug Secure     : "
				  << (b0 & 0x04 ? 1 : 0) << std::endl;
			std::cout << "  Extclk Disable   : "
				  << (b0 & 0x08 ? 1 : 0) << std::endl;
		}

		// BROM sett1 parsing
		if (c.name == "BROM sett1" && resp.size() >= 6) {
			uint8_t b0 = resp[2], b1 = resp[3];
			std::cout << "  Secure Boot              : "
				  << (b0 & 0x01) << std::endl;
			std::cout << "  Secure Boot Signature Sel: "
				  << ((b0 & 0x02) ? 1 : 0) << std::endl;
			std::cout << "  Secure Boot App First    : "
				  << ((b0 & 0x04) ? 1 : 0) << std::endl;
			std::cout << "  Warm Reset Disable       : "
				  << (b1 & 0x01) << std::endl;
		}

		// BROM sett3 parsing
		if (c.name == "BROM sett3" && resp.size() >= 6) {
			uint8_t b0 = resp[2], b1 = resp[3];
			std::cout << "  Secure Channel Enable    : "
				  << (b0 & 0x01) << std::endl;
			std::cout << "  SHA256 Disable           : "
				  << ((b0 & 0x02) ? 1 : 0) << std::endl;
			std::cout << "  HMAC Disable             : "
				  << ((b0 & 0x04) ? 1 : 0) << std::endl;
			std::cout << "  SPI Slave Enable         : "
				  << ((b0 & 0x08) ? 1 : 0) << std::endl;
			std::cout << "  Anti-Rollback Enable     : "
				  << ((b0 & 0x10) ? 1 : 0) << std::endl;
			std::cout << "  Download SRAM Disable    : "
				  << ((b0 & 0x20) ? 1 : 0) << std::endl;
			std::cout << "  OTP SK_D3 No Read        : "
				  << ((b0 & 0x40) ? 1 : 0) << std::endl;
			std::cout << "  OTP SK_D2 No Read        : "
				  << ((b0 & 0x80) ? 1 : 0) << std::endl;
			std::cout << "  OTP SK_D No Read         : "
				  << (b1 & 0x01) << std::endl;
		}

		std::cout << "-----------------------------------------------"
			  << std::endl;
	}

	return 0;
}