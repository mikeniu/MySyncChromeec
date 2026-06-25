/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "flasher_logic.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <print>
#include <span>
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
    std::span<const uint8_t> remaining_data) {
  static constexpr size_t kResetCmdSize =
      sizeof(bootrom_protocol::SystemResetCommand);
  if (remaining_data.size() < kResetCmdSize)
    return std::unexpected(ParseError::kDataTooSmall);

  if (remaining_data[0] ==
      std::to_underlying(bootrom_protocol::CommandId::kSystemReset))
    return kResetCmdSize;
  else if (remaining_data[0] !=
           std::to_underlying(bootrom_protocol::CommandId::kSecureWrapped))
    return std::unexpected(ParseError::kInvalidHeader);

  auto parsed_res =
      ReadStruct<bootrom_protocol::SecureCommandBaseHeader>(remaining_data);
  if (!parsed_res) return std::unexpected(ParseError::kTruncatedHeader);

  auto base_header = *parsed_res;
  base_header.FromLittleEndian();

  static constexpr size_t kMaxWrappedSize =
      BootromComm::kMaxFlashWriteSize +
      sizeof(bootrom_protocol::BlockWriteCommand);

  if (base_header.payload_len > kMaxWrappedSize)
    return std::unexpected(ParseError::kInvalidPayloadSize);

  // Fetch the correct header size cleanly
  auto header_size_res =
      bootrom_protocol::GetSecureHeaderSize(base_header.algorithm);
  if (!header_size_res) return std::unexpected(ParseError::kUnsupportedCrypto);

  return *header_size_res + base_header.payload_len;
}

std::expected<void, AppError> ProgramFlashPayload(
    BootromComm& bootrom_comm, uint32_t flash_offset,
    std::span<const uint8_t> payload) {
  std::println("--- Erasing Flash ---");
  const uint32_t sector_mask = bootrom_protocol::kFlashSectorSize - 1;
  uint32_t erase_size = (payload.size() + sector_mask) & ~sector_mask;

  if (auto res = bootrom_comm.EraseFlash(flash_offset, erase_size); !res) {
    std::println(stderr, "\n[HALT] Flash Erase failed at offset 0x{:X}: {}",
                 flash_offset, ToString(res.error()));
    return std::unexpected(AppError::kBootromError);
  }

  std::println("--- Writing Flash ---");
  const size_t total_size = payload.size();
  uint32_t current_addr = flash_offset;

  while (!payload.empty()) {
    size_t chunk_size =
        std::min(BootromComm::kMaxFlashWriteSize, payload.size());

    if (auto res =
            bootrom_comm.WriteFlash(current_addr, payload.first(chunk_size));
        !res) {
      std::println(stderr, "\n[HALT] Flash Write failed at address 0x{:X}: {}",
                   current_addr, ToString(res.error()));
      return std::unexpected(AppError::kBootromError);
    }

    current_addr += chunk_size;
    payload = payload.subspan(chunk_size);

    PrintProgress("Writing Progress", total_size - payload.size(), total_size);
  }

  std::println("\n[OK] Flash Full Binary Write successful");
  return {};
}

std::expected<void, AppError> ProgramRawCommandPayload(
    BootromComm& bootrom_comm, std::span<const uint8_t> firmware) {
  std::println("--- Flash RAW CMD Binary Flashing ---");
  const size_t total_size = firmware.size();

  while (!firmware.empty()) {
    auto packet_size_res = GetNextPacketSize(firmware);
    size_t offset = total_size - firmware.size();

    if (!packet_size_res || *packet_size_res > firmware.size()) {
      std::println(stderr,
                   "\n[ERROR] Parse failed or truncated at offset 0x{:X}",
                   offset);
      return std::unexpected(AppError::kParseError);
    }

    size_t packet_size = *packet_size_res;
    std::span<const uint8_t> packet = firmware.first(packet_size);
    firmware = firmware.subspan(packet_size);

    // If it's a reset, just send it raw
    if (packet[0] ==
        std::to_underlying(bootrom_protocol::CommandId::kSystemReset)) {
      bootrom_comm.SystemReset();
      continue;
    }

    // Send it through BootromComm, which handles the USB receive and Status
    // Header validation automatically!
    if (auto res = bootrom_comm.SendPrewrappedPacket(
            packet, BootromComm::kFlashTimeout);
        !res) {
      std::println(stderr, "\n[ERROR] Device error at 0x{:X}: {}", offset,
                   ToString(res.error()));
      return std::unexpected(AppError::kBootromError);
    }

    size_t bytes_written = total_size - firmware.size();
    PrintProgress("Flashing Progress", bytes_written, total_size);
  }

  std::println("\n[OK] RAW CMD Flashing Finished");
  return {};
}
}  // namespace

std::expected<void, AppError> ExecuteFirmwareFlash(const FlasherConfig& config,
                                                   BootromComm& bootrom_comm) {
  std::println("[INFO] Flash FW Mode");
  std::println("FW  : {}", config.firmware_path);
  std::println("Key : {}", config.key_path);
  std::println("Offset : 0x{:08X}", config.image_offset);
  std::println("Size : 0x{:08X}", config.firmware_size_override);

  if (config.firmware_path.empty())
    return std::unexpected(AppError::kInvalidArgs);
  if (config.crypto_algorithm == EgisCryptoType::kAesGcm &&
      config.key_path.empty()) {
    return std::unexpected(AppError::kInvalidArgs);
  }

  auto firmware_res = ReadBinaryFile(config.firmware_path);
  if (!firmware_res) {
    std::println(stderr, "[ERROR] Cannot open or read firmware file '{}': {}",
                 config.firmware_path,
                 std::make_error_code(firmware_res.error()).message());
    return std::unexpected(AppError::kFileIoError);
  }
  std::vector<uint8_t>& firmware = *firmware_res;

  if (config.crypto_algorithm == EgisCryptoType::kAesGcm) {
    auto key_res = LoadAesKey(config.key_path);
    if (!key_res) {
      std::println(stderr, "[ERROR] Failed to load key file '{}': {}",
                   config.key_path,
                   std::make_error_code(key_res.error()).message());
      return std::unexpected(AppError::kFileIoError);
    }
    bootrom_comm.SetAesKey(*key_res);
    crypto::Cleanse(*key_res);
  }

  if (config.dump_cmd_path.has_value()) {
    if (auto res = bootrom_comm.EnableRawCmdDump(*config.dump_cmd_path); !res) {
      std::println(stderr, "[ERROR] Cannot create dump file: {}",
                   ToString(res.error()));
      return std::unexpected(AppError::kFileIoError);
    }
  }

  if (auto res = bootrom_comm.DisableWatchdog(); !res)
    return std::unexpected(AppError::kBootromError);
  if (auto res = bootrom_comm.DisableWp(); !res)
    return std::unexpected(AppError::kBootromError);

  if (config.image_offset >= firmware.size())
    return std::unexpected(AppError::kInvalidArgs);

  size_t size_to_flash = firmware.size() - config.image_offset;
  if (config.firmware_size_override > 0)
    size_to_flash = config.firmware_size_override;

  if (config.image_offset + size_to_flash > firmware.size()) {
    std::println(stderr, "[ERROR] The specified range exceeds the file size.");
    return std::unexpected(AppError::kInvalidArgs);
  }

  std::println("--- Flashing Full Binary with Key ---");
  auto fw_payload = std::span<const uint8_t>(firmware).subspan(
      config.image_offset, size_to_flash);

  auto flash_res =
      ProgramFlashPayload(bootrom_comm, config.image_offset, fw_payload);
  if (!flash_res) return std::unexpected(flash_res.error());
  return {};
}

std::expected<void, AppError> ExecuteRawCommandFlash(
    const FlasherConfig& config, BootromComm& bootrom_comm) {
  if (config.firmware_path.empty())
    return std::unexpected(AppError::kInvalidArgs);
  auto firmware_res = ReadBinaryFile(config.firmware_path);
  if (!firmware_res) {
    std::println(stderr, "[ERROR] Cannot open or read file '{}': {}",
                 config.firmware_path,
                 std::make_error_code(firmware_res.error()).message());
    return std::unexpected(AppError::kFileIoError);
  }

  auto flash_res = ProgramRawCommandPayload(bootrom_comm, *firmware_res);
  if (!flash_res) return std::unexpected(flash_res.error());
  return {};
}

}  // namespace egis
