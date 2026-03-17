/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <getopt.h>

// C system files
#include <cstdlib>
#include <cstring>

// C++ system files
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>

// Other libraries' .h files
#include <openssl/aes.h>
#include <openssl/crypto.h>

// Project headers
#include "bootrom.h"
#include "ec_comm.h"
#include "ec_protocol_defs.h"
#include "usb_comm.h"

using namespace std::chrono_literals;

namespace {
constexpr uint16_t kVidGoogle = 0x18D1;
constexpr uint16_t kPidEgisApp = 0x5401;
constexpr uint16_t kVidEgis = 0x1C7A;
constexpr uint16_t kPidEgisBootloader = 0x1002;

template <std::integral T>
std::expected<T, std::errc> ParseHex(std::string_view strv) {
  if (strv.empty()) return std::unexpected(std::errc::invalid_argument);

  // Strip standard hex prefixes
  if (strv.starts_with("0x") || strv.starts_with("0X")) {
    strv.remove_prefix(2);
  }

  if (strv.empty()) return std::unexpected(std::errc::invalid_argument);

  T val;
  auto [ptr, ec] =
      std::from_chars(strv.data(), strv.data() + strv.size(), val, 16);

  // Catch overflow or formatting errors natively reported by from_chars
  if (ec != std::errc{}) {
    return std::unexpected(ec);
  }

  // Catch trailing garbage (e.g., "1000xyz")
  if (ptr != strv.data() + strv.size()) {
    return std::unexpected(std::errc::invalid_argument);
  }

  return val;
}

std::optional<std::vector<uint8_t> > ReadBinaryFile(std::string_view path) {
  std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
  if (!file) return std::nullopt;

  std::streamsize size = file.tellg();
  if (size <= 0) return std::nullopt;

  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
    return std::nullopt;
  }
  return buffer;
}

bool LoadAesKey(std::string_view key_file_path,
                std::span<uint8_t, 32> key_buffer) {
  auto key_opt = ReadBinaryFile(key_file_path);
  if (!key_opt) {
    std::println(stderr, "Failed to open or read key file: {}", key_file_path);
    return false;
  }

  if (key_opt->size() != key_buffer.size()) {
    std::println(stderr,
                 "Key file size incorrect, expected {} bytes but got {}.",
                 key_buffer.size(), key_opt->size());
    OPENSSL_cleanse(key_opt->data(), key_opt->size());
    return false;
  }

  std::copy(key_opt->begin(), key_opt->end(), key_buffer.begin());
  OPENSSL_cleanse(key_opt->data(), key_opt->size());
  return true;
}

int ExecuteGetVersion(UsbComm &comm) {
  EcComm ec_comm(comm);
  std::println("\n--- Executing GET_VERSION ---");
  auto resp = ec_comm.SendCommand(
      static_cast<uint16_t>(EcCommandId::kEcCmdGetVersion), 1);
  if (!resp.has_value()) {
    std::println(stderr, "[ERROR] GET_VERSION command failed or timed out.");
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

int ExecuteBootLoader(UsbComm &comm) {
  EcComm ec_comm(comm);
  std::println("\n--- Executing Enter Bootloader ---");
  auto resp = ec_comm.SendCommand(
      static_cast<uint16_t>(EcCommandId::kEcCmdEnterBootloader), 0);
  if (!resp.has_value()) {
    std::println(stderr, "[ERROR] Failed to send Enter Bootloader command.");
    return -1;
  }
  return 0;
}

void PrintHelp(const char *prog_name) {
  std::print(
      "\n=== ET171 Flash Tool ===\n\n"
      "Usage:\n"
      "  {0} <mode> [options]\n\n"
      "Modes (mutually exclusive):\n"
      "  --flashfw <fw.bin>      Flash firmware with real-time encryption.\n"
      "  --flashcmd <cmd.bin>    Flash a pre-processed raw command binary.\n"
      "Options:\n"
      "  --algo <type>           Encryption algorithm: SHA256 (default) or "
      "AESGCM.\n"
      "  --key <key.bin>         Path to the AES-GCM key file (required for "
      "AESGCM).\n"
      "  --offset <addr>         Flash start offset in hex (Default: "
      "0x00001000).\n"
      "  --size <bytes>          Size to flash in hex (Default: entire "
      "firmware file).\n"
      "  --dumpcmd               Save processed commands to a file (for "
      "--flashfw mode).\n"
      "  --help, -h              Show this help message.\n\n"
      "Examples:\n"
      "  {0} --flashfw fw.bin --algo AESGCM --key key.bin\n"
      "  {0} --flashcmd cmd.bin\n",
      prog_name);
}

}  // namespace

int main(int argc, char *argv[]) {
  // 1. Initialize the global libusb context once
  libusb_context *raw_ctx = nullptr;
  int rc = libusb_init(&raw_ctx);
  if (rc != 0) {
    std::println(stderr, "[ERROR] Failed to initialize libusb: {}",
                 libusb_error_name(rc));
    return -1;
  }

  // 2. Wrap it in a smart pointer for guaranteed cleanup on all exit
  // paths
  std::unique_ptr<libusb_context, decltype(&libusb_exit)> usb_ctx(raw_ctx,
                                                                  &libusb_exit);
  std::string mode;
  std::string fw_path;
  std::string key_path;
  EgisCryptoType algo_type = EgisCryptoType::kSha256;
  uint32_t flash_offset = 0x00001000;
  uint32_t fw_size_override = 0x0;
  bool dump_cmd_fw = false;
  std::ofstream raw_cmd_fw_file;

  static struct option long_options[] = {
      {"flashfw", required_argument, nullptr, 'f'},
      {"flashcmd", required_argument, nullptr, 'c'},
      {"algo", required_argument, nullptr, 'a'},
      {"key", required_argument, nullptr, 'k'},
      {"offset", required_argument, nullptr, 'o'},
      {"size", required_argument, nullptr, 's'},
      {"dumpcmd", no_argument, nullptr, 'd'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0}};

  int opt;
  // The colon after a letter indicates it requires an argument.
  // Using getopt_long_only allows long options with a single '-' for
  // backward compatibility.
  while ((opt = getopt_long_only(argc, argv, "f:c:a:k:o:s:dh", long_options,
                                 nullptr)) != -1) {
    switch (opt) {
      case 'f':
        mode = "flashfw";
        fw_path = optarg;
        break;
      case 'c':
        mode = "flashcmd";
        fw_path = optarg;
        break;
      case 'a':
        if (std::strcmp(optarg, "SHA256") == 0) {
          algo_type = EgisCryptoType::kSha256;
        } else if (std::strcmp(optarg, "AESGCM") == 0) {
          algo_type = EgisCryptoType::kAesGcm;
        } else {
          std::println(stderr,
                       "[ERROR] Invalid --algo: {}. Must be SHA256 or AESGCM.",
                       optarg);
          return -1;
        }
        break;
      case 'k':
        key_path = optarg;
        break;
      case 'o':
      case 's': {
        auto parsed_val = ParseHex<uint32_t>(optarg);
        if (!parsed_val) {
          if (parsed_val.error() == std::errc::result_out_of_range) {
            std::println(stderr,
                         "[ERROR] Hex value '{}' is too large (exceeds 32-bit "
                         "limit).",
                         optarg);
          } else {
            std::println(stderr,
                         "[ERROR] Invalid characters found in hex argument: {}",
                         optarg);
          }
          return -1;
        }
        if (opt == 'o') flash_offset = parsed_val.value();
        if (opt == 's') fw_size_override = parsed_val.value();
        break;
      }
      case 'd':
        dump_cmd_fw = true;
        break;
      case 'h':
        PrintHelp(argv[0]);
        return 0;
      case '?':  // '?' is returned for unknown option or missing arg.
        PrintHelp(argv[0]);
        return -1;
      default:
        return -1;
    }
  }

  if (optind < argc) {
    std::println(stderr, "[ERROR] Unexpected positional argument: {}",
                 argv[optind]);
    PrintHelp(argv[0]);
    return -1;
  }

  if (mode.empty()) {
    std::println(stderr,
                 "[ERROR] You must specify a mode (--flashfw, --flashcmd");
    PrintHelp(argv[0]);
    return -1;
  }

  std::println("===== Parsed Arguments =====");
  std::println("Mode          : {}", mode);
  std::println("FW Path       : {}", fw_path.empty() ? "(empty)" : fw_path);
  std::println("Key Path      : {}", key_path.empty() ? "(empty)" : key_path);
  std::println("Algo          : {}",
               (algo_type == EgisCryptoType::kAesGcm) ? "AESGCM" : "SHA256");
  std::println("Flash Offset  : 0x{:08X}", flash_offset);
  std::println("Flash Size    : 0x{:08X}", fw_size_override);
  std::println("Dump CMD FW   : {}", dump_cmd_fw);
  std::println("============================");

  if (mode == "flashfw" && algo_type == EgisCryptoType::kAesGcm &&
      key_path.empty()) {
    std::println(stderr,
                 "[ERROR] AESGCM requires a key file. "
                 "Use -key <key.bin> or provide it as the 3rd argument.");
    return -1;
  }

  // --- USB connect ---
  UsbComm app_comm(usb_ctx.get(), kVidGoogle, kPidEgisApp);
  if (app_comm.Connect() == 0) {
    ExecuteGetVersion(app_comm);
    std::println("--- Switching to Bootloader Mode ---");
    ExecuteBootLoader(app_comm);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
  }

  UsbComm boot_comm(usb_ctx.get(), kVidEgis, kPidEgisBootloader);
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
  brom.SetWrappedAlgo(algo_type);

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

    if (algo_type == EgisCryptoType::kAesGcm && key_path.empty()) {
      std::println(stderr, "[ERROR] AESGCM requires a key file");
      return -1;
    }

    // --- Load firmware ---
    auto firmware_opt = ReadBinaryFile(fw_path);
    if (!firmware_opt) {
      std::println(stderr, "[ERROR] Cannot open or read file: {}", fw_path);
      return -1;
    }
    std::vector<uint8_t> &firmware = firmware_opt.value();

    // --- Load AES Key ---
    if (algo_type == EgisCryptoType::kAesGcm) {
      std::array<uint8_t, 32> local_aes_key{};
      if (!LoadAesKey(key_path, local_aes_key)) {
        return -1;
      }
      brom.SetAesKey(local_aes_key.data());
      OPENSSL_cleanse(local_aes_key.data(), local_aes_key.size());
    }

    // --- Dump RAW CMD ---
    if (dump_cmd_fw) {
      std::ostringstream oss;
      std::string fw_filename = fw_path.substr(fw_path.find_last_of("/\\") + 1);
      std::string algo_tag =
          (algo_type == EgisCryptoType::kAesGcm) ? "aes" : "sha256";
      oss << fw_filename << "_" << algo_tag << "_cmd_" << std::time(nullptr)
          << ".bin";
      raw_cmd_fw_file.open(oss.str(), std::ios::binary);
      if (!raw_cmd_fw_file.is_open()) {
        std::println(stderr, "[ERROR] Cannot create dump file");
        return -1;
      }
      brom.SetRawCmdDump(&raw_cmd_fw_file);
    }

    // --- Disable WDT and Write Protect ---
    if (brom.DisableWatchdog() != 0) return -1;
    if (brom.DisableWp() != 0) return -1;

    // --- Flash firmware in chunks ---
    std::println("--- Flashing Full Binary with Key ---");

    if (flash_offset >= firmware.size()) {
      std::println(stderr,
                   "[ERROR] flash_offset (0x{:X}) is at or beyond firmware "
                   "size (0x{:X}).",
                   flash_offset, firmware.size());
      return -1;
    }

    // If size is not specified, flash from offset to the end of the file.
    size_t size_to_flash = firmware.size() - flash_offset;
    if (fw_size_override > 0) {
      size_to_flash = fw_size_override;
    }

    // Ensure we don't read past the end of the firmware file.
    if (flash_offset + size_to_flash > firmware.size()) {
      std::println(stderr,
                   "[ERROR] The specified range (offset 0x{:X} + size 0x{:X}) "
                   "exceeds the file size (0x{:X}).",
                   flash_offset, size_to_flash, firmware.size());
      return -1;
    }

    auto fw_payload =
        std::span<const uint8_t>(firmware).subspan(flash_offset, size_to_flash);
    const size_t chunk_size = 32 * 1024;
    uint32_t current_addr = flash_offset;
    size_t done = 0;

    for (size_t i = 0; i < fw_payload.size(); i += chunk_size) {
      auto chunk =
          fw_payload.subspan(i, std::min(chunk_size, fw_payload.size() - i));
      if (brom.WriteFlash(current_addr, chunk) != 0) {
        std::println(stderr, "\n[HALT] Flash Write failed at address 0x{:X}",
                     current_addr);
        return -1;
      }
      current_addr += chunk.size();
      done += chunk.size();
      std::print("\rWriting Progress: {} / {} ({:.1f}%)", done,
                 fw_payload.size(),
                 (static_cast<float>(done) / fw_payload.size() * 100.0f));
      std::fflush(stdout);
    }

    std::println("\n[OK] Flash Full Binary Write successful");
    brom.SystemReset();
  } else if (mode == "flashcmd") {
    if (fw_path.empty()) {
      std::println(stderr, "[ERROR] -flashcmd requires a command file path.");
      return -1;
    }
    auto firmware_opt = ReadBinaryFile(fw_path);
    if (!firmware_opt) {
      std::println(stderr, "[ERROR] Cannot open or read file: {}", fw_path);
      return -1;
    }
    std::vector<uint8_t> &firmware = firmware_opt.value();

    std::println("--- Flash RAW CMD Binary Flashing ---");
    size_t done = 0;
    while (done < firmware.size()) {
      if (done + 2 > firmware.size()) {
        std::println(stderr,
                     "\n[ERROR] Remaining data too small to parse command.");
        break;
      }
      uint8_t header1 = firmware[done];
      if (header1 == EgisBootrom::kCmdSystemReset) {
        std::span<const uint8_t> buf(&firmware[done], 2);
        int ret = boot_comm.BulkWrite(BootromComm::kBootromEpOutBulk, buf,
                                      kTransferTimeout);
        if (ret != static_cast<int>(buf.size())) {
          std::println(stderr,
                       "\n[WARN] USB bulk_write for System Reset may have "
                       "failed (wrote {}/{} bytes)",
                       ret, buf.size());
        }
        done += 2;
        continue;
      }
      if (header1 != EgisBootrom::kCmdSecureWrapped) {
        std::println(stderr, "\n[ERROR] Invalid header 0x{:X} at offset 0x{:X}",
                     header1, done);
        return -1;
      }
      EgisCryptoType secure_algo =
          static_cast<EgisCryptoType>(firmware[done + 1]);
      size_t header_size = 0;
      std::string algo_name;
      switch (secure_algo) {
        case EgisCryptoType::kSha256:
          algo_name = "SHA256";
          header_size = BootromComm::kScHeaderSizeSha;  // 38
          break;
        case EgisCryptoType::kHmacSha256:
          std::println(stderr,
                       "\n[ERROR] CRYPTO_HMAC_SHA256 not supported yet.");
          return -1;
        case EgisCryptoType::kAesGcm:
          algo_name = "AESGCM";
          header_size = BootromComm::kScHeaderSizeAes;  // 34
          break;
        default:
          std::println(
              stderr, "\n[ERROR] Unknown Secure_Algo 0x{:02X} at offset 0x{:X}",
              static_cast<uint8_t>(secure_algo), done);
          return -1;
      }
      if (done + header_size > firmware.size()) {
        std::println(stderr,
                     "\n[ERROR] Data truncated: missing full {} header.",
                     algo_name);
        break;
      }

      uint32_t wrapped_cmd_size = 0;
      std::memcpy(&wrapped_cmd_size, &firmware[done + 2], sizeof(uint32_t));

      size_t total_packet_size = header_size + wrapped_cmd_size;

      if (done + total_packet_size > firmware.size()) {
        std::println(
            stderr,
            "\n[ERROR] Packet truncated at offset 0x{:X}. Expected {} bytes.",
            done, total_packet_size);
        return -1;
      }

      std::span<const uint8_t> buf(&firmware[done], total_packet_size);
      int ret = boot_comm.BulkWrite(BootromComm::kBootromEpOutBulk, buf,
                                    kTransferTimeout);
      if (ret != static_cast<int>(buf.size())) {
        std::println(
            stderr,
            "\n[ERROR] USB bulk_write failed at 0x{:X} (wrote {}/{} bytes)",
            done, ret, buf.size());
        return -1;
      }

      std::vector<uint8_t> resp(128, 0);
      int transferred = boot_comm.BulkRead(BootromComm::kBootromEpInBulk, resp,
                                           kTransferTimeout);
      if (transferred <= 0) {
        std::println(stderr, "\n[ERROR] Device no response (Timeout) at 0x{:X}",
                     done);
        return -1;
      }

      done += total_packet_size;
      std::print("\rFlashing Progress: {} / {} ({:.1f}%)", done,
                 firmware.size(),
                 (static_cast<float>(done) / firmware.size() * 100.0f));
      std::fflush(stdout);
    }
    std::println("\n[OK] RAW CMD Flashing Finished");
    brom.SystemReset();
  }
  return 0;
}
