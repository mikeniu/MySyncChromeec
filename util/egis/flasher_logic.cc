/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "flasher_logic.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <print>
#include <span>
#include <sstream>
#include <vector>

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
  constexpr size_t kResetCmdSize = sizeof(EgisBootrom::SystemResetCommand);
  if (fw.size() < kResetCmdSize)
    return std::unexpected(ParseError::kDataTooSmall);

  // The file format contains raw commands. Check for the two valid starting
  // bytes.
  if (fw[0] == std::to_underlying(EgisBootrom::CommandId::kSystemReset))
    return kResetCmdSize;
  else if (fw[0] != std::to_underlying(EgisBootrom::CommandId::kSecureWrapped))
    return std::unexpected(ParseError::kInvalidHeader);

  auto base_hdr_res = ReadStruct<EgisBootrom::SecureCommandBaseHeader>(fw);
  if (!base_hdr_res) return std::unexpected(ParseError::kTruncatedHeader);

  uint32_t wrapped_size = FromLittleEndian(base_hdr_res->payload_len);

  // CRITICAL: Enforce hardware limits before calculating total packet size to
  // prevent a potential integer overflow. Allow up to MaxFlashWriteSize +
  // header.
  constexpr size_t kMaxWrappedSize =
      BootromComm::kMaxFlashWriteSize + sizeof(EgisBootrom::BlockWriteCommand);
  if (wrapped_size > kMaxWrappedSize)
    return std::unexpected(ParseError::kInvalidPayloadSize);

  size_t header_size = 0;
  EgisCryptoType algo = static_cast<EgisCryptoType>(base_hdr_res->algo);

  if (algo == EgisCryptoType::kSha256) {
    header_size = sizeof(EgisBootrom::SecureCommandShaHeader);
  } else if (algo == EgisCryptoType::kAesGcm) {
    header_size = sizeof(EgisBootrom::SecureCommandAesHeader);
  } else {
    return std::unexpected(ParseError::kUnsupportedCrypto);
  }

  return header_size + wrapped_size;
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
  const size_t total_size = payload.size();
  uint32_t current_addr = flash_offset;

  while (!payload.empty()) {
    size_t take = std::min(BootromComm::kMaxFlashWriteSize, payload.size());

    if (auto res = brom.WriteFlash(current_addr, payload.first(take)); !res) {
      std::println(stderr, "\n[HALT] Flash Write failed at address 0x{:X}: {}",
                   current_addr, ToString(res.error()));
      return std::unexpected(AppError::kBootromError);
    }

    current_addr += take;
    payload = payload.subspan(take);

    size_t done = total_size - payload.size();
    std::print("\rWriting Progress: {} / {} ({:.1f}%)", done, total_size,
               (static_cast<float>(done) / total_size * 100.0f));
    std::fflush(stdout);
  }

  std::println("\n[OK] Flash Full Binary Write successful");
  return {};
}

std::expected<void, AppError> ProgramRawCommandPayload(
    UsbInterface &boot_comm, std::span<const uint8_t> firmware) {
  std::println("--- Flash RAW CMD Binary Flashing ---");
  const size_t total_size = firmware.size();

  while (!firmware.empty()) {
    auto packet_size_res = GetNextPacketSize(firmware);
    size_t offset =
        total_size - firmware.size();  // Only needed for error reporting

    if (!packet_size_res || packet_size_res.value() > firmware.size()) {
      std::println(stderr,
                   "\n[ERROR] Parse failed or truncated at offset 0x{:X}",
                   offset);
      return std::unexpected(AppError::kParseError);
    }

    size_t take = packet_size_res.value();

    std::span<const uint8_t> packet = firmware.first(take);

    firmware = firmware.subspan(take);

    auto res = boot_comm.Send(packet);
    if (!res) {
      if (packet[0] ==
          std::to_underlying(EgisBootrom::CommandId::kSystemReset)) {
        std::println(stderr,
                     "\n[WARN] USB write failed during reset (device may have "
                     "reset instantly).");
      } else {
        std::println(stderr, "\n[ERROR] USB write failed at 0x{:X}: {}", offset,
                     ToString(res.error()));
        return std::unexpected(AppError::kParseError);
      }
    }

    if (packet[0] != std::to_underlying(EgisBootrom::CommandId::kSystemReset)) {
      std::vector<uint8_t> resp(
          BootromComm::kMaxPayloadSize + BootromComm::kStatusHeaderSize, 0);
      auto read_res = boot_comm.Receive(resp);
      if (!read_res || read_res.value() <= 0) {
        std::println(stderr, "\n[ERROR] Device no response at 0x{:X}", offset);
        return std::unexpected(AppError::kUsbError);
      }
      if (read_res.value() >= BootromComm::kStatusHeaderSize) {
        auto status = ReadLittleEndian<uint16_t>(
            std::span(resp).first(BootromComm::kStatusHeaderSize));
        if (status && status.value() != 0) {
          std::println(stderr,
                       "\n[ERROR] Device returned error status (0x{:04X}) at "
                       "0x{:X}",
                       status.value(), offset);
          return std::unexpected(AppError::kBootromError);
        }
      }
    }

    size_t done = total_size - firmware.size();
    std::print("\rFlashing Progress: {} / {} ({:.1f}%)", done, total_size,
               (static_cast<float>(done) / total_size * 100.0f));
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

  if (config.dump_cmd_path.has_value()) {
    if (auto res = brom.EnableRawCmdDump(config.dump_cmd_path.value()); !res) {
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
