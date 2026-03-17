/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_BOOTROM_PROTOCOL_H_
#define UTIL_EGIS_BOOTROM_PROTOCOL_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "crypto_util.h"

namespace egis::EgisBootrom {

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

constexpr uint8_t kSystemResetTypeNormal = 0x01;
// The Additional Authenticated Data (AAD) is required by the Bootrom
// protocol. It represents the Chip ID and revision as a 32-bit
// little-endian integer. 0x00000001 explicitly sets this
// identifier for AES-GCM authentication.
constexpr uint32_t kChipIdAad = 0x00000001;

// Flash address map
constexpr uint32_t kFlashBaseAddr = 0x80000000;
constexpr uint32_t kFlashTotalSize = 2 * 1024 * 1024;
constexpr uint32_t kFlashSectorSize = 4096;

// SPI registers
constexpr uint32_t kSpiRegBase = 0xf0b00000;
constexpr uint32_t kSpiRegId = kSpiRegBase + 0x0;
constexpr uint32_t kSpiRegTfmat = kSpiRegBase + 0x10;
constexpr uint32_t kSpiRegWrcnt = kSpiRegBase + 0x18;
constexpr uint32_t kSpiRegRdcnt = kSpiRegBase + 0x1C;
constexpr uint32_t kSpiRegTctrl = kSpiRegBase + 0x20;
constexpr uint32_t kSpiRegCmd = kSpiRegBase + 0x24;
constexpr uint32_t kSpiRegAddr = kSpiRegBase + 0x28;
constexpr uint32_t kSpiRegData = kSpiRegBase + 0x2C;

// Watchdog / PWM registers
constexpr uint32_t kRegPwmChnen = 0xf040001c;
constexpr uint32_t kRegPwmCtrl = 0xf0400020;
constexpr uint32_t kRegPwmReload = 0xf0400024;
constexpr uint32_t kRegPwmMux = 0xf0e00100;

// ============================================================================
// Bitmasks & Register Payloads (Unsigned Literals)
// ============================================================================

// TCTRL bit definitions
constexpr uint32_t kTctrlTrnsModeOffset = 24;
constexpr uint32_t kTctrlAddrFmtMsk = (1U << 28);
constexpr uint32_t kTctrlAddrEnMsk = (1U << 29);
constexpr uint32_t kTctrlCmdEnMsk = (1U << 30);

constexpr uint32_t kTrnsModeWriteOnly = (1U << kTctrlTrnsModeOffset);
constexpr uint32_t kTrnsModeReadOnly = (2U << kTctrlTrnsModeOffset);
constexpr uint32_t kTrnsModeNoneData = (7U << kTctrlTrnsModeOffset);

constexpr uint32_t kTfmat = 0x00020700;
constexpr uint8_t kFlashSr1Wip = 0x01;

// Watchdog / PWM disable payloads
constexpr uint32_t kPwmCtrlDisableMask = 0x00000004;
constexpr uint32_t kPwmReloadTimeoutVal = 0x00070a21;
constexpr uint32_t kPwmChnenDisableVal = 0x00000008;
constexpr uint32_t kPwmMuxDisableVal = 0x0c000004;

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
} __attribute__((packed));
static_assert(sizeof(BootromInfoResponse) == 12, "Must be exactly 12 bytes");

struct FlashInfoResponse {
  uint32_t flash_size;
  uint32_t reserved;
  char prod_name[8];
  uint8_t remaining[24];
} __attribute__((packed));
static_assert(sizeof(FlashInfoResponse) == 40, "Must be exactly 40 bytes");

struct SecureCommandBaseHeader {
  CommandId command_id = CommandId::kSecureWrapped;
  uint8_t algo;
  uint32_t payload_len;
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
} __attribute__((packed));
static_assert(sizeof(SecureResponseHeader) == 6, "Must be exactly 6 bytes");

}  // namespace egis::EgisBootrom

#endif  // UTIL_EGIS_BOOTROM_PROTOCOL_H_
