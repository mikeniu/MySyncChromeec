/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "bootrom.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <concepts>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <print>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

#include "crypto_util.h"
#include "egis_util.h"

namespace egis {

using namespace std::chrono_literals;

namespace {
bool IsValidFlashRange(uint32_t offset, size_t size) {
  return offset <= bootrom_protocol::kFlashTotalSize &&
         size <= (bootrom_protocol::kFlashTotalSize - offset);
}
}  // namespace

BootromComm::~BootromComm() { crypto::Cleanse(aes_key_); }

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendPrewrappedPacket(std::span<const uint8_t> tx_cmd,
                                  std::chrono::milliseconds timeout) {
  if (auto res = usb_.Send(tx_cmd, timeout); !res)
    return std::unexpected(BootromError::kUsbWriteFailed);

  size_t envelope_size = 0;
  if (auto env_res =
          bootrom_protocol::GetSecureResponseEnvelopeSize(crypto_algorithm_)) {
    envelope_size = *env_res;
  }
  std::vector<uint8_t> rx_buffer(kMaxPayloadSize + envelope_size, 0);
  auto read_res = usb_.Receive(rx_buffer, timeout);
  if (!read_res || *read_res <= 0)
    return std::unexpected(BootromError::kUsbReadFailed);

  rx_buffer.resize(*read_res);

  // Unpack the universal baseline header
  auto hdr_res = ReadStruct<bootrom_protocol::RawResponseHeader>(rx_buffer);
  if (!hdr_res) return std::unexpected(BootromError::kResponseTruncated);

  hdr_res->FromLittleEndian();

  if (hdr_res->status != 0)
    return std::unexpected(BootromError::kInvalidStatusHeader);

  return rx_buffer;
}

std::expected<std::span<const uint8_t>, BootromError>
BootromComm::ExtractSecureData(std::span<const uint8_t> raw_response) {
  auto header_res =
      ReadStruct<bootrom_protocol::SecureResponseHeader>(raw_response);
  if (!header_res) return std::unexpected(BootromError::kResponseTruncated);

  header_res->FromLittleEndian();
  const uint32_t expected_payload_len = header_res->payload_len;

  auto envelope_size_res =
      bootrom_protocol::GetSecureResponseEnvelopeSize(crypto_algorithm_);
  if (!envelope_size_res)
    return std::unexpected(BootromError::kUnsupportedCrypto);

  const size_t envelope_size = *envelope_size_res;

  if (raw_response.size() < envelope_size) {
    return std::unexpected(BootromError::kResponseTruncated);
  }

  if (expected_payload_len > raw_response.size() - envelope_size) {
    std::println(
        stderr,
        "[ERROR] Device promised {} payload bytes but only delivered {}.",
        expected_payload_len, raw_response.size() - envelope_size);
    return std::unexpected(BootromError::kResponseTruncated);
  }

  const size_t crypto_overhead_size =
      envelope_size - sizeof(bootrom_protocol::SecureResponseHeader);

  const size_t total_data_to_extract =
      crypto_overhead_size + expected_payload_len;

  return raw_response.subspan(sizeof(bootrom_protocol::SecureResponseHeader),
                              total_data_to_extract);
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandRaw(std::span<const uint8_t> unwrapped_command,
                                   std::chrono::milliseconds timeout) {
  if (dry_run_) {
    DumpRawCmd(unwrapped_command);
    std::vector<uint8_t> mock_resp;
    if (!unwrapped_command.empty()) {
      if (unwrapped_command[0] ==
          std::to_underlying(bootrom_protocol::CommandId::kGetBootInfo)) {
        bootrom_protocol::BootromInfoResponse info{
            .version = ToLittleEndian(uint32_t{0x01000000}),
            .max_cmd_size = ToLittleEndian(uint32_t{kMaxPayloadSize}),
            .max_fw_upgrade_size = ToLittleEndian(uint32_t{kMaxFlashWriteSize}),
        };
        mock_resp.assign(AsBytes(info).begin(), AsBytes(info).end());
      } else if (unwrapped_command[0] ==
                 std::to_underlying(bootrom_protocol::CommandId::kFlashInfo)) {
        bootrom_protocol::FlashInfoResponse flash{
            .flash_size = ToLittleEndian(bootrom_protocol::kFlashTotalSize),
        };
        std::memcpy(flash.product_name, "ET171", 5);
        mock_resp.assign(AsBytes(flash).begin(), AsBytes(flash).end());
      }
    }
    return mock_resp;
  }
  // RawResponseHeader is at the beginning (status). The payload starts after
  // it.
  auto response_res = SendPrewrappedPacket(unwrapped_command, timeout);
  if (!response_res) return std::unexpected(response_res.error());
  auto& raw_response = *response_res;
  static constexpr size_t kHeaderSize =
      sizeof(bootrom_protocol::RawResponseHeader);
  return std::vector<uint8_t>(raw_response.begin() + kHeaderSize,
                              raw_response.end());
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandSha(std::span<const uint8_t> unwrapped_command,
                                   std::chrono::milliseconds timeout) {
  if (unwrapped_command.size() > std::numeric_limits<uint32_t>::max()) {
    std::println(stderr, "[ERROR] Payload size exceeds uint32_t max: {}",
                 unwrapped_command.size());
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }

  bootrom_protocol::SecureCommandShaHeader req_hdr;
  req_hdr.algorithm = crypto_algorithm_;
  req_hdr.payload_len =
      ToLittleEndian(static_cast<uint32_t>(unwrapped_command.size()));
  req_hdr.hash = crypto::Sha256(unwrapped_command);

  std::vector<uint8_t> cmd = PackMessage(req_hdr, unwrapped_command);
  if (dry_run_) {
    DumpRawCmd(cmd);
    return GetMockSecureResponse(unwrapped_command);
  }
  auto response_res = SendPrewrappedPacket(cmd, timeout);
  if (!response_res) return std::unexpected(response_res.error());
  auto& raw_response = *response_res;

  auto secure_data = ExtractSecureData(raw_response);
  if (!secure_data) return std::unexpected(secure_data.error());

  return std::vector<uint8_t>(secure_data->begin(), secure_data->end());
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandAes(std::span<const uint8_t> unwrapped_command,
                                   std::chrono::milliseconds timeout) {
  if (unwrapped_command.size() > std::numeric_limits<uint32_t>::max()) {
    std::println(stderr, "[ERROR] Payload size exceeds uint32_t max: {}",
                 unwrapped_command.size());
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }

  bootrom_protocol::SecureCommandAesHeader req_hdr;
  req_hdr.algorithm = crypto_algorithm_;
  req_hdr.payload_len =
      ToLittleEndian(static_cast<uint32_t>(unwrapped_command.size()));

  uint32_t aad_val = ToLittleEndian(bootrom_protocol::kChipIdAad);

  std::vector<uint8_t> ciphertext(unwrapped_command.size());
  {
    auto iv_res = crypto::GenerateGcmIv();
    if (!iv_res) {
      std::println(stderr, "[ERROR] IV Generation Failed");
      return std::unexpected(BootromError::kGcmIvGenerationFailed);
    }
    req_hdr.iv = *iv_res;

    auto enc_res =
        crypto::AesGcmEncrypt(aes_key_, req_hdr.iv, AsBytes(aad_val),
                              unwrapped_command, ciphertext, req_hdr.tag);
    if (!enc_res) {
      std::println(stderr, "[ERROR] Encryption Failed");
      return std::unexpected(BootromError::kGcmEncryptionFailed);
    }
  }

  if (dry_run_) {
    DumpRawCmd(PackMessage(req_hdr, ciphertext));
    return GetMockSecureResponse(unwrapped_command);
  }

  std::span<const uint8_t> secure_data;
  auto response_res =
      SendPrewrappedPacket(PackMessage(req_hdr, ciphertext), timeout);
  {
    if (!response_res) return std::unexpected(response_res.error());

    auto& raw_response = *response_res;
    auto extracted = ExtractSecureData(raw_response);
    if (!extracted) return std::unexpected(extracted.error());

    secure_data = *extracted;
  }

  std::vector<uint8_t> decrypted_payload;
  {
    auto rsp_iv = secure_data.first<crypto::kAesGcmIvSize>();
    auto rsp_tag =
        secure_data.subspan<crypto::kAesGcmIvSize, crypto::kAesGcmTagSize>();
    auto rsp_ciphertext =
        secure_data.subspan(crypto::kAesGcmIvSize + crypto::kAesGcmTagSize);

    decrypted_payload.resize(rsp_ciphertext.size());

    auto dec_res =
        crypto::AesGcmDecrypt(aes_key_, rsp_iv, AsBytes(aad_val),
                              rsp_ciphertext, rsp_tag, decrypted_payload);
    if (!dec_res) {
      std::println(stderr, "[ERROR] Decryption Failed");
      return std::unexpected(BootromError::kGcmAuthenticationFailed);
    }
  }

  return decrypted_payload;
}

std::expected<void, BootromError> BootromComm::WaitForFlashReady(
    std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    auto status_register = ReadStatusRegister1();
    if (!status_register.has_value()) {
      std::println(stderr,
                   "[ERROR] Failed to read Status Register 1 during polling.");
      return std::unexpected(status_register.error());
    }
    if ((*status_register & bootrom_protocol::kFlashSr1Wip) == 0) {
      return {};
    }
    std::this_thread::sleep_for(kFlashReadyPollInterval);
  }
  std::println(stderr, "[ERROR] Flash ready timeout.");
  return std::unexpected(BootromError::kFlashReadyTimeout);
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommand(std::span<const uint8_t> unwrapped_command,
                                std::chrono::milliseconds timeout) {
  if (unwrapped_command.empty()) {
    return std::unexpected(BootromError::kBufferTooSmall);
  }

  if ((unwrapped_command[0] >=
       std::to_underlying(bootrom_protocol::CommandId::kMemRead)) &&
      (unwrapped_command[0] <=
       std::to_underlying(bootrom_protocol::CommandId::kOtpWrite))) {
    switch (crypto_algorithm_) {
      case EgisCryptoType::kSha256:
        return SendBootromCommandSha(unwrapped_command, timeout);
      case EgisCryptoType::kHmacSha256:
        std::println(stderr, "[ERROR] HMAC is planned but not supported yet.");
        return std::unexpected(BootromError::kUnsupportedCrypto);
      case EgisCryptoType::kAesGcm:
        return SendBootromCommandAes(unwrapped_command, timeout);
      default:
        std::println(stderr, "--- Crypto algorithm is not defined ---");
        return std::unexpected(BootromError::kUnsupportedCrypto);
    }
  }
  return SendBootromCommandRaw(unwrapped_command, timeout);
}

std::expected<void, BootromError> BootromComm::WriteReg(uint32_t addr,
                                                        uint32_t value) {
  bootrom_protocol::MemWriteRegCommand req{.address = ToLittleEndian(addr),
                                           .count = ToLittleEndian(uint32_t{1}),
                                           .value = ToLittleEndian(value)};

  auto resp = SendStruct(req);
  if (!resp) return std::unexpected(resp.error());
  return {};
}

std::expected<void, BootromError> BootromComm::WriteRegs(
    std::initializer_list<RegWrite> regs) {
  for (const auto& [addr, val] : regs) {
    if (auto res = WriteReg(addr, val); !res) return res;
  }
  return {};
}

std::expected<uint32_t, BootromError> BootromComm::ReadReg(uint32_t addr) {
  bootrom_protocol::MemReadCommand req{
      .address = ToLittleEndian(addr),
      .access_size = bootrom_protocol::AccessSize::kUint32,
      .count = ToLittleEndian(uint32_t{1})};

  return SendAndReadStruct<uint32_t>(req).transform(FromLittleEndian<uint32_t>);
}

std::expected<void, BootromError> BootromComm::WriteEnable() {
  uint32_t tctrl_config = bootrom_protocol::kTransferModeNoDataPhase |
                          bootrom_protocol::kTctrlCmdEnMsk;
  if (auto res = WriteRegs({
          {bootrom_protocol::kSpiRegTfmat, 0},
          {bootrom_protocol::kSpiRegTctrl, tctrl_config},
          {bootrom_protocol::kSpiRegTfmat,
           bootrom_protocol::kSpiTfmatConfigVal},
          {bootrom_protocol::kSpiRegCmd,
           std::to_underlying(bootrom_protocol::SpiFlashOpcode::kWriteEnable)},
      });
      !res) {
    return res;
  }
  return WaitForFlashReady();
}

std::expected<uint8_t, BootromError> BootromComm::ReadStatusRegister1() {
  uint32_t tctrl_config = bootrom_protocol::kTransferModeReadOnly |
                          bootrom_protocol::kTctrlCmdEnMsk;
  if (auto res = WriteRegs({
          {bootrom_protocol::kSpiRegRdcnt, 0},
          {bootrom_protocol::kSpiRegTctrl, tctrl_config},
          {bootrom_protocol::kSpiRegTfmat,
           bootrom_protocol::kSpiTfmatConfigVal},
          {bootrom_protocol::kSpiRegCmd,
           std::to_underlying(
               bootrom_protocol::SpiFlashOpcode::kReadStatusReg)},
      });
      !res) {
    return std::unexpected(res.error());
  }
  std::this_thread::sleep_for(kFlashStatusReadDelay);

  auto res = ReadReg(bootrom_protocol::kSpiRegData);
  if (!res) return std::unexpected(res.error());
  return static_cast<uint8_t>(*res & 0xFF);
}

std::expected<void, BootromError> BootromComm::WriteStatusRegister1(
    uint8_t status_value) {
  if (auto res = WriteEnable(); !res) return res;
  uint32_t tctrl_config = bootrom_protocol::kTransferModeWriteOnly |
                          bootrom_protocol::kTctrlCmdEnMsk;
  if (auto res = WriteRegs({
          {bootrom_protocol::kSpiRegWrcnt, 0},
          {bootrom_protocol::kSpiRegTctrl, tctrl_config},
          {bootrom_protocol::kSpiRegTfmat,
           bootrom_protocol::kSpiTfmatConfigVal},
          {bootrom_protocol::kSpiRegData, static_cast<uint32_t>(status_value)},
          {bootrom_protocol::kSpiRegCmd,
           std::to_underlying(
               bootrom_protocol::SpiFlashOpcode::kWriteStatusReg)},
      });
      !res) {
    return res;
  }

  if (auto res = WaitForFlashReady(); !res) {
    std::println(stderr,
                 "[WARN] WriteStatusRegister1 timed out waiting for flash.");
    return res;
  }
  return {};
}

std::expected<void, BootromError> BootromComm::EnableRawCmdDump(
    std::string_view filename) {
  raw_cmd_dump_file_.open(std::string(filename),
                          std::ios::binary | std::ios::trunc);
  if (!raw_cmd_dump_file_.is_open()) {
    return std::unexpected(BootromError::kFileOperationFailed);
  }
  return {};
}

void BootromComm::DumpRawCmd(std::span<const uint8_t> cmd) {
  if (!raw_cmd_dump_file_.is_open()) return;
  raw_cmd_dump_file_.write(reinterpret_cast<const char*>(cmd.data()),
                           cmd.size());
  raw_cmd_dump_file_.flush();
}

std::expected<void, BootromError> BootromComm::BackupFlash(
    std::string_view filename, uint32_t backup_size) {
  if (!IsValidFlashRange(0, backup_size)) {
    std::println(stderr, "[ERROR] Backup size {} exceeds flash capacity {}",
                 backup_size, bootrom_protocol::kFlashTotalSize);
    return std::unexpected(BootromError::kInvalidFlashAddress);
  }

  std::ofstream outfile(std::string(filename),
                        std::ios::binary | std::ios::trunc);
  if (!outfile.is_open())
    return std::unexpected(BootromError::kFileOperationFailed);

  std::println("--- Executing CMD_MEM_READ (0x20) ---");

  uint32_t flash_addr = bootrom_protocol::kFlashBaseAddr;
  const uint32_t end_addr = flash_addr + backup_size;

  while (flash_addr < end_addr) {
    uint32_t chunk_size =
        std::min<uint32_t>(kMaxFlashReadSize, end_addr - flash_addr);

    bootrom_protocol::MemReadCommand req{
        .address = ToLittleEndian(flash_addr),
        .access_size = bootrom_protocol::AccessSize::kUint8,
        .count = ToLittleEndian(chunk_size)};

    auto resp = SendStruct(req, kFlashTimeout);
    if (!resp.has_value()) {
      std::println(stderr, "\n[HALT] Failed at address 0x{:x}", flash_addr);
      return std::unexpected(resp.error());
    }

    if (resp->size() < chunk_size) {
      std::println(stderr,
                   "\n[WARN] Incomplete payload received, only got {} bytes",
                   resp->size());
      return std::unexpected(BootromError::kResponseTruncated);
    }
    outfile.write(reinterpret_cast<const char*>(resp->data()), resp->size());

    flash_addr += chunk_size;

    uint32_t bytes_read = flash_addr - bootrom_protocol::kFlashBaseAddr;
    PrintProgress("Backup Progress", bytes_read, backup_size);
  }

  outfile.close();
  std::println("\nFlash Backup Completed");
  return {};
}

std::expected<std::vector<uint8_t>, BootromError> BootromComm::ReadFlash(
    uint32_t offset, uint32_t size) {
  if (!IsValidFlashRange(offset, size)) {
    std::println(stderr,
                 "[ERROR] Read range (offset: 0x{:X}, size: 0x{:X}) exceeds "
                 "flash capacity.",
                 offset, size);
    return std::unexpected(BootromError::kInvalidFlashAddress);
  }

  std::vector<uint8_t> buffer;
  buffer.reserve(size);

  uint32_t flash_addr = bootrom_protocol::kFlashBaseAddr + offset;
  const uint32_t end_addr = flash_addr + size;

  while (flash_addr < end_addr) {
    uint32_t chunk_size =
        std::min<uint32_t>(kMaxFlashReadSize, end_addr - flash_addr);

    bootrom_protocol::MemReadCommand req{
        .address = ToLittleEndian(flash_addr),
        .access_size = bootrom_protocol::AccessSize::kUint8,
        .count = ToLittleEndian(chunk_size)};

    auto resp = SendStruct(req, kFlashTimeout);

    if (!resp.has_value()) {
      std::println(stderr, "[ERROR] read_flash failed at addr 0x{:x}: {}",
                   flash_addr, ToString(resp.error()));
      return std::unexpected(resp.error());
    }
    if (resp->size() < chunk_size) {
      std::println(stderr,
                   "[ERROR] read_flash failed at addr 0x{:x} (size mismatch: "
                   "got {}, expected {})",
                   flash_addr, resp->size(), chunk_size);
      return std::unexpected(BootromError::kResponseTruncated);
    }

    buffer.append_range(*resp);

    flash_addr += chunk_size;
  }
  return buffer;
}

std::expected<void, BootromError> BootromComm::EraseFlash(uint32_t offset,
                                                          uint32_t size) {
  if (!IsValidFlashRange(offset, size)) {
    std::println(stderr,
                 "[ERROR] Erase range (offset: 0x{:X}, size: 0x{:X}) exceeds "
                 "flash capacity.",
                 offset, size);
    return std::unexpected(BootromError::kInvalidFlashAddress);
  }

  std::println("--- Executing CMD_FLASH_ERASE (0x22) ---");

  uint32_t addr = bootrom_protocol::kFlashBaseAddr + offset;

  bootrom_protocol::FlashEraseCommand req{.address = ToLittleEndian(addr),
                                          .size = ToLittleEndian(size)};
  auto resp = SendStruct(req, kFlashTimeout);

  if (resp.has_value()) {
    std::println("Erase Success: {} bytes at 0x{:08x}", size, addr);
    return {};
  } else {
    std::println(stderr, "Erase Failed!");
    return std::unexpected(resp.error());
  }
}

std::expected<void, BootromError> BootromComm::WriteFlash(
    uint32_t offset, std::span<const uint8_t> data) {
  if (!IsValidFlashRange(offset, data.size())) {
    std::println(stderr,
                 "[ERROR] Write range (offset: 0x{:X}, size: 0x{:X}) exceeds "
                 "flash capacity.",
                 offset, data.size());
    return std::unexpected(BootromError::kInvalidFlashAddress);
  }

  if (data.size() > kMaxFlashWriteSize) {
    std::println(stderr,
                 "[ERROR] Payload size {} exceeds max chunk limit of {}",
                 data.size(), kMaxFlashWriteSize);
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }

  uint32_t addr = bootrom_protocol::kFlashBaseAddr + offset;

  bootrom_protocol::BlockWriteCommand req{
      .address = ToLittleEndian(addr),
      .count = ToLittleEndian(static_cast<uint32_t>(data.size()))};

  std::vector<uint8_t> cmd = PackMessage(req, data);
  auto resp = SendBootromCommand(cmd, kFlashTimeout);
  if (!resp.has_value()) {
    std::println(stderr, "\n[HALT] USB/Bulk transmission failed at 0x{:x}: {}",
                 addr, ToString(resp.error()));
    return std::unexpected(resp.error());
  }

  if (auto res = WaitForFlashReady(); !res) {
    std::println(
        stderr,
        "\n[HALT] Timeout waiting for flash write to complete at 0x{:x}", addr);
    return std::unexpected(res.error());
  }
  return {};
}

std::expected<void, BootromError> BootromComm::DisableWatchdog() {
  std::println("--- Disabling Watchdog ---");
  return WriteRegs({
      {bootrom_protocol::kRegPwmCtrl, bootrom_protocol::kPwmCtrlDisableVal},
      {bootrom_protocol::kRegPwmReload, bootrom_protocol::kPwmReloadTimeoutVal},
      {bootrom_protocol::kRegPwmChnen, bootrom_protocol::kPwmChnenDisableVal},
      {bootrom_protocol::kRegPwmMux, bootrom_protocol::kPwmMuxDisableVal},
  });
}

std::expected<void, BootromError> BootromComm::DisableWp() {
  auto status_val_old = ReadStatusRegister1();
  if (!status_val_old) {
    std::println(stderr, "Failed to read initial Status Register 1 value");
    return std::unexpected(status_val_old.error());
  }
  std::println("Old Status Register 1: 0x{:02x}", *status_val_old);

  if (auto res = WriteStatusRegister1(0); !res) {
    return res;
  }

  auto status_val_new = ReadStatusRegister1();
  if (!status_val_new) {
    std::println(
        stderr,
        "Failed to read Status Register 1 after attempting to clear WP");
    return std::unexpected(status_val_new.error());
  }
  std::println("New Status Register 1: 0x{:02x}", *status_val_new);

  if (*status_val_new != 0) {
    std::println(stderr, "Failed to clear WP bits");
    return std::unexpected(BootromError::kFlashWpClearFailed);
  }
  return {};
}

std::expected<bootrom_protocol::BootromInfoResponse, BootromError>
BootromComm::GetBootromInfo() {
  auto info_res = SendAndReadStruct<bootrom_protocol::BootromInfoResponse>(
      bootrom_protocol::GetBootromInfoCommand{});

  if (info_res) {
    info_res->FromLittleEndian();
  }

  return info_res;
}

std::expected<bootrom_protocol::FlashInfoResponse, BootromError>
BootromComm::GetFlashInfo() {
  auto info_res = SendAndReadStruct<bootrom_protocol::FlashInfoResponse>(
      bootrom_protocol::GetFlashInfoCommand{});

  if (info_res) {
    info_res->FromLittleEndian();
  }

  return info_res;
}

std::expected<void, BootromError> BootromComm::SystemReset() {
  std::println("--- Executing System Reset (0x12) ---");

  bootrom_protocol::SystemResetCommand req{};
  DumpRawCmd(AsBytes(req));

  if (dry_run_) {
    return {};
  }

  // For System Reset, the device will instantly disconnect from USB.
  // If we attempt to read a response, it will inevitably trigger
  // LIBUSB_ERROR_IO. Therefore, we only send the command and do not wait for a
  // response.
  if (auto res = usb_.Send(AsBytes(req), kResetTimeout); !res) {
    std::println(stderr,
                 "[WARN] USB write failed during reset (device may have reset "
                 "instantly).");
  } else {
    std::println("[INFO] Command sent, device is resetting...");
  }
  return {};
}

std::vector<uint8_t> BootromComm::GetMockSecureResponse(
    std::span<const uint8_t> unwrapped_command) {
  if (unwrapped_command.empty()) return {};

  if (unwrapped_command[0] ==
          std::to_underlying(bootrom_protocol::CommandId::kMemRead) ||
      unwrapped_command[0] ==
          std::to_underlying(bootrom_protocol::CommandId::kOtpRead)) {
    if (unwrapped_command.size() >= sizeof(bootrom_protocol::MemReadCommand)) {
      auto req =
          ReadStruct<bootrom_protocol::MemReadCommand>(unwrapped_command);
      if (req) {
        uint32_t count = FromLittleEndian(req->count);
        uint32_t size = count * std::to_underlying(req->access_size);
        return std::vector<uint8_t>(size, 0);
      }
    }
  }
  return {};
}

}  // namespace egis
