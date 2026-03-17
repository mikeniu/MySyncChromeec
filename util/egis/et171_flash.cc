/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "bootrom.h"
#include "ec_comm.h"
#include "ec_protocol_defs.h"
#include "usb_comm.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <openssl/aes.h>
#include <sstream>
#include <stdexcept>
#include <thread>

bool load_aes_key(const char *key_file_path, uint8_t *key_buffer)
{
	std::ifstream key_file(key_file_path, std::ios::binary);
	if (!key_file) {
		std::cerr << "Failed to open key file: " << key_file_path
			  << std::endl;
		return false;
	}
	key_file.read(reinterpret_cast<char *>(key_buffer), 32);
	if (key_file.gcount() != 32) {
		std::cerr << "Key file size incorrect, expected 32 bytes"
			  << std::endl;
		return false;
	}
	return true;
}

int ExecuteGetVersion(UsbComm &comm)
{
	ECComm ec_comm(comm);
	std::cout << "\n--- Executing GET_VERSION ---\n";
	std::vector<uint8_t> payload;
	bool success = ec_comm.send_command(
		static_cast<uint16_t>(ECCommandId::EC_CMD_GET_VERSION), 1, {},
		payload);
	if (!success) {
		std::cerr
			<< "[ERROR] GET_VERSION command failed or timed out.\n";
		return -1;
	}
	std::cout << "Command SUCCESS.\nReceived payload length: "
		  << payload.size() << " bytes.\n";

	GetVersionResponse response;
	size_t copy_size = std::min(payload.size(), sizeof(GetVersionResponse));
	std::memcpy(&response, payload.data(), copy_size);

	response.ro_ver[sizeof(response.ro_ver) - 1] = '\0';
	response.rw_ver[sizeof(response.rw_ver) - 1] = '\0';
	std::cout << "\n--- Parsed Version Info ---\n";
	std::cout << "RO Version: " << response.ro_ver << "\n";
	std::cout << "RW Version: " << response.rw_ver << "\n";
	return 0;
}

int ExecuteBootLoader(UsbComm &comm)
{
	ECComm ec_comm(comm);
	std::cout << "\n--- Executing Enter Bootloader ---\n";
	std::vector<uint8_t> resp;
	bool success = ec_comm.send_command(
		static_cast<uint16_t>(ECCommandId::EC_CMD_ENTER_BOOTLOADER), 0,
		{}, resp);
	if (!success) {
		std::cerr
			<< "[ERROR] Failed to send Enter Bootloader command.\n";
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	// --- 1. Help Message ---
	if (argc < 2 || std::string(argv[1]) == "-h" ||
	    std::string(argv[1]) == "--help") {
		std::cout
			<< "\n=== ET171 Flash Tool ===\n\n"
			<< "Usage:\n"
			<< "  et171_flash <mode> <file> [options]\n\n"
			<< "Modes:\n"
			<< "  -flashfw <fw.bin>      [Mode A] Real-time encryption flash \n"
			<< "  -flashcmd <cmd.bin>    [Mode B] Direct command flash \n\n"
			<< "Options for Mode A (-flashfw):\n"
			<< "  -algo <type>           Encryption: SHA256 (default) or AESGCM\n"
			<< "  -key <key.bin>         AESGCM key file (required for AESGCM)\n"
			<< "  -offset <addr>         Flash start offset (Default: 0x00001000)\n"
			<< "  -size   <addr>         Flash size (Default: Fw.bin size())\n"
			<< "  -dumpcmd               Save processed commands to a file\n"
			<< std::endl;
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
				std::cerr
					<< "[ERROR] -flashfw requires a firmware path."
					<< std::endl;
				return -1;
			}
		} else if (arg == "-flashcmd") {
			if (i + 1 < argc) {
				mode = "flashcmd";
				fw_path = argv[++i];
			} else {
				std::cerr
					<< "[ERROR] -flashcmd requires a command file path."
					<< std::endl;
				return -1;
			}
		} else if (arg == "-key") {
			if (i + 1 < argc) {
				key_path = argv[++i];
			} else {
				std::cerr
					<< "[ERROR] -key requires a file path."
					<< std::endl;
				return -1;
			}
		} else if (arg == "-algo") {
			if (i + 1 < argc) {
				algo_type = argv[++i];
			} else {
				std::cerr
					<< "[ERROR] -algo requires a type (SHA256/AESGCM)."
					<< std::endl;
				return -1;
			}
		} else if (arg == "-offset") {
			if (i + 1 < argc) {
				char *endptr = nullptr;
				const char *val_str = argv[++i];
				unsigned long val =
					strtoul(val_str, &endptr, 16);

				if (endptr == val_str || *endptr != '\0') {
					std::cerr
						<< "[ERROR] Invalid hex value for -offset: "
						<< val_str << std::endl;
					return -1;
				}
				flash_offset = static_cast<uint32_t>(val);
			} else {
				std::cerr
					<< "[ERROR] -offset requires a hex value (e.g., 0x1000)."
					<< std::endl;
				return -1;
			}
		} else if (arg == "-size") {
			if (i + 1 < argc) {
				char *endptr = nullptr;
				const char *val_str = argv[++i];
				unsigned long val =
					strtoul(val_str, &endptr, 16);

				if (endptr == val_str || *endptr != '\0') {
					std::cerr
						<< "[ERROR] Invalid hex value for -size: "
						<< val_str << std::endl;
					return -1;
				}
				fw_size_override = static_cast<uint32_t>(val);
			} else {
				std::cerr
					<< "[ERROR] -size requires a hex value."
					<< std::endl;
				return -1;
			}
		} else if (arg == "-dumpcmd") {
			dump_cmd_fw = true;
		} else {
			std::cerr << "[ERROR] Unknown argument: " << arg
				  << std::endl;
			return -1;
		}
	}

	std::cout
		<< "===== Parsed Arguments =====" << std::endl
		<< "Mode          : " << mode << std::endl
		<< "FW Path       : " << (fw_path.empty() ? "(empty)" : fw_path)
		<< std::endl
		<< "Key Path      : "
		<< (key_path.empty() ? "(empty)" : key_path) << std::endl
		<< "Algo          : " << algo_type << std::endl
		<< "Flash Offset  : 0x" << std::hex << std::uppercase
		<< std::setfill('0') << std::setw(8) << flash_offset
		<< std::endl
		<< "Flash Size    : 0x" << std::setw(8) << fw_size_override
		<< std::endl
		<< std::boolalpha << "Dump CMD FW   : " << dump_cmd_fw
		<< std::endl
		<< std::dec << "============================" << std::endl;

	if (mode == "-flashfw" && algo_type == "AESGCM" && key_path.empty()) {
		std::cerr
			<< "[ERROR] AESGCM requires a key file. "
			<< "Use -key <key.bin> or provide it as the 3rd argument."
			<< std::endl;
		return -1;
	}

	// --- USB connect ---
	UsbComm app_comm(0x18D1, 0x5401);
	if (app_comm.connect() == 0) {
		ExecuteGetVersion(app_comm);
		std::cout << "--- Switching to Bootloader Mode ---"
			  << std::endl;
		ExecuteBootLoader(app_comm);
		std::this_thread::sleep_for(std::chrono::milliseconds(800));
	}

	UsbComm boot_comm(0x1C7A, 0x1002);
	bool connected = false;
	auto start_time = std::chrono::steady_clock::now();
	std::cout << "Connecting to Bootloader..." << std::endl;
	while (std::chrono::steady_clock::now() - start_time <
	       std::chrono::seconds(5)) {
		if (boot_comm.connect() == 0) {
			connected = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	if (!connected) {
		std::cerr << "[ERROR] Connection timeout." << std::endl;
		return -1;
	}
	BootromComm brom(boot_comm);
	if (algo_type == "AESGCM") {
		brom.Wrapped_Algo = CRYPTO_AES_GCM;
	} else if (algo_type == "SHA256") {
		brom.Wrapped_Algo = CRYPTO_SHA_256;
	} else {
		std::cerr << "[ERROR] Unsupported algorithm: " << algo_type
			  << std::endl;
		return -1;
	}

	if (mode == "flashfw") {
		std::cout << "[INFO] Flash FW Mode" << std::endl;
		std::cout << "FW  : " << fw_path << std::endl;
		std::cout << "Key : " << key_path << std::endl;
		std::cout << "Offset : 0x" << std::hex << flash_offset
			  << std::dec << std::endl;
		std::cout << "Size : 0x" << std::hex << fw_size_override
			  << std::dec << std::endl;

		if (fw_path.empty()) {
			std::cerr << "[ERROR] Missing firmware path\n";
			return -1;
		}

		if (algo_type == "AESGCM" && key_path.empty()) {
			std::cerr << "[ERROR] AESGCM requires a key file\n";
			return -1;
		}

		// --- Load firmware ---
		std::ifstream infile(fw_path, std::ios::binary | std::ios::ate);
		if (!infile.is_open()) {
			std::cerr << "[ERROR] Cannot open firmware file: "
				  << fw_path << std::endl;
			return -1;
		}

		// --- Load AES Key ---
		if (algo_type == "AESGCM") {
			uint8_t local_aes_key[32] = { 0 };
			if (!load_aes_key(key_path.c_str(), local_aes_key)) {
				return -1;
			}
			brom.set_aes_key(local_aes_key);
		}

		// --- Dump RAW CMD ---
		if (dump_cmd_fw) {
			std::ostringstream oss;
			std::string fw_filename =
				fw_path.substr(fw_path.find_last_of("/\\") + 1);
			std::string algo_tag =
				(algo_type == "AESGCM") ? "aes" : "sha256";
			oss << fw_filename << "_" << algo_tag << "_cmd_"
			    << time(nullptr) << ".bin";
			raw_cmd_fw_file.open(oss.str(), std::ios::binary);
			if (!raw_cmd_fw_file.is_open()) {
				std::cerr
					<< "[ERROR] Cannot create dump file\n";
				return -1;
			}
			brom.set_raw_cmd_dump(&raw_cmd_fw_file);
		}

		// --- Disable WDT and Write Protect ---
		if (brom.disable_watchdog() != 0)
			return -1;
		if (brom.disable_wp() != 0)
			return -1;

		std::streamsize fw_size = infile.tellg();
		infile.seekg(0, std::ios::beg);

		if (fw_size <= 0) {
			std::cerr << "[ERROR] Firmware file is empty\n";
			return -1;
		}
		fw_size = (fw_size_override > 0) ? fw_size_override : fw_size;
		std::vector<uint8_t> firmware(fw_size);
		if (!infile.read(reinterpret_cast<char *>(firmware.data()),
				 fw_size)) {
			std::cerr << "[ERROR] Failed to read firmware file"
				  << std::endl;
			return -1;
		}
		infile.close();

		// --- Flash firmware in chunks ---
		std::cout << "--- Flashing Full Binary with Key ---"
			  << std::endl;
		const uint32_t chunk_size = 32 * 1024;

		size_t file_pos = flash_offset;
		uint32_t flash_addr = flash_offset;

		while (file_pos < fw_size) {
			uint32_t current_chunk = std::min(
				chunk_size,
				static_cast<uint32_t>(fw_size - file_pos));

			if (brom.write_flash(
				    flash_addr,
				    std::vector<uint8_t>(
					    firmware.begin() + file_pos,
					    firmware.begin() + file_pos +
						    current_chunk)) != 0) {
				std::cerr
					<< "[HALT] Flash Write failed at address 0x"
					<< std::hex << flash_addr << std::dec
					<< std::endl;
				return -1;
			}

			file_pos += current_chunk;
			flash_addr += current_chunk;

			std::cout << "\rWriting Progress: " << file_pos << " / "
				  << fw_size << " (" << std::fixed
				  << std::setprecision(1)
				  << (static_cast<float>(file_pos) / fw_size *
				      100.0f)
				  << "%)" << std::flush;
			std::this_thread::sleep_for(
				std::chrono::milliseconds(10));
		}

		std::cout << "\n[OK] Flash Full Binary Write successful"
			  << std::endl;
		brom.system_reset();
	} else if (mode == "flashcmd") {
		if (argc < 3) {
			std::cerr << "[ERROR] -flashcmd requires fw.bin\n";
			return -1;
		}
		fw_path = argv[2];
		std::ifstream infile(fw_path, std::ios::binary | std::ios::ate);
		if (!infile.is_open()) {
			std::cerr << "[ERROR] Cannot open firmware file: "
				  << fw_path << std::endl;
			return -1;
		}
		std::streamsize fw_size = infile.tellg();
		infile.seekg(0, std::ios::beg);
		std::vector<uint8_t> firmware(fw_size);
		if (!infile.read(reinterpret_cast<char *>(firmware.data()),
				 fw_size)) {
			std::cerr << "[ERROR] Failed to read firmware file"
				  << std::endl;
			return -1;
		}
		infile.close();

		std::cout << "--- Flash RAW CMD Binary Flashing ---"
			  << std::endl;
		size_t done = 0;
		while (done < firmware.size()) {
			if (done + 34 > firmware.size()) {
				std::cerr
					<< "\n[ERROR] Remaining data too small for a valid packet header."
					<< std::endl;
				break;
			}

			uint8_t header1 = firmware[done];
			uint8_t secure_algo = firmware[done + 1];

			if (header1 != EgisBootrom::CMD_SECURE_WRAPPED) {
				std::cerr
					<< "[ERROR] Invalid header at offset 0x"
					<< std::hex << done << std::endl;
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
				std::cerr << "[ERROR] Unknown Secure_Algo 0x"
					  << std::hex
					  << static_cast<int>(secure_algo)
					  << " at offset 0x" << done
					  << std::endl;
				return -1;
			}

			uint32_t wrapped_cmd_size = 0;
			std::memcpy(&wrapped_cmd_size, &firmware[done + 2],
				    sizeof(uint32_t));

			size_t total_packet_size = 2 + 4;
			if (secure_algo == 0x02) {
				total_packet_size += 12 + 16 + 4;
			} else {
				total_packet_size += 32;
			}
			total_packet_size += wrapped_cmd_size;

			if (done + total_packet_size > firmware.size()) {
				std::cerr
					<< "\n[ERROR] Packet truncated at offset 0x"
					<< std::hex << done << ". Expected "
					<< total_packet_size << " bytes."
					<< std::endl;
				return -1;
			}

			std::vector<uint8_t> buf(firmware.begin() + done,
						 firmware.begin() + done +
							 total_packet_size);
			int ret = boot_comm.bulk_write(0x02, buf,
						       TRANSFER_TIMEOUT_MS);
			if (ret < 0) {
				std::cerr
					<< "\n[ERROR] USB bulk_write failed at 0x"
					<< std::hex << done << ", ret=" << ret
					<< std::endl;
				return -1;
			}

			std::vector<uint8_t> resp(128, 0);
			int transferred = boot_comm.bulk_read(
				0x81, resp, TRANSFER_TIMEOUT_MS);
			if (transferred <= 0) {
				std::cerr
					<< "\n[ERROR] Device no response (Timeout) at 0x"
					<< std::hex << done << std::endl;
				return -1;
			}

			done += total_packet_size;
			std::cout << "\rFlashing Progress: " << done << " / "
				  << firmware.size() << " (" << std::fixed
				  << std::setprecision(1)
				  << (static_cast<float>(done) /
				      firmware.size() * 100.0f)
				  << "%)" << std::flush;
		}
		std::cout << "\n[OK] RAW CMD Flashing Finished" << std::endl;
		brom.system_reset();
	}

	return 0;
}