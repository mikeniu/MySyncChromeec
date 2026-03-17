/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "flasher_logic.h"

// C system files
#include <cstdio>
#include <ctime>

// C++ system files
#include <algorithm>
#include <array>
#include <cstddef>
#include <print>
#include <span>
#include <sstream>
#include <vector>

// Project headers
#include "crypto_util.h"
#include "egis_util.h"
#include "file_util.h"

namespace egis {

namespace {
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

  if (fw[0] == EgisBootrom::kCmdSystemReset) return 2;
  if (fw[0] != EgisBootrom::kCmdSecureWrapped)
    return std::unexpected(ParseError::kInvalidHeader);

  size_t header_size = 0;
  EgisCryptoType algo = static_cast<EgisCryptoType>(fw[1]);

  if (algo == EgisCryptoType::kSha256) {
    header_size = BootromComm::kScHeaderSizeSha;
  } else if (algo == EgisCryptoType::kAesGcm) {
    header_size = BootromComm::kScHeaderSizeAes;
  } else {
    return std::unexpected(ParseError::kUnsupportedCrypto);
  }

  if (fw.size() < header_size)
    return std::unexpected(ParseError::kTruncatedHeader);

  auto wrapped_size = ReadLittleEndian<uint32_t>(
      fw.subspan(offsetof(BootromComm::SecureCommandShaHeader, payload_len),
                 sizeof(BootromComm::SecureCommandShaHeader::payload_len)));
  if (!wrapped_size) return std::unexpected(ParseError::kInvalidPayloadSize);

  return header_size + wrapped_size.value();
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
    UsbInterface &boot_comm, std::span<const uint8_t> firmware) {
  std::println("--- Flash RAW CMD Binary Flashing ---");
  size_t done = 0;

  while (done < firmware.size()) {
    std::span<const uint8_t> remaining = firmware.subspan(done);
    auto packet_size_res = GetNextPacketSize(remaining);
    if (!packet_size_res) {
      std::println(stderr, "\n[ERROR] Parse failed at offset 0x{:X}: {}", done,
                   ToString(packet_size_res.error()));
      return std::unexpected(AppError::kParseError);
    }

    size_t packet_size = packet_size_res.value();
    if (packet_size > remaining.size()) {
      std::println(
          stderr,
          "\n[ERROR] Packet truncated at offset 0x{:X}. Expected {} bytes.",
          done, packet_size);
      return std::unexpected(AppError::kParseError);
    }

    std::span<const uint8_t> packet = remaining.first(packet_size);
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

    if (packet[0] != EgisBootrom::kCmdSystemReset) {
      std::vector<uint8_t> resp(128, 0);
      auto read_res = boot_comm.Receive(resp);
      if (!read_res || read_res.value() <= 0) {
        std::println(stderr, "\n[ERROR] Device no response at 0x{:X}", done);
        return std::unexpected(AppError::kUsbError);
      }
      if (read_res.value() >= 2) {
        auto status =
            ReadLittleEndian<uint16_t>(std::span(resp).first(sizeof(uint16_t)));
        if (status && status.value() != 0) {
          std::println(stderr,
                       "\n[ERROR] Device returned error status (0x{:04X}) at "
                       "0x{:X}",
                       status.value(), done);
          return std::unexpected(AppError::kBootromError);
        }
      }
    }

    done += packet_size;
    std::print("\rFlashing Progress: {} / {} ({:.1f}%)", done, firmware.size(),
               (static_cast<float>(done) / firmware.size() * 100.0f));
    std::fflush(stdout);
  }

  std::println("\n[OK] RAW CMD Flashing Finished");
  return {};
}
}  // namespace

std::expected<void, AppError> RunFlashFw(const AppConfig &config,
                                         BootromComm &brom) {
  std::println("[INFO] Flash FW Mode");
  std::println("FW  : {}", config.fw_path);
  std::println("Key : {}", config.key_path);
  std::println("Offset : 0x{:08X}", config.flash_offset);
  std::println("Size : 0x{:08X}", config.fw_size_override);

  if (config.fw_path.empty()) return std::unexpected(AppError::kInvalidArgs);
  if (config.algo_type == EgisCryptoType::kAesGcm && config.key_path.empty()) {
    return std::unexpected(AppError::kInvalidArgs);
  }

  auto firmware_res = ReadBinaryFile(config.fw_path);
  if (!firmware_res) {
    std::println(stderr, "[ERROR] Cannot open or read firmware file '{}': {}",
                 config.fw_path,
                 std::make_error_code(firmware_res.error()).message());
    return std::unexpected(AppError::kFileIoError);
  }
  std::vector<uint8_t> &firmware = firmware_res.value();

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

  if (config.dump_cmd_fw) {
    std::ostringstream oss;
    std::string_view path_view = config.fw_path;
    auto fw_filename = path_view.substr(path_view.find_last_of("/\\") + 1);
    std::string_view algo_tag =
        (config.algo_type == EgisCryptoType::kAesGcm) ? "aes" : "sha256";
    oss << fw_filename << "_" << algo_tag << "_cmd_" << std::time(nullptr)
        << ".bin";

    if (auto res = brom.EnableRawCmdDump(oss.str()); !res) {
      std::println(stderr, "[ERROR] Cannot create dump file: {}",
                   ToString(res.error()));
      return std::unexpected(AppError::kFileIoError);
    }
  }

  if (auto res = brom.DisableWatchdog(); !res)
    return std::unexpected(AppError::kBootromError);
  if (auto res = brom.DisableWp(); !res)
    return std::unexpected(AppError::kBootromError);

  if (config.flash_offset >= firmware.size())
    return std::unexpected(AppError::kInvalidArgs);

  size_t size_to_flash = firmware.size() - config.flash_offset;
  if (config.fw_size_override > 0) size_to_flash = config.fw_size_override;

  if (config.flash_offset + size_to_flash > firmware.size()) {
    std::println(stderr, "[ERROR] The specified range exceeds the file size.");
    return std::unexpected(AppError::kInvalidArgs);
  }

  std::println("--- Flashing Full Binary with Key ---");
  auto fw_payload = std::span<const uint8_t>(firmware).subspan(
      config.flash_offset, size_to_flash);

  auto flash_res = ProgramFlashPayload(brom, config.flash_offset, fw_payload);
  if (!flash_res) return std::unexpected(flash_res.error());
  return {};
}

std::expected<void, AppError> RunFlashCmd(const AppConfig &config,
                                          UsbInterface &boot_comm) {
  if (config.fw_path.empty()) return std::unexpected(AppError::kInvalidArgs);
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
