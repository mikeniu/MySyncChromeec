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

using namespace std;

std::ofstream raw_cmd_fw_file;

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
	cout << "\n--- Executing GET_VERSION ---\n";
	vector<uint8_t> payload = ec_comm.send_command(
		static_cast<uint16_t>(ECCommandId::EC_CMD_GET_VERSION), 1, {});
	cout << "Command SUCCESS.\nReceived payload length: " << payload.size()
	     << " bytes.\n";

	GetVersionResponse response;
	size_t copy_size = std::min(payload.size(), sizeof(GetVersionResponse));
	memcpy(&response, payload.data(), copy_size);

	response.ro_ver[sizeof(response.ro_ver) - 1] = '\0';
	response.rw_ver[sizeof(response.rw_ver) - 1] = '\0';
	cout << "\n--- Parsed Version Info ---\n";
	cout << "RO Version: " << response.ro_ver << "\n";
	cout << "RW Version: " << response.rw_ver << "\n";
	return 0;
}

int ExecuteBootLoader(UsbComm &comm)
{
	ECComm ec_comm(comm);
	cout << "\n--- Executing Enter Bootloader ---\n";
	ec_comm.send_command(
		static_cast<uint16_t>(ECCommandId::EC_CMD_ENTER_BOOTLOADER), 0,
		{});
	return 0;
}

int main(int argc, char *argv[])
{
	// --- 1. Help Message ---
	if (argc < 2 || string(argv[1]) == "-h" ||
	    string(argv[1]) == "--help") {
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

	string mode;
	string fw_path;
	string key_path;
	string algo_type = "SHA256";
	uint32_t flash_offset = 0x00001000;
	uint32_t fw_size_override = 0x0;
	bool dump_cmd_fw = false;

	for (int i = 1; i < argc; i++) {
		string arg = argv[i];

		if (arg == "-flashfw" && i + 1 < argc) {
			mode = "flashfw";
			fw_path = argv[++i];
		} else if (arg == "-flashcmd" && i + 1 < argc) {
			mode = "flashcmd";
			fw_path = argv[++i];
		} else if (arg == "-key" && i + 1 < argc) {
			key_path = argv[++i];
		} else if (arg == "-algo" && i + 1 < argc) {
			algo_type = argv[++i];
		} else if (arg == "-offset" && i + 1 < argc) {
			flash_offset = std::stoul(argv[++i], nullptr, 16);
		} else if (arg == "-size" && i + 1 < argc) {
			fw_size_override = std::stoul(argv[++i], nullptr, 16);
		} else if (arg == "-dumpcmd") {
			dump_cmd_fw = true;
		} else {
			std::cerr << "Unknown argument: " << arg << std::endl;
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
		cerr << "[ERROR] AESGCM requires a key file. "
		     << "Use -key <key.bin> or provide it as the 3rd argument."
		     << endl;
		return -1;
	}

	// --- USB connect ---
	UsbComm app_comm(0x18D1, 0x5401);
	if (app_comm.connect() == 0) {
		ExecuteGetVersion(app_comm);
		cout << "--- Switching to Bootloader Mode ---" << endl;
		ExecuteBootLoader(app_comm);
		std::this_thread::sleep_for(std::chrono::milliseconds(800));
	}

	UsbComm boot_comm(0x1C7A, 0x1002);
	bool connected = false;
	auto start_time = chrono::steady_clock::now();
	cout << "Connecting to Bootloader..." << endl;
	while (chrono::steady_clock::now() - start_time < chrono::seconds(5)) {
		if (boot_comm.connect() == 0) {
			connected = true;
			break;
		}
		this_thread::sleep_for(chrono::milliseconds(200));
	}
	if (!connected) {
		cerr << "[ERROR] Connection timeout." << endl;
		return -1;
	}
	BootromComm brom(boot_comm);
	if (algo_type == "AESGCM") {
		brom.Wrapped_Algo = CRYPTO_AES_GCM;
	} else if (algo_type == "SHA256") {
		brom.Wrapped_Algo = CRYPTO_SHA_256;
	} else {
		cerr << "[ERROR] Unsupported algorithm: " << algo_type << endl;
		return -1;
	}

	if (mode == "flashfw") {
		cout << "[INFO] Flash FW Mode" << endl;
		cout << "FW  : " << fw_path << endl;
		cout << "Key : " << key_path << endl;
		cout << "Offset : 0x" << hex << flash_offset << dec << endl;
		cout << "Size : 0x" << hex << fw_size_override << dec << endl;

		if (fw_path.empty()) {
			cerr << "[ERROR] Missing firmware path\n";
			return -1;
		}

		if (algo_type == "AESGCM" && key_path.empty()) {
			cerr << "[ERROR] AESGCM requires a key file\n";
			return -1;
		}

		// --- Load firmware ---
		ifstream infile(fw_path, ios::binary | ios::ate);
		if (!infile.is_open()) {
			cerr << "[ERROR] Cannot open firmware file: " << fw_path
			     << endl;
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
			ostringstream oss;
			string fw_filename =
				fw_path.substr(fw_path.find_last_of("/\\") + 1);
			string algo_tag = (algo_type == "AESGCM") ? "aes" :
								    "sha256";
			oss << fw_filename << "_" << algo_tag << "_cmd_"
			    << time(nullptr) << ".bin";
			raw_cmd_fw_file.open(oss.str(), ios::binary);
			if (!raw_cmd_fw_file.is_open()) {
				cerr << "[ERROR] Cannot create dump file\n";
				return -1;
			}
			brom.set_raw_cmd_dump(&raw_cmd_fw_file);
		}

		// --- Disable WDT and Write Protect ---
		if (brom.disable_watchdog() != 0)
			return -1;
		if (brom.disable_wp() != 0)
			return -1;

		streamsize fw_size = infile.tellg();
		infile.seekg(0, ios::beg);

		if (fw_size <= 0) {
			cerr << "[ERROR] Firmware file is empty\n";
			return -1;
		}
		fw_size = (fw_size_override > 0) ? fw_size_override : fw_size;
		vector<uint8_t> firmware(fw_size);
		if (!infile.read(reinterpret_cast<char *>(firmware.data()),
				 fw_size)) {
			cerr << "[ERROR] Failed to read firmware file" << endl;
			return -1;
		}
		infile.close();

		// --- Flash firmware in chunks ---
		cout << "--- Flashing Full Binary with Key ---" << endl;
		const uint32_t chunk_size = 32 * 1024;

		size_t file_pos = flash_offset; // firmware buffer index
		uint32_t flash_addr = flash_offset;

		while (file_pos < fw_size) {
			uint32_t current_chunk = std::min(
				chunk_size, uint32_t(fw_size - file_pos));

			if (brom.write_flash(
				    flash_addr,
				    vector<uint8_t>(
					    firmware.begin() + file_pos,
					    firmware.begin() + file_pos +
						    current_chunk)) != 0) {
				cerr << "[HALT] Flash Write failed at address 0x"
				     << hex << flash_addr << dec << endl;
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
			this_thread::sleep_for(chrono::milliseconds(10));
		}

		cout << "\n[OK] Flash Full Binary Write successful" << endl;
		brom.system_reset();
	} else if (mode == "flashcmd") {
		if (argc < 3) {
			cerr << "[ERROR] -flashcmd requires fw.bin\n";
			return -1;
		}
		fw_path = argv[2];
		ifstream infile(fw_path, ios::binary | ios::ate);
		if (!infile.is_open()) {
			cerr << "[ERROR] Cannot open firmware file: " << fw_path
			     << endl;
			return -1;
		}
		streamsize fw_size = infile.tellg();
		infile.seekg(0, ios::beg);
		vector<uint8_t> firmware(fw_size);
		if (!infile.read(reinterpret_cast<char *>(firmware.data()),
				 fw_size)) {
			cerr << "[ERROR] Failed to read firmware file" << endl;
			return -1;
		}
		infile.close();

		cout << "--- Flash RAW CMD Binary Flashing ---" << endl;
		size_t done = 0;
		while (done < firmware.size()) {
			if (done + 34 > firmware.size()) {
				cerr << "\n[ERROR] Remaining data too small for a valid packet header."
				     << endl;
				break;
			}

			uint8_t header1 = firmware[done];
			uint8_t secure_algo = firmware[done + 1];

			if (header1 != 0xF4) {
				cerr << "[ERROR] Invalid header at offset 0x"
				     << hex << done << endl;
				return -1;
			}

			string algo_name;
			switch (secure_algo) {
			case 0x00:
				algo_name = "SHA256";
				break;
			case 0x01:
				algo_name = "HMAC-SHA256";
				break;
			case 0x02:
				algo_name = "AESGCM";
				break;
			default:
				cerr << "[ERROR] Unknown Secure_Algo 0x" << hex
				     << static_cast<int>(secure_algo)
				     << " at offset 0x" << done << endl;
				return -1;
			}

			// 3. calculate wrapped cmd size
			uint32_t wrapped_cmd_size = 0;
			memcpy(&wrapped_cmd_size, &firmware[done + 2],
			       sizeof(uint32_t));

			size_t total_packet_size = 2 + 4; // header + wrapped
							  // size field
			if (secure_algo == 0x02) { // AESGCM
				total_packet_size += 12 + 16 + 4; // IV + TAG +
								  // AAD
			} else { // SHA256 // HMAC
				total_packet_size += 32; // hash
			}
			total_packet_size += wrapped_cmd_size; // payload

			// 5. check data size equal firmware size
			if (done + total_packet_size > firmware.size()) {
				cerr << "\n[ERROR] Packet truncated at offset 0x"
				     << hex << done << ". Expected "
				     << total_packet_size << " bytes." << endl;
				return -1;
			}

			// 6. send USB packet
			vector<uint8_t> buf(firmware.begin() + done,
					    firmware.begin() + done +
						    total_packet_size);
			int ret = boot_comm.bulk_write(0x02, buf,
						       TRANSFER_TIMEOUT_MS);
			if (ret < 0) {
				cerr << "\n[ERROR] USB bulk_write failed at 0x"
				     << hex << done << ", ret=" << ret << endl;
				return -1;
			}

			// 6. read USB packet
			vector<uint8_t> resp(128, 0);
			int transferred = boot_comm.bulk_read(
				0x81, resp, TRANSFER_TIMEOUT_MS);
			if (transferred <= 0) {
				cerr << "\n[ERROR] Device no response (Timeout) at 0x"
				     << hex << done << endl;
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
		cout << "\n[OK] RAW CMD Flashing Finished" << endl;
		brom.system_reset();
	}

	return 0;
}
