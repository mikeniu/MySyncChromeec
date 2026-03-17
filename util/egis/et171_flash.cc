/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <getopt.h>

// C system files
#include <cstdio>
#include <cstdlib>
#include <cstring>

// C++ system files
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
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

// Project headers
#include "bootrom.h"
#include "crypto_util.h"
#include "ec_comm.h"
#include "ec_protocol_defs.h"
#include "egis_util.h"
#include "usb_comm.h"

namespace egis {

using namespace std::chrono_literals;

constexpr uint16_t kVidGoogle = 0x18D1;
constexpr uint16_t kPidEgisApp = 0x5401;
constexpr uint16_t kVidEgis = 0x1C7A;
constexpr uint16_t kPidEgisBootloader = 0x1002;

constexpr uint8_t kAppEpOut = 0x01;
constexpr uint8_t kAppEpIn = 0x81;
constexpr uint8_t kBootloaderEpOut = 0x02;
constexpr uint8_t kBootloaderEpIn = 0x81;

enum class AppError {
  kInvalidArgs,
  kFileIoError,
  kCryptoError,
  kBootromError,
  kUsbError,
  kParseError,
};

enum class AppMode {
  kNone,
  kFlashFw,
  kFlashCmd,
};

struct AppConfig {
  AppMode mode = AppMode::kNone;
  std::string fw_path;
  std::string key_path;
  EgisCryptoType algo_type = EgisCryptoType::kSha256;
  uint32_t flash_offset = 0x00001000;
  uint32_t fw_size_override = 0;
  bool dump_cmd_fw = false;
};

namespace {
constexpr auto kBootloaderTransitionDelay = std::chrono::milliseconds(800);
constexpr auto kBootloaderConnectTimeout = std::chrono::seconds(5);
constexpr auto kBootloaderConnectPollInterval = std::chrono::milliseconds(200);

enum class ParseError {
  kDataTooSmall,
  kInvalidHeader,
  kUnsupportedCrypto,
  kTruncatedHeader,
  kInvalidPayloadSize,
};

constexpr std::string_view ToString(ParseError err) {
  switch (err) {
    case ParseError::kDataTooSmall:
      return "Data too small to parse header";
    case ParseError::kInvalidHeader:
      return "Invalid command header";
    case ParseError::kUnsupportedCrypto:
      return "Unsupported crypto algorithm";
    case ParseError::kTruncatedHeader:
      return "Data truncated before full header could be read";
    case ParseError::kInvalidPayloadSize:
      return "Failed to read payload size";
    default:
      return "Unknown parsing error";
  }
}

std::expected<size_t, ParseError> GetNextPacketSize(
    std::span<const uint8_t> fw) {
  if (fw.size() < 2) return std::unexpected(ParseError::kDataTooSmall);

  if (fw[0] == EgisBootrom::kCmdSystemReset) {
    return 2;  // System Reset is a fixed 2-byte packet
  }

  if (fw[0] != EgisBootrom::kCmdSecureWrapped) {
    return std::unexpected(ParseError::kInvalidHeader);
  }

  size_t header_size = 0;
  EgisCryptoType algo = static_cast<EgisCryptoType>(fw[1]);

  if (algo == EgisCryptoType::kSha256) {
    header_size = BootromComm::kScHeaderSizeSha;
  } else if (algo == EgisCryptoType::kAesGcm) {
    header_size = BootromComm::kScHeaderSizeAes;
  } else {
    return std::unexpected(ParseError::kUnsupportedCrypto);
  }

  if (fw.size() < header_size) {
    return std::unexpected(ParseError::kTruncatedHeader);
  }

  // Safely extract the wrapped size using the shared template function
  auto wrapped_size = ReadLittleEndian<uint32_t>(
      fw.subspan(offsetof(BootromComm::SecureCommandShaHeader, payload_len),
                 sizeof(BootromComm::SecureCommandShaHeader::payload_len)));
  if (!wrapped_size) {
    return std::unexpected(ParseError::kInvalidPayloadSize);
  }

  return header_size + wrapped_size.value();
}

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

std::expected<std::vector<uint8_t>, std::errc> ReadBinaryFile(
    std::string_view path) {
  // Reset errno before the call that might set it.
  errno = 0;
  std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
  if (!file) {
    return std::unexpected(errno ? static_cast<std::errc>(errno)
                                 : std::errc::io_error);
  }

  std::streamsize size = file.tellg();
  if (size <= 0) return std::unexpected(std::errc::invalid_argument);

  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
    return std::unexpected(std::errc::io_error);
  }
  return buffer;
}

std::expected<void, std::errc> LoadAesKey(std::string_view key_file_path,
                                          std::span<uint8_t, 32> key_buffer) {
  auto key_res = ReadBinaryFile(key_file_path);
  if (!key_res) return std::unexpected(key_res.error());

  auto &key_opt = key_res.value();
  if (key_opt.size() != key_buffer.size()) {
    crypto::Cleanse(key_opt);
    return std::unexpected(std::errc::invalid_argument);
  }

  std::copy(key_opt.begin(), key_opt.end(), key_buffer.begin());
  crypto::Cleanse(key_opt);
  return {};
}

std::expected<void, EcError> ExecuteGetVersion(UsbComm &comm) {
  EcComm ec_comm(comm);
  std::println("\n--- Executing GET_VERSION ---");
  auto resp = ec_comm.SendCommand(
      static_cast<uint16_t>(EcCommandId::kEcCmdGetVersion), 1);
  if (!resp) {
    return std::unexpected(resp.error());
  }
  auto payload = resp.value();
  std::println("Command SUCCESS.\nReceived payload length: {} bytes.",
               payload.size());

  GetVersionResponse response = {};
  size_t copy_size = std::min(payload.size(), sizeof(GetVersionResponse));
  std::memcpy(&response, payload.data(), copy_size);

  // Correct endianness for multi-byte integer field
  response.current_image = FromLittleEndian(response.current_image);

  response.ro_ver[sizeof(response.ro_ver) - 1] = '\0';
  response.rw_ver[sizeof(response.rw_ver) - 1] = '\0';
  std::println("\n--- Parsed Version Info ---");
  std::println("RO Version: {}", response.ro_ver);
  std::println("RW Version: {}", response.rw_ver);
  return {};
}

std::expected<void, EcError> ExecuteBootLoader(UsbComm &comm) {
  EcComm ec_comm(comm);
  std::println("\n--- Executing Enter Bootloader ---");
  auto resp = ec_comm.SendCommand(
      static_cast<uint16_t>(EcCommandId::kEcCmdEnterBootloader), 0);
  if (!resp) {
    return std::unexpected(resp.error());
  }
  return {};
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

std::expected<void, AppError> ProgramFlashPayload(
    BootromComm &brom, uint32_t flash_offset,
    std::span<const uint8_t> payload) {
  std::println("--- Erasing Flash ---");
  const uint32_t sector_mask = EgisBootrom::kFlashSectorSize - 1;
  uint32_t erase_size = (payload.size() + sector_mask) & ~sector_mask;

  if (auto res = brom.EraseFlash(flash_offset, erase_size); !res) {
    std::println(stderr, "\n[HALT] Flash Erase failed at offset 0x{:X}: {}",
                 flash_offset, ToString(res.error()));
    return std::unexpected(AppError::kBootromError);
  }

  std::println("--- Writing Flash ---");
  const size_t chunk_size = BootromComm::kMaxFlashWriteSize;
  uint32_t current_addr = flash_offset;
  size_t done = 0;

  for (size_t i = 0; i < payload.size(); i += chunk_size) {
    auto chunk = payload.subspan(i, std::min(chunk_size, payload.size() - i));
    if (auto res = brom.WriteFlash(current_addr, chunk); !res) {
      std::println(stderr, "\n[HALT] Flash Write failed at address 0x{:X}: {}",
                   current_addr, ToString(res.error()));
      return std::unexpected(AppError::kBootromError);
    }
    current_addr += chunk.size();
    done += chunk.size();
    std::print("\rWriting Progress: {} / {} ({:.1f}%)", done, payload.size(),
               (static_cast<float>(done) / payload.size() * 100.0f));
    std::fflush(stdout);
  }

  std::println("\n[OK] Flash Full Binary Write successful");
  return {};
}

std::expected<void, AppError> ProgramRawCommandPayload(
    UsbComm &boot_comm, std::span<const uint8_t> firmware) {
  std::println("--- Flash RAW CMD Binary Flashing ---");
  size_t done = 0;

  while (done < firmware.size()) {
    std::span<const uint8_t> remaining = firmware.subspan(done);

    // 1. Parse bounds
    auto packet_size_res = GetNextPacketSize(remaining);
    if (!packet_size_res) {
      std::println(stderr, "\n[ERROR] Parse failed at offset 0x{:X}: {}", done,
                   ToString(packet_size_res.error()));
      return std::unexpected(AppError::kParseError);
    }

    size_t packet_size = packet_size_res.value();

    // Guard against a packet claiming to be larger than the remaining file size
    if (packet_size > remaining.size()) {
      std::println(
          stderr,
          "\n[ERROR] Packet truncated at offset 0x{:X}. Expected {} bytes.",
          done, packet_size);
      return std::unexpected(AppError::kParseError);
    }

    std::span<const uint8_t> packet = remaining.first(packet_size);

    // 2. Send Data
    auto res = boot_comm.Send(packet);
    if (!res) {
      if (packet[0] == EgisBootrom::kCmdSystemReset) {
        std::println(stderr,
                     "\n[WARN] USB write failed during reset (device may have "
                     "reset instantly).");
      } else {
        std::println(stderr, "\n[ERROR] USB write failed at 0x{:X}: {}", done,
                     ToString(res.error()));
        return std::unexpected(AppError::kUsbError);
      }
    }

    // 3. Receive Response (System reset commands do not generate responses)
    if (packet[0] != EgisBootrom::kCmdSystemReset) {
      std::vector<uint8_t> resp(128, 0);
      auto read_res = boot_comm.Receive(resp);
      if (!read_res || read_res.value() <= 0) {
        std::println(stderr, "\n[ERROR] Device no response at 0x{:X}", done);
        return std::unexpected(AppError::kUsbError);
      }
      if (read_res.value() >= 2 && (resp[0] != 0 || resp[1] != 0)) {
        std::println(
            stderr,
            "\n[ERROR] Device returned error status (0x{:02X}{:02X}) at 0x{:X}",
            resp[1], resp[0], done);
        return std::unexpected(AppError::kBootromError);
      }
    }

    // 4. Advance and print progress
    done += packet_size;
    std::print("\rFlashing Progress: {} / {} ({:.1f}%)", done, firmware.size(),
               (static_cast<float>(done) / firmware.size() * 100.0f));
    std::fflush(stdout);
  }

  std::println("\n[OK] RAW CMD Flashing Finished");
  return {};
}

}  // namespace

std::optional<AppConfig> ParseArgs(int argc, char *argv[]) {
  AppConfig config;

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
        config.mode = AppMode::kFlashFw;
        config.fw_path = optarg;
        break;
      case 'c':
        config.mode = AppMode::kFlashCmd;
        config.fw_path = optarg;
        break;
      case 'a':
        if (std::string_view(optarg) == "SHA256") {
          config.algo_type = EgisCryptoType::kSha256;
        } else if (std::string_view(optarg) == "AESGCM") {
          config.algo_type = EgisCryptoType::kAesGcm;
        } else {
          std::println(stderr,
                       "[ERROR] Invalid --algo: {}. Must be SHA256 or AESGCM.",
                       optarg);
          return std::nullopt;
        }
        break;
      case 'k':
        config.key_path = optarg;
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
          return std::nullopt;
        }
        if (opt == 'o') config.flash_offset = parsed_val.value();
        if (opt == 's') config.fw_size_override = parsed_val.value();
        break;
      }
      case 'd':
        config.dump_cmd_fw = true;
        break;
      case 'h':
        PrintHelp(argv[0]);
        return std::nullopt;
      case '?':  // '?' is returned for unknown option or missing arg.
        PrintHelp(argv[0]);
        return std::nullopt;
      default:
        return std::nullopt;
    }
  }

  if (optind < argc) {
    std::println(stderr, "[ERROR] Unexpected positional argument: {}",
                 argv[optind]);
    PrintHelp(argv[0]);
    return std::nullopt;
  }

  if (config.mode == AppMode::kNone) {
    std::println(stderr,
                 "[ERROR] You must specify a mode (--flashfw, --flashcmd)");
    PrintHelp(argv[0]);
    return std::nullopt;
  }

  if (config.mode == AppMode::kFlashFw &&
      config.algo_type == EgisCryptoType::kAesGcm && config.key_path.empty()) {
    std::println(stderr,
                 "[ERROR] AESGCM requires a key file. Use --key <key.bin>.");
    return std::nullopt;
  }

  return config;
}

std::expected<void, UsbError> ConnectToBootloader(libusb_context *usb_ctx,
                                                  UsbComm &boot_comm) {
  UsbComm app_comm(usb_ctx, kVidGoogle, kPidEgisApp, kAppEpOut, kAppEpIn);
  if (auto res = app_comm.Connect(); res) {
    if (auto ver_res = ExecuteGetVersion(app_comm); !ver_res) {
      std::println(stderr, "[WARN] GET_VERSION failed: {}",
                   ToString(ver_res.error()));
    }
    std::println("--- Switching to Bootloader Mode ---");
    if (auto bl_res = ExecuteBootLoader(app_comm); !bl_res) {
      std::println(stderr, "[WARN] Enter Bootloader failed: {}",
                   ToString(bl_res.error()));
    }
    std::this_thread::sleep_for(kBootloaderTransitionDelay);
  }

  auto start_time = std::chrono::steady_clock::now();
  std::println("Connecting to Bootloader...");
  while (std::chrono::steady_clock::now() - start_time <
         kBootloaderConnectTimeout) {
    if (auto res = boot_comm.Connect(); res) {
      return {};
    }
    std::this_thread::sleep_for(kBootloaderConnectPollInterval);
  }
  return std::unexpected(UsbError::kTimeout);
}

std::expected<void, AppError> RunFlashFw(const AppConfig &config,
                                         BootromComm &brom) {
  std::println("[INFO] Flash FW Mode");
  std::println("FW  : {}", config.fw_path);
  std::println("Key : {}", config.key_path);
  std::println("Offset : 0x{:08X}", config.flash_offset);
  std::println("Size : 0x{:08X}", config.fw_size_override);

  if (config.fw_path.empty()) {
    std::println(stderr, "[ERROR] Missing firmware path");
    return std::unexpected(AppError::kInvalidArgs);
  }

  if (config.algo_type == EgisCryptoType::kAesGcm && config.key_path.empty()) {
    std::println(stderr, "[ERROR] AESGCM requires a key file");
    return std::unexpected(AppError::kInvalidArgs);
  }

  // --- Load firmware ---
  auto firmware_res = ReadBinaryFile(config.fw_path);
  if (!firmware_res) {
    std::println(stderr, "[ERROR] Cannot open or read firmware file '{}': {}",
                 config.fw_path,
                 std::make_error_code(firmware_res.error()).message());
    return std::unexpected(AppError::kFileIoError);
  }
  std::vector<uint8_t> &firmware = firmware_res.value();

  // --- Load AES Key ---
  if (config.algo_type == EgisCryptoType::kAesGcm) {
    std::array<uint8_t, 32> local_aes_key{};
    if (auto key_res = LoadAesKey(config.key_path, local_aes_key); !key_res) {
      std::println(stderr, "[ERROR] Failed to load key file '{}': {}",
                   config.key_path,
                   std::make_error_code(key_res.error()).message());
      return std::unexpected(AppError::kFileIoError);
    }
    brom.SetAesKey(local_aes_key);
    crypto::Cleanse(local_aes_key);
  }

  // --- Dump RAW CMD ---
  if (config.dump_cmd_fw) {
    std::ostringstream oss;
    std::string fw_filename =
        config.fw_path.substr(config.fw_path.find_last_of("/\\") + 1);
    std::string algo_tag =
        (config.algo_type == EgisCryptoType::kAesGcm) ? "aes" : "sha256";
    oss << fw_filename << "_" << algo_tag << "_cmd_" << std::time(nullptr)
        << ".bin";

    if (auto res = brom.EnableRawCmdDump(oss.str()); !res) {
      std::println(stderr, "[ERROR] Cannot create dump file: {}",
                   ToString(res.error()));
      return std::unexpected(AppError::kFileIoError);
    }
  }
  // --- Disable WDT and Write Protect ---
  if (auto res = brom.DisableWatchdog(); !res) {
    std::println(stderr, "[ERROR] Failed to disable watchdog: {}",
                 ToString(res.error()));
    return std::unexpected(AppError::kBootromError);
  }
  if (auto res = brom.DisableWp(); !res) {
    std::println(stderr, "[ERROR] Failed to disable write protect: {}",
                 ToString(res.error()));
    return std::unexpected(AppError::kBootromError);
  }

  if (config.flash_offset >= firmware.size()) {
    std::println(stderr,
                 "[ERROR] flash_offset (0x{:X}) is at or beyond firmware "
                 "size (0x{:X}).",
                 config.flash_offset, firmware.size());
    return std::unexpected(AppError::kInvalidArgs);
  }

  // If size is not specified, flash from offset to the end of the file.
  size_t size_to_flash = firmware.size() - config.flash_offset;
  if (config.fw_size_override > 0) {
    size_to_flash = config.fw_size_override;
  }

  // Ensure we don't read past the end of the firmware file.
  if (config.flash_offset + size_to_flash > firmware.size()) {
    std::println(stderr,
                 "[ERROR] The specified range (offset 0x{:X} + size 0x{:X}) "
                 "exceeds the file size (0x{:X}).",
                 config.flash_offset, size_to_flash, firmware.size());
    return std::unexpected(AppError::kInvalidArgs);
  }

  std::println("--- Flashing Full Binary with Key ---");
  auto fw_payload = std::span<const uint8_t>(firmware).subspan(
      config.flash_offset, size_to_flash);

  // Defer all erase/write logic to the new helper
  auto flash_res = ProgramFlashPayload(brom, config.flash_offset, fw_payload);
  if (!flash_res) return std::unexpected(flash_res.error());
  return {};
}

std::expected<void, AppError> RunFlashCmd(const AppConfig &config,
                                          UsbComm &boot_comm) {
  if (config.fw_path.empty()) {
    std::println(stderr, "[ERROR] -flashcmd requires a command file path.");
    return std::unexpected(AppError::kInvalidArgs);
  }
  auto firmware_res = ReadBinaryFile(config.fw_path);
  if (!firmware_res) {
    std::println(stderr, "[ERROR] Cannot open or read file '{}': {}",
                 config.fw_path,
                 std::make_error_code(firmware_res.error()).message());
    return std::unexpected(AppError::kFileIoError);
  }

  auto flash_res = ProgramRawCommandPayload(boot_comm, firmware_res.value());
  if (!flash_res) return std::unexpected(flash_res.error());
  return {};
}

}  // namespace egis

int main(int argc, char *argv[]) {
  // 1. Parse Arguments
  auto config_opt = egis::ParseArgs(argc, argv);
  if (!config_opt) {
    return EXIT_FAILURE;
  }
  const egis::AppConfig &config = config_opt.value();

  // 2. Initialize USB Context
  libusb_context *raw_ctx = nullptr;
  int rc = libusb_init(&raw_ctx);
  if (rc != 0) {
    std::println(stderr, "[ERROR] Failed to initialize libusb: {}",
                 libusb_error_name(rc));
    return EXIT_FAILURE;
  }
  std::unique_ptr<libusb_context, decltype(&libusb_exit)> usb_ctx(raw_ctx,
                                                                  &libusb_exit);

  // 3. Connect to Device
  egis::UsbComm boot_comm(usb_ctx.get(), egis::kVidEgis,
                          egis::kPidEgisBootloader, egis::kBootloaderEpOut,
                          egis::kBootloaderEpIn);
  if (auto res = egis::ConnectToBootloader(usb_ctx.get(), boot_comm); !res) {
    std::println(stderr, "[ERROR] Failed to connect to bootloader: {}",
                 egis::ToString(res.error()));
    return EXIT_FAILURE;
  }
  egis::BootromComm brom(boot_comm);
  brom.SetWrappedAlgo(config.algo_type);

  // 4. Dispatch to chosen mode
  std::expected<void, egis::AppError> status;
  switch (config.mode) {
    case egis::AppMode::kFlashFw:
      status = egis::RunFlashFw(config, brom);
      break;
    case egis::AppMode::kFlashCmd:
      status = egis::RunFlashCmd(config, boot_comm);
      break;
    case egis::AppMode::kNone:
      status = std::unexpected(egis::AppError::kInvalidArgs);
      break;
  }

  if (status.has_value()) {
    return EXIT_SUCCESS;
  }

  return EXIT_FAILURE;
}
