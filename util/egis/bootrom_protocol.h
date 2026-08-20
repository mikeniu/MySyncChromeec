/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_BOOTROM_PROTOCOL_H_
#define UTIL_EGIS_BOOTROM_PROTOCOL_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <system_error>

#include "crypto_util.h"
#include "egis_util.h"

namespace egis {
enum class EgisCryptoType : uint8_t {
  kSha256 = 0x00,
  kHmacSha256 = 0x01,  // TODO: Planned for future release. Not supported yet.
  kAesGcm = 0x02,
};
}  // namespace egis

namespace egis::bootrom_protocol {

// ============================================================================
// Mutually Exclusive Options (Strongly Typed)
// ============================================================================

enum class CommandId : uint8_t {
  kGetBootInfo = 0x10,
  kFlashInfo = 0x11,
  kSystemReset = 0x12,
  kMemRead = 0x20,
  kMemWrite = 0x21,
  kFlashErase = 0x22,
  kOtpRead = 0x23,
  kOtpWrite = 0x24,
  kSecureWrapped = 0xF4,
};

enum class AccessSize : uint8_t {
  kUint8 = 0x01,
  kUint16 = 0x02,
  kUint32 = 0x04,
};

enum class SpiFlashOpcode : uint8_t {
  kWriteStatusReg = 0x01,
  kReadStatusReg = 0x05,
  kWriteEnable = 0x06,
};

// ============================================================================
// Hardware Constants & Memory Map
// ============================================================================

inline constexpr uint8_t kSystemResetTypeNormal = 0x01;
// The Additional Authenticated Data (AAD) is required by the Bootrom
// protocol. It represents the Chip ID and revision as a 32-bit
// little-endian integer. 0x00000001 explicitly sets this
// identifier for AES-GCM authentication.
inline constexpr uint32_t kChipIdAad = 0x00000001;

// Flash address map
inline constexpr uint32_t kFlashBaseAddr = 0x80000000;
inline constexpr uint32_t kFlashTotalSize = 2 * 1024 * 1024;
inline constexpr uint32_t kFlashSectorSize = 4096;

// SPI registers
inline constexpr uint32_t kSpiRegBase = 0xf0b00000;
inline constexpr uint32_t kSpiRegId = kSpiRegBase + 0x0;
inline constexpr uint32_t kSpiRegTfmat = kSpiRegBase + 0x10;
inline constexpr uint32_t kSpiRegWrcnt = kSpiRegBase + 0x18;
inline constexpr uint32_t kSpiRegRdcnt = kSpiRegBase + 0x1C;
inline constexpr uint32_t kSpiRegTctrl = kSpiRegBase + 0x20;
inline constexpr uint32_t kSpiRegCmd = kSpiRegBase + 0x24;
inline constexpr uint32_t kSpiRegAddr = kSpiRegBase + 0x28;
inline constexpr uint32_t kSpiRegData = kSpiRegBase + 0x2C;

// Watchdog / PWM registers
inline constexpr uint32_t kRegPwmChnen = 0xf040001c;
inline constexpr uint32_t kRegPwmCtrl = 0xf0400020;
inline constexpr uint32_t kRegPwmReload = 0xf0400024;
inline constexpr uint32_t kRegPwmMux = 0xf0e00100;

// ============================================================================
// Bitmasks & Register Payloads (Unsigned Literals)
// ============================================================================

// TCTRL bit definitions
inline constexpr uint32_t kTctrlTransferModeOffset = 24;
inline constexpr uint32_t kTctrlAddrFmtMsk = (1U << 28);
inline constexpr uint32_t kTctrlAddrEnMsk = (1U << 29);
inline constexpr uint32_t kTctrlCmdEnMsk = (1U << 30);

inline constexpr uint32_t kTransferModeWriteOnly =
    (1U << kTctrlTransferModeOffset);
inline constexpr uint32_t kTransferModeReadOnly =
    (2U << kTctrlTransferModeOffset);
inline constexpr uint32_t kTransferModeNoDataPhase =
    (7U << kTctrlTransferModeOffset);

inline constexpr uint32_t kSpiTfmatConfigVal = 0x00020700;
inline constexpr uint8_t kFlashSr1Wip = 0x01;

// Watchdog / PWM disable payloads
inline constexpr uint32_t kPwmCtrlDisableVal = 0x00000004;
inline constexpr uint32_t kPwmReloadTimeoutVal = 0x00070a21;
inline constexpr uint32_t kPwmChnenDisableVal = 0x00000008;
inline constexpr uint32_t kPwmMuxDisableVal = 0x0c000004;

// ============================================================================
// Hardware Wire Protocols (Packed Structs & Size Assertions)
// ============================================================================

struct GetBootromInfoCommand {
  CommandId command_id = CommandId::kGetBootInfo;
} __attribute__((packed));
static_assert(sizeof(GetBootromInfoCommand) == 1, "Must be exactly 1 byte");

struct GetFlashInfoCommand {
  CommandId command_id = CommandId::kFlashInfo;
} __attribute__((packed));
static_assert(sizeof(GetFlashInfoCommand) == 1, "Must be exactly 1 byte");

struct SystemResetCommand {
  CommandId command_id = CommandId::kSystemReset;
  uint8_t reset_type = kSystemResetTypeNormal;
} __attribute__((packed));
static_assert(sizeof(SystemResetCommand) == 2, "Must be exactly 2 bytes");

struct FlashEraseCommand {
  CommandId command_id = CommandId::kFlashErase;
  uint32_t address;
  uint32_t size;
} __attribute__((packed));
static_assert(sizeof(FlashEraseCommand) == 9, "Must be exactly 9 bytes");

struct MemReadCommand {
  CommandId command_id = CommandId::kMemRead;
  uint32_t address;
  AccessSize access_size;
  uint32_t count;
} __attribute__((packed));
static_assert(sizeof(MemReadCommand) == 10, "Must be exactly 10 bytes");

struct BlockWriteCommand {
  CommandId command_id = CommandId::kMemWrite;
  uint32_t address;
  AccessSize access_size = AccessSize::kUint8;
  uint32_t count;
} __attribute__((packed));
static_assert(sizeof(BlockWriteCommand) == 10, "Must be exactly 10 bytes");

struct MemWriteRegCommand {
  CommandId command_id = CommandId::kMemWrite;
  uint32_t address;
  AccessSize access_size = AccessSize::kUint32;
  uint32_t count;
  uint32_t value;
} __attribute__((packed));
static_assert(sizeof(MemWriteRegCommand) == 14, "Must be exactly 14 bytes");

struct BootromInfoResponse {
  uint32_t version;
  uint32_t max_cmd_size;
  uint32_t max_fw_upgrade_size;

  void FromLittleEndian() {
    version = egis::FromLittleEndian(version);
    max_cmd_size = egis::FromLittleEndian(max_cmd_size);
    max_fw_upgrade_size = egis::FromLittleEndian(max_fw_upgrade_size);
  }
} __attribute__((packed));
static_assert(sizeof(BootromInfoResponse) == 12, "Must be exactly 12 bytes");

struct FlashInfoResponse {
  uint32_t flash_size;
  uint32_t reserved;
  char product_name[8];
  uint8_t reserved_padding[24];

  void FromLittleEndian() {
    flash_size = egis::FromLittleEndian(flash_size);
    reserved = egis::FromLittleEndian(reserved);
  }
} __attribute__((packed));
static_assert(sizeof(FlashInfoResponse) == 40, "Must be exactly 40 bytes");

struct RawResponseHeader {
  uint16_t status;

  void FromLittleEndian() { status = egis::FromLittleEndian(status); }
} __attribute__((packed));
static_assert(sizeof(RawResponseHeader) == 2, "Must be exactly 2 bytes");

struct SecureCommandBaseHeader {
  CommandId command_id = CommandId::kSecureWrapped;
  egis::EgisCryptoType algorithm;
  uint32_t payload_len;

  void FromLittleEndian() { payload_len = egis::FromLittleEndian(payload_len); }
} __attribute__((packed));
static_assert(sizeof(SecureCommandBaseHeader) == 6, "Must be exactly 6 bytes");

struct SecureCommandShaHeader : public SecureCommandBaseHeader {
  std::array<uint8_t, crypto::kSha256DigestSize> hash{};
} __attribute__((packed));
static_assert(sizeof(SecureCommandShaHeader) ==
                  sizeof(SecureCommandBaseHeader) + crypto::kSha256DigestSize,
              "Size of SHA header is incorrect");

struct SecureCommandAesHeader : public SecureCommandBaseHeader {
  std::array<uint8_t, crypto::kAesGcmIvSize> iv{};
  std::array<uint8_t, crypto::kAesGcmTagSize> tag{};
} __attribute__((packed));
static_assert(sizeof(SecureCommandAesHeader) ==
                  sizeof(SecureCommandBaseHeader) + crypto::kAesGcmIvSize +
                      crypto::kAesGcmTagSize,
              "Size of AES header is incorrect");

struct SecureResponseHeader {
  uint16_t status;
  uint32_t payload_len;

  void FromLittleEndian() {
    status = egis::FromLittleEndian(status);
    payload_len = egis::FromLittleEndian(payload_len);
  }
} __attribute__((packed));
static_assert(sizeof(SecureResponseHeader) == 6, "Must be exactly 6 bytes");

constexpr std::expected<size_t, std::errc> GetSecureHeaderSize(
    EgisCryptoType algorithm) {
  switch (algorithm) {
    case EgisCryptoType::kSha256:
      return sizeof(SecureCommandShaHeader);
    case EgisCryptoType::kAesGcm:
      return sizeof(SecureCommandAesHeader);
    default:
      return std::unexpected(std::errc::invalid_argument);
  }
}

constexpr std::expected<size_t, std::errc> GetSecureResponseEnvelopeSize(
    EgisCryptoType algorithm) {
  switch (algorithm) {
    case EgisCryptoType::kSha256:
      return sizeof(SecureResponseHeader);
    case EgisCryptoType::kAesGcm:
      return sizeof(SecureResponseHeader) + crypto::kAesGcmIvSize +
             crypto::kAesGcmTagSize;
    default:
      return std::unexpected(std::errc::invalid_argument);
  }
}

}  // namespace egis::bootrom_protocol

#endif  // UTIL_EGIS_BOOTROM_PROTOCOL_H_
