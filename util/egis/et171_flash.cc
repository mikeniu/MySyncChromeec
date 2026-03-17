/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "bootrom.h"
#include "ec_comm.h"
#include "ec_protocol_defs.h"
#include "usb_comm.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <openssl/aes.h>
#include <openssl/crypto.h>
#include <print>
#include <ranges>
#include <span>
#include <sstream>
#include <string_view>
#include <thread>

namespace
{
constexpr uint16_t kVidGoogle = 0x18D1;
constexpr uint16_t kPidEgisApp = 0x5401;
constexpr uint16_t kVidEgis = 0x1C7A;
constexpr uint16_t kPidEgisBootloader = 0x1002;
}

bool LoadAesKey(std::string_view key_file_path,
		std::span<uint8_t, 32> key_buffer)
{
	std::ifstream key_file(std::string(key_file_path), std::ios::binary);
	if (!key_file) {
		std::println(stderr, "Failed to open key file: {}",
			     key_file_path);
		return false;
	}
	key_file.read(reinterpret_cast<char *>(key_buffer.data()), 32);
	if (key_file.gcount() != 32) {
		std::println(stderr,
			     "Key file size incorrect, expected 32 bytes");
		return false;
	}
	return true;
}

int ExecuteGetVersion(UsbComm &comm)
{
	EcComm ec_comm(comm);
	std::println("\n--- Executing GET_VERSION ---");
	auto resp = ec_comm.SendCommand(
		static_cast<uint16_t>(EcCommandId::EC_CMD_GET_VERSION), 1);
	if (!resp.has_value()) {
		std::println(
			stderr,
			"[ERROR] GET_VERSION command failed or timed out.");
		return -1;
	}
	auto payload = resp.value();
	std::println("Command SUCCESS.\nReceived payload length: {} bytes.",
		     payload.size());

	GetVersionResponse response = {};
	size_t copy_size = std::min(payload.size(), sizeof(GetVersionResponse));
	std::memcpy(&response, payload.data(), copy_size);

	response.ro_ver[sizeof(response.ro_ver) - 1] = '\0';
	response.rw_ver[sizeof(response.rw_ver) - 1] = '\0';
	std::println("\n--- Parsed Version Info ---");
	std::println("RO Version: {}", response.ro_ver);
	std::println("RW Version: {}", response.rw_ver);
	return 0;
}

int ExecuteBootLoader(UsbComm &comm)
{
	EcComm ec_comm(comm);
	std::println("\n--- Executing Enter Bootloader ---");
	auto resp = ec_comm.SendCommand(
		static_cast<uint16_t>(EcCommandId::EC_CMD_ENTER_BOOTLOADER), 0);
	if (!resp.has_value()) {
		std::println(
			stderr,
			"[ERROR] Failed to send Enter Bootloader command.");
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	// --- 1. Help Message ---
	if (argc < 2 || std::string(argv[1]) == "-h" ||
	    std::string(argv[1]) == "--help") {
		std::print(
			"\n=== ET171 Flash Tool ===\n\n"
			"Usage:\n"
			"  et171_flash <mode> <file> [options]\n\n"
			"Modes:\n"
			"  -flashfw <fw.bin>      [Mode A] Real-time encryption flash \n"
			"  -flashcmd <cmd.bin>    [Mode B] Direct command flash \n\n"
			"Options for Mode A (-flashfw):\n"
			"  -algo <type>           Encryption: SHA256 (default) or AESGCM\n"
			"  -key <key.bin>         AESGCM key file (required for AESGCM)\n"
			"  -offset <addr>         Flash start offset (Default: 0x00001000)\n"
			"  -size   <addr>         Flash size (Default: Fw.bin size())\n"
			"  -dumpcmd               Save processed commands to a file\n");
		return 0;
	}

	std::string mode;
	std::string fw_path;
	std::string key_path;
	std::string algo_type = "SHA256";
	uint32_t flash_offset = 0x00001000;
	uint32_t fw_size_override = 0x0;
	bool dump_cmd_fw = false;
	std::ofstream raw_cmd_fw_file;

	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];

		if (arg == "-flashfw") {
			if (i + 1 < argc) {
				mode = "flashfw";
				fw_path = argv[++i];
			} else {
				std::println(
					stderr,
					"[ERROR] -flashfw requires a firmware path.");
				return -1;
			}
		} else if (arg == "-flashcmd") {
			if (i + 1 < argc) {
				mode = "flashcmd";
				fw_path = argv[++i];
			} else {
				std::println(
					stderr,
					"[ERROR] -flashcmd requires a command file path.");
				return -1;
			}
		} else if (arg == "-key") {
			if (i + 1 < argc) {
				key_path = argv[++i];
			} else {
				std::println(
					stderr,
					"[ERROR] -key requires a file path.");
				return -1;
			}
		} else if (arg == "-algo") {
			if (i + 1 < argc) {
				algo_type = argv[++i];
			} else {
				std::println(
					stderr,
					"[ERROR] -algo requires a type (SHA256/AESGCM).");
				return -1;
			}
		} else if (arg == "-offset") {
			if (i + 1 < argc) {
				char *endptr = nullptr;
				const char *val_str = argv[++i];
				unsigned long val =
					strtol(val_str, &endptr, 16);

				if (endptr == val_str || *endptr != '\0' ||
				    val < 0) {
					std::println(
						stderr,
						"[ERROR] Invalid hex value for -offset: {}",
						val_str);
					return -1;
				}
				flash_offset = static_cast<uint32_t>(val);
			} else {
				std::println(
					stderr,
					"[ERROR] -offset requires a hex value (e.g., 0x1000).");
				return -1;
			}
		} else if (arg == "-size") {
			if (i + 1 < argc) {
				char *endptr = nullptr;
				const char *val_str = argv[++i];
				unsigned long val =
					strtol(val_str, &endptr, 16);

				if (endptr == val_str || *endptr != '\0' ||
				    val < 0) {
					std::println(
						stderr,
						"[ERROR] Invalid hex value for -size: {}",
						val_str);
					return -1;
				}
				fw_size_override = static_cast<uint32_t>(val);
			} else {
				std::println(
					stderr,
					"[ERROR] -size requires a hex value.");
				return -1;
			}
		} else if (arg == "-dumpcmd") {
			dump_cmd_fw = true;
		} else {
			std::println(stderr, "[ERROR] Unknown argument: {}",
				     arg);
			return -1;
		}
	}

	std::println("===== Parsed Arguments =====");
	std::println("Mode          : {}", mode);
	std::println("FW Path       : {}",
		     fw_path.empty() ? "(empty)" : fw_path);
	std::println("Key Path      : {}",
		     key_path.empty() ? "(empty)" : key_path);
	std::println("Algo          : {}", algo_type);
	std::println("Flash Offset  : 0x{:08X}", flash_offset);
	std::println("Flash Size    : 0x{:08X}", fw_size_override);
	std::println("Dump CMD FW   : {}", dump_cmd_fw);
	std::println("============================");

	if (mode == "-flashfw" && algo_type == "AESGCM" && key_path.empty()) {
		std::println(
			stderr,
			"[ERROR] AESGCM requires a key file. "
			"Use -key <key.bin> or provide it as the 3rd argument.");
		return -1;
	}

	// --- USB connect ---
	UsbComm app_comm(kVidGoogle, kPidEgisApp);
	if (app_comm.Connect() == 0) {
		ExecuteGetVersion(app_comm);
		std::println("--- Switching to Bootloader Mode ---");
		ExecuteBootLoader(app_comm);
		std::this_thread::sleep_for(std::chrono::milliseconds(800));
	}

	UsbComm boot_comm(kVidEgis, kPidEgisBootloader);
	bool connected = false;
	auto start_time = std::chrono::steady_clock::now();
	std::println("Connecting to Bootloader...");
	while (std::chrono::steady_clock::now() - start_time <
	       std::chrono::seconds(5)) {
		if (boot_comm.Connect() == 0) {
			connected = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	if (!connected) {
		std::println(stderr, "[ERROR] Connection timeout.");
		return -1;
	}
	BootromComm brom(boot_comm);
	if (algo_type == "AESGCM") {
		brom.wrapped_algo = CRYPTO_AES_GCM;
	} else if (algo_type == "SHA256") {
		brom.wrapped_algo = CRYPTO_SHA_256;
	} else {
		std::println(stderr, "[ERROR] Unsupported algorithm: {}",
			     algo_type);
		return -1;
	}

	if (mode == "flashfw") {
		std::println("[INFO] Flash FW Mode");
		std::println("FW  : {}", fw_path);
		std::println("Key : {}", key_path);
		std::println("Offset : 0x{:08X}", flash_offset);
		std::println("Size : 0x{:08X}", fw_size_override);

		if (fw_path.empty()) {
			std::println(stderr, "[ERROR] Missing firmware path");
			return -1;
		}

		if (algo_type == "AESGCM" && key_path.empty()) {
			std::println(stderr,
				     "[ERROR] AESGCM requires a key file");
			return -1;
		}

		// --- Load firmware ---
		std::ifstream infile(fw_path, std::ios::binary | std::ios::ate);
		if (!infile.is_open()) {
			std::println(stderr,
				     "[ERROR] Cannot open firmware file: {}",
				     fw_path);
			return -1;
		}

		// --- Load AES Key ---
		if (algo_type == "AESGCM") {
			std::array<uint8_t, 32> local_aes_key{};
			if (!LoadAesKey(key_path, local_aes_key)) {
				return -1;
			}
			brom.SetAesKey(local_aes_key.data());
		}

		// --- Dump RAW CMD ---
		if (dump_cmd_fw) {
			std::ostringstream oss;
			std::string fw_filename =
				fw_path.substr(fw_path.find_last_of("/\\") + 1);
			std::string algo_tag =
				(algo_type == "AESGCM") ? "aes" : "sha256";
			oss << fw_filename << "_" << algo_tag << "_cmd_"
			    << std::time(nullptr) << ".bin";
			raw_cmd_fw_file.open(oss.str(), std::ios::binary);
			if (!raw_cmd_fw_file.is_open()) {
				std::println(stderr,
					     "[ERROR] Cannot create dump file");
				return -1;
			}
			brom.SetRawCmdDump(&raw_cmd_fw_file);
		}

		// --- Disable WDT and Write Protect ---
		if (brom.DisableWatchdog() != 0)
			return -1;
		if (brom.DisableWp() != 0)
			return -1;

		std::streamsize fw_size = infile.tellg();
		infile.seekg(0, std::ios::beg);

		if (fw_size <= 0) {
			std::println(stderr, "[ERROR] Firmware file is empty");
			return -1;
		}
		if (fw_size_override > 0 && fw_size_override > fw_size) {
			std::println(
				stderr,
				"[ERROR] Override size (0x{:X}) exceeds actual file size (0x{:X}).",
				fw_size_override, fw_size);
			return -1;
		}

		fw_size = (fw_size_override > 0) ? fw_size_override : fw_size;
		std::vector<uint8_t> firmware(fw_size);
		if (!infile.read(reinterpret_cast<char *>(firmware.data()),
				 fw_size)) {
			std::println(stderr,
				     "[ERROR] Failed to read firmware file");
			return -1;
		}
		infile.close();

		// --- Flash firmware in chunks ---
		std::println("--- Flashing Full Binary with Key ---");

		if (flash_offset >= firmware.size()) {
			std::println(
				stderr,
				"[ERROR] flash_offset (0x{:X}) is beyond firmware size (0x{:X}).",
				flash_offset, firmware.size());
			return -1;
		}

		auto fw_payload = std::span<const uint8_t>(firmware).subspan(
			flash_offset);
		const size_t chunk_size = 32 * 1024;
		uint32_t current_addr = flash_offset;
		size_t done = 0;

		for (size_t i = 0; i < fw_payload.size(); i += chunk_size) {
			auto chunk = fw_payload.subspan(
				i, std::min(chunk_size, fw_payload.size() - i));
			if (brom.WriteFlash(current_addr, chunk) != 0) {
				std::println(
					stderr,
					"\n[HALT] Flash Write failed at address 0x{:X}",
					current_addr);
				return -1;
			}
			current_addr += chunk.size();
			done += chunk.size();
			std::print("\rWriting Progress: {} / {} ({:.1f}%)",
				   done, fw_payload.size(),
				   (static_cast<float>(done) /
				    fw_payload.size() * 100.0f));
			std::fflush(stdout);
		}

		std::println("\n[OK] Flash Full Binary Write successful");
		brom.SystemReset();
	} else if (mode == "flashcmd") {
		if (argc < 3) {
			std::println(stderr,
				     "[ERROR] -flashcmd requires fw.bin");
			return -1;
		}
		fw_path = argv[2];
		std::ifstream infile(fw_path, std::ios::binary | std::ios::ate);
		if (!infile.is_open()) {
			std::println(stderr,
				     "[ERROR] Cannot open firmware file: {}",
				     fw_path);
			return -1;
		}
		std::streamsize fw_size = infile.tellg();
		infile.seekg(0, std::ios::beg);
		std::vector<uint8_t> firmware(fw_size);
		if (!infile.read(reinterpret_cast<char *>(firmware.data()),
				 fw_size)) {
			std::println(stderr,
				     "[ERROR] Failed to read firmware file");
			return -1;
		}
		infile.close();

		std::println("--- Flash RAW CMD Binary Flashing ---");
		size_t done = 0;
		while (done < firmware.size()) {
			if (done + 34 > firmware.size()) {
				std::println(
					stderr,
					"\n[ERROR] Remaining data too small for a valid packet header.");
				break;
			}

			uint8_t header1 = firmware[done];
			uint8_t secure_algo = firmware[done + 1];

			if (header1 != EgisBootrom::CMD_SECURE_WRAPPED) {
				std::println(
					stderr,
					"[ERROR] Invalid header at offset 0x{:X}",
					done);
				return -1;
			}

			std::string algo_name;
			switch (secure_algo) {
			case 0x00:
				algo_name = "SHA256";
				break;
			case 0x02:
				algo_name = "AESGCM";
				break;
			default:
				std::println(
					stderr,
					"[ERROR] Unknown Secure_Algo 0x{:X} at offset 0x{:X}",
					static_cast<int>(secure_algo), done);
				return -1;
			}

			uint32_t wrapped_cmd_size = 0;
			std::memcpy(&wrapped_cmd_size, &firmware[done + 2],
				    sizeof(uint32_t));

			size_t total_packet_size =
				(secure_algo == 0x02) ?
					BootromComm::kScHeaderSizeAes :
					BootromComm::kScHeaderSizeSha;
			total_packet_size += wrapped_cmd_size;

			if (done + total_packet_size > firmware.size()) {
				std::println(
					stderr,
					"\n[ERROR] Packet truncated at offset 0x{:X}. Expected {} bytes.",
					done, total_packet_size);
				return -1;
			}

			std::vector<uint8_t> buf(firmware.begin() + done,
						 firmware.begin() + done +
							 total_packet_size);
			int ret = boot_comm.BulkWrite(
				BootromComm::kBootromEpOutBulk, buf,
				kTransferTimeoutMs);
			if (ret < 0) {
				std::println(
					stderr,
					"\n[ERROR] USB bulk_write failed at 0x{:X}, ret={}",
					done, ret);
				return -1;
			}

			std::vector<uint8_t> resp(128, 0);
			int transferred = boot_comm.BulkRead(
				BootromComm::kBootromEpInBulk, resp,
				kTransferTimeoutMs);
			if (transferred <= 0) {
				std::println(
					stderr,
					"\n[ERROR] Device no response (Timeout) at 0x{:X}",
					done);
				return -1;
			}

			done += total_packet_size;
			std::print("\rFlashing Progress: {} / {} ({:.1f}%)",
				   done, firmware.size(),
				   (static_cast<float>(done) / firmware.size() *
				    100.0f));
			std::fflush(stdout);
		}
		std::println("\n[OK] RAW CMD Flashing Finished");
		brom.SystemReset();
	}

	return 0;
}
