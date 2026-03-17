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
  return offset <= EgisBootrom::kFlashTotalSize &&
         size <= (EgisBootrom::kFlashTotalSize - offset);
}
}  // namespace

BootromComm::~BootromComm() { crypto::Cleanse(aes_key_); }

std::expected<std::vector<uint8_t>, BootromError> BootromComm::DoUsbTransfer(
    std::span<const uint8_t> tx_cmd, size_t min_expected_rx_size,
    std::chrono::milliseconds timeout) {
  DumpRawCmd(tx_cmd);

  if (auto res = usb_.Send(tx_cmd, timeout); !res)
    return std::unexpected(BootromError::kUsbWriteFailed);

  std::vector<uint8_t> ret(kMaxPayloadSize + min_expected_rx_size, 0);
  auto read_res = usb_.Receive(ret, timeout);
  if (!read_res || read_res.value() <= 0)
    return std::unexpected(BootromError::kUsbReadFailed);

  ret.resize(read_res.value());

  if (ret.size() < kStatusHeaderSize)
    return std::unexpected(BootromError::kResponseTruncated);

  auto status =
      ReadLittleEndian<uint16_t>(std::span(ret).first(kStatusHeaderSize));
  if (!status) return std::unexpected(BootromError::kResponseTruncated);

  if (status.value() != 0)
    return std::unexpected(BootromError::kInvalidStatusHeader);

  if (ret.size() < min_expected_rx_size)
    return std::unexpected(BootromError::kResponseTruncated);

  return ret;
}

std::expected<std::span<const uint8_t>, BootromError>
BootromComm::ExtractSecureData(std::span<const uint8_t> raw_response,
                               size_t min_resp_size) {
  auto rsp_hdr_res =
      ReadStruct<EgisBootrom::SecureResponseHeader>(raw_response);
  if (!rsp_hdr_res) return std::unexpected(BootromError::kResponseTruncated);

  // CRITICAL: Perform this check *before* subtracting min_resp_size from
  // raw_response.size() to prevent a potential integer underflow if the
  // response is smaller than the minimum required size.
  if (raw_response.size() < min_resp_size) {
    return std::unexpected(BootromError::kResponseTruncated);
  }

  uint32_t rsp_wrapped_len = FromLittleEndian(rsp_hdr_res->payload_len);

  // Clamp the payload length based on the actual bytes received
  if (rsp_wrapped_len > raw_response.size() - min_resp_size) {
    rsp_wrapped_len = raw_response.size() - min_resp_size;
  }

  // Calculate the size of the non-payload data (e.g., IV + Tag for AES)
  size_t extra_data_size =
      min_resp_size - sizeof(EgisBootrom::SecureResponseHeader);

  return raw_response.subspan(sizeof(EgisBootrom::SecureResponseHeader),
                              extra_data_size + rsp_wrapped_len);
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandRaw(std::span<const uint8_t> data,
                                   std::chrono::milliseconds timeout) {
  auto ret_res = DoUsbTransfer(data, kStatusHeaderSize, timeout);
  if (!ret_res) return std::unexpected(ret_res.error());
  auto &ret = ret_res.value();
  return std::vector<uint8_t>(ret.begin() + kStatusHeaderSize, ret.end());
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandSha(std::span<const uint8_t> data,
                                   std::chrono::milliseconds timeout) {
  if (data.size() > std::numeric_limits<uint32_t>::max()) {
    std::println(stderr, "[ERROR] Payload size exceeds uint32_t max: {}",
                 data.size());
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }

  EgisBootrom::SecureCommandShaHeader req_hdr;
  req_hdr.algo = std::to_underlying(wrapped_algo_);
  req_hdr.payload_len = ToLittleEndian(static_cast<uint32_t>(data.size()));
  crypto::Sha256(data, req_hdr.hash);

  std::vector<uint8_t> cmd = PackMessage(req_hdr, data);
  auto ret_res =
      DoUsbTransfer(cmd, sizeof(EgisBootrom::SecureResponseHeader), timeout);
  if (!ret_res) return std::unexpected(ret_res.error());
  auto &ret = ret_res.value();

  auto secure_data =
      ExtractSecureData(ret, sizeof(EgisBootrom::SecureResponseHeader));
  if (!secure_data) return std::unexpected(secure_data.error());

  return std::vector<uint8_t>(secure_data->begin(), secure_data->end());
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandAes(std::span<const uint8_t> data,
                                   std::chrono::milliseconds timeout) {
  EgisBootrom::SecureCommandAesHeader req_hdr;
  req_hdr.algo = std::to_underlying(wrapped_algo_);
  req_hdr.payload_len = ToLittleEndian(static_cast<uint32_t>(data.size()));

  uint32_t aad_val = ToLittleEndian(EgisBootrom::kChipIdAad);

  std::vector<uint8_t> ciphertext(data.size());
  {
    if (auto res = crypto::GenerateGcmIv(req_hdr.iv); !res) {
      std::println(stderr, "[ERROR] IV Generation Failed");
      return std::unexpected(BootromError::kGcmIvGenerationFailed);
    }

    if (auto res = crypto::AesGcmEncrypt(aes_key_, req_hdr.iv, AsBytes(aad_val),
                                         data, ciphertext, req_hdr.tag);
        !res) {
      std::println(stderr, "[ERROR] Encryption Failed");
      return std::unexpected(BootromError::kGcmEncryptionFailed);
    }
  }

  std::span<const uint8_t> secure_data;
  auto ret_res = DoUsbTransfer(PackMessage(req_hdr, ciphertext),
                               BootromComm::kScHeaderSizeAes, timeout);
  {
    if (!ret_res) return std::unexpected(ret_res.error());

    auto extracted =
        ExtractSecureData(ret_res.value(), BootromComm::kScHeaderSizeAes);
    if (!extracted) return std::unexpected(extracted.error());

    secure_data = extracted.value();
  }

  std::vector<uint8_t> decrypted_payload;
  {
    auto rsp_iv = secure_data.first<crypto::kAesGcmIvSize>();
    auto rsp_tag =
        secure_data.subspan<crypto::kAesGcmIvSize, crypto::kAesGcmTagSize>();
    auto rsp_ciphertext =
        secure_data.subspan(crypto::kAesGcmIvSize + crypto::kAesGcmTagSize);

    decrypted_payload.resize(rsp_ciphertext.size());

    if (auto res =
            crypto::AesGcmDecrypt(aes_key_, rsp_iv, AsBytes(aad_val),
                                  rsp_ciphertext, rsp_tag, decrypted_payload);
        !res) {
      std::println(stderr, "[ERROR] Decryption Failed");
      return std::unexpected(BootromError::kGcmAuthenticationFailed);
    }
  }

  return decrypted_payload;
}

std::expected<void, BootromError> BootromComm::WaitForFlashReady(
    std::chrono::milliseconds timeout) {
  auto start_time = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start_time < timeout) {
    auto sr1 = GetSr1();
    if (!sr1.has_value()) {
      std::println(stderr, "[ERROR] Failed to read SR1 during polling.");
      return std::unexpected(sr1.error());
    }
    if ((sr1.value() & EgisBootrom::kFlashSr1Wip) == 0) {
      return {};
    }
    std::this_thread::sleep_for(kFlashReadyPollInterval);
  }
  std::println(stderr, "[ERROR] Flash ready timeout.");
  return std::unexpected(BootromError::kFlashReadyTimeout);
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommand(std::span<const uint8_t> data,
                                std::chrono::milliseconds timeout) {
  if (data.empty()) {
    return std::unexpected(BootromError::kBufferTooSmall);
  }

  if ((data[0] >= std::to_underlying(EgisBootrom::CommandId::kMemRead)) &&
      (data[0] <= std::to_underlying(EgisBootrom::CommandId::kOtpWrite))) {
    switch (wrapped_algo_) {
      case EgisCryptoType::kSha256:
        return SendBootromCommandSha(data, timeout);
      case EgisCryptoType::kHmacSha256:
        std::println(stderr, "[ERROR] HMAC is planned but not supported yet.");
        return std::unexpected(BootromError::kUnsupportedCrypto);
      case EgisCryptoType::kAesGcm:
        return SendBootromCommandAes(data, timeout);
      default:
        std::println(stderr, "--- Wrapped_Algo is not defined ---");
        return std::unexpected(BootromError::kUnsupportedCrypto);
    }
  }
  return SendBootromCommandRaw(data, timeout);
}

std::expected<void, BootromError> BootromComm::WriteReg(uint32_t addr,
                                                        uint32_t value) {
  EgisBootrom::MemWriteRegCommand req{
      .command_id = EgisBootrom::CommandId::kMemWrite,
      .address = ToLittleEndian(addr),
      .count = ToLittleEndian(uint32_t{1}),
      .value = ToLittleEndian(value)};

  auto resp = SendStruct(req);
  if (!resp) return std::unexpected(resp.error());
  return {};
}

std::expected<void, BootromError> BootromComm::WriteRegs(
    std::initializer_list<RegWrite> regs) {
  for (const auto &[addr, val] : regs) {
    if (auto res = WriteReg(addr, val); !res) return res;
  }
  return {};
}

std::expected<uint32_t, BootromError> BootromComm::ReadReg(uint32_t addr) {
  EgisBootrom::MemReadCommand req{
      .command_id = EgisBootrom::CommandId::kMemRead,
      .address = ToLittleEndian(addr),
      .access_size = EgisBootrom::AccessSize::kUint32,
      .count = ToLittleEndian(uint32_t{1})};

  auto resp = SendStruct(req);
  if (!resp) return std::unexpected(resp.error());

  auto value_out = ReadStruct<uint32_t>(std::span<const uint8_t>(*resp));
  if (!value_out) return std::unexpected(BootromError::kResponseTruncated);
  return FromLittleEndian(value_out.value());
}

std::expected<void, BootromError> BootromComm::WriteEnable() {
  uint32_t tctrl = EgisBootrom::kTrnsModeNoneData | EgisBootrom::kTctrlCmdEnMsk;
  if (auto res = WriteRegs({
          {EgisBootrom::kSpiRegTfmat, 0},
          {EgisBootrom::kSpiRegTctrl, tctrl},
          {EgisBootrom::kSpiRegTfmat, EgisBootrom::kTfmat},
          {EgisBootrom::kSpiRegCmd,
           std::to_underlying(EgisBootrom::SpiFlashOpcode::kWriteEnable)},
      });
      !res) {
    return res;
  }
  return WaitForFlashReady();
}

std::expected<uint8_t, BootromError> BootromComm::GetSr1() {
  uint32_t tctrl = EgisBootrom::kTrnsModeReadOnly | EgisBootrom::kTctrlCmdEnMsk;
  if (auto res = WriteRegs({
          {EgisBootrom::kSpiRegRdcnt, 0},
          {EgisBootrom::kSpiRegTctrl, tctrl},
          {EgisBootrom::kSpiRegTfmat, EgisBootrom::kTfmat},
          {EgisBootrom::kSpiRegCmd,
           std::to_underlying(EgisBootrom::SpiFlashOpcode::kReadStatusReg)},
      });
      !res) {
    return std::unexpected(res.error());
  }
  std::this_thread::sleep_for(kFlashStatusReadDelay);

  auto res = ReadReg(EgisBootrom::kSpiRegData);
  if (!res) return std::unexpected(res.error());
  return static_cast<uint8_t>(res.value() & 0xFF);
}

std::expected<void, BootromError> BootromComm::SetSr1(uint8_t sr1) {
  if (auto res = WriteEnable(); !res) return res;
  uint32_t tctrl =
      EgisBootrom::kTrnsModeWriteOnly | EgisBootrom::kTctrlCmdEnMsk;
  if (auto res = WriteRegs({
          {EgisBootrom::kSpiRegWrcnt, 0},
          {EgisBootrom::kSpiRegTctrl, tctrl},
          {EgisBootrom::kSpiRegTfmat, EgisBootrom::kTfmat},
          {EgisBootrom::kSpiRegData, static_cast<uint32_t>(sr1)},
          {EgisBootrom::kSpiRegCmd,
           std::to_underlying(EgisBootrom::SpiFlashOpcode::kWriteStatusReg)},
      });
      !res) {
    return res;
  }

  if (auto res = WaitForFlashReady(); !res) {
    std::println(stderr, "[WARN] SetSr1 timed out waiting for flash.");
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
  raw_cmd_dump_file_.write(reinterpret_cast<const char *>(cmd.data()),
                           cmd.size());
  raw_cmd_dump_file_.flush();
}

std::expected<void, BootromError> BootromComm::BackupFlash(
    std::string_view filename, uint32_t total_size) {
  if (!IsValidFlashRange(0, total_size)) {
    std::println(stderr, "[ERROR] Backup size {} exceeds flash capacity {}",
                 total_size, EgisBootrom::kFlashTotalSize);
    return std::unexpected(BootromError::kInvalidFlashAddress);
  }

  std::ofstream outfile(std::string(filename),
                        std::ios::binary | std::ios::trunc);
  if (!outfile.is_open())
    return std::unexpected(BootromError::kFileOperationFailed);

  std::println("--- Executing CMD_MEM_READ (0x20) ---");

  uint32_t flash_addr = EgisBootrom::kFlashBaseAddr;
  const uint32_t end_addr = flash_addr + total_size;

  while (flash_addr < end_addr) {
    uint32_t take =
        std::min<uint32_t>(kMaxFlashReadSize, end_addr - flash_addr);

    EgisBootrom::MemReadCommand req{
        .command_id = EgisBootrom::CommandId::kMemRead,
        .address = ToLittleEndian(flash_addr),
        .access_size = EgisBootrom::AccessSize::kUint8,
        .count = ToLittleEndian(take)};

    auto resp = SendStruct(req, kFlashTimeout);
    if (!resp.has_value()) {
      std::println(stderr, "\n[HALT] Failed at address 0x{:x}", flash_addr);
      return std::unexpected(resp.error());
    }

    if (resp->size() < take) {
      std::println(stderr,
                   "\n[WARN] Incomplete payload received, only got {} bytes",
                   resp->size());
      return std::unexpected(BootromError::kResponseTruncated);
    }
    outfile.write(reinterpret_cast<const char *>(resp->data()), resp->size());

    flash_addr += take;

    uint32_t done = total_size - (end_addr - flash_addr);
    std::print("\rBackup Progress: {} / {} ({:.1f}%)", done, total_size,
               (100.0 * done / total_size));
    std::fflush(stdout);
  }

  outfile.close();
  std::println("\nFlash Backup Completed");
  return {};
}

std::expected<std::vector<uint8_t>, BootromError> BootromComm::ReadFlash(
    uint32_t offset, uint32_t length) {
  if (!IsValidFlashRange(offset, length)) {
    std::println(stderr,
                 "[ERROR] Read range (offset: 0x{:X}, size: 0x{:X}) exceeds "
                 "flash capacity.",
                 offset, length);
    return std::unexpected(BootromError::kInvalidFlashAddress);
  }

  std::vector<uint8_t> buffer;
  buffer.reserve(length);

  uint32_t flash_addr = EgisBootrom::kFlashBaseAddr + offset;
  const uint32_t end_addr = flash_addr + length;

  while (flash_addr < end_addr) {
    uint32_t take =
        std::min<uint32_t>(kMaxFlashReadSize, end_addr - flash_addr);

    EgisBootrom::MemReadCommand req{
        .command_id = EgisBootrom::CommandId::kMemRead,
        .address = ToLittleEndian(flash_addr),
        .access_size = EgisBootrom::AccessSize::kUint8,
        .count = ToLittleEndian(take)};

    auto resp = SendStruct(req, kFlashTimeout);

    if (!resp.has_value()) {
      std::println(stderr, "[ERROR] read_flash failed at addr 0x{:x}: {}",
                   flash_addr, ToString(resp.error()));
      return std::unexpected(resp.error());
    }
    if (resp->size() < take) {
      std::println(stderr,
                   "[ERROR] read_flash failed at addr 0x{:x} (size mismatch: "
                   "got {}, expected {})",
                   flash_addr, resp->size(), take);
      return std::unexpected(BootromError::kResponseTruncated);
    }

    buffer.insert(buffer.end(), resp->begin(), resp->end());

    flash_addr += take;
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

  uint32_t addr = EgisBootrom::kFlashBaseAddr + offset;

  EgisBootrom::FlashEraseCommand req{
      .command_id = EgisBootrom::CommandId::kFlashErase,
      .address = ToLittleEndian(addr),
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

  uint32_t addr = EgisBootrom::kFlashBaseAddr + offset;

  EgisBootrom::BlockWriteCommand req_hdr{
      .command_id = EgisBootrom::CommandId::kMemWrite,
      .address = ToLittleEndian(addr),
      .count = ToLittleEndian(static_cast<uint32_t>(data.size()))};

  std::vector<uint8_t> cmd = PackMessage(req_hdr, data);
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
      {EgisBootrom::kRegPwmCtrl, EgisBootrom::kPwmCtrlDisableMask},
      {EgisBootrom::kRegPwmReload, EgisBootrom::kPwmReloadTimeoutVal},
      {EgisBootrom::kRegPwmChnen, EgisBootrom::kPwmChnenDisableVal},
      {EgisBootrom::kRegPwmMux, EgisBootrom::kPwmMuxDisableVal},
  });
}

std::expected<void, BootromError> BootromComm::DisableWp() {
  auto sr1_old_res = GetSr1();
  if (!sr1_old_res) {
    std::println(stderr, "Failed to read initial SR1 value");
    return std::unexpected(sr1_old_res.error());
  }
  std::println("Old SR1: 0x{:02x}", sr1_old_res.value());

  if (auto res = SetSr1(0); !res) {
    return res;
  }

  auto sr1_new_res = GetSr1();
  if (!sr1_new_res) {
    std::println(stderr, "Failed to read SR1 after attempting to clear WP");
    return std::unexpected(sr1_new_res.error());
  }
  std::println("New SR1: 0x{:02x}", sr1_new_res.value());

  if (sr1_new_res.value() != 0) {
    std::println(stderr, "Failed to clear WP bits");
    return std::unexpected(BootromError::kFlashWpClearFailed);
  }
  return {};
}

std::expected<EgisBootrom::BootromInfoResponse, BootromError>
BootromComm::GetBootromInfo() {
  EgisBootrom::GetBootromInfoCommand req{};
  auto resp = SendStruct(req);
  if (!resp) return std::unexpected(resp.error());

  auto info_res = ReadStruct<EgisBootrom::BootromInfoResponse>(*resp);
  if (!info_res) return std::unexpected(BootromError::kResponseTruncated);

  EgisBootrom::BootromInfoResponse info = info_res.value();

  info.version = FromLittleEndian(info.version);
  info.max_cmd_size = FromLittleEndian(info.max_cmd_size);
  info.max_fw_upgrade_size = FromLittleEndian(info.max_fw_upgrade_size);

  return info;
}

std::expected<EgisBootrom::FlashInfoResponse, BootromError>
BootromComm::GetFlashInfo() {
  EgisBootrom::GetFlashInfoCommand req{};
  auto resp = SendStruct(req);
  if (!resp) return std::unexpected(resp.error());

  auto info_res = ReadStruct<EgisBootrom::FlashInfoResponse>(*resp);
  if (!info_res) return std::unexpected(BootromError::kResponseTruncated);

  EgisBootrom::FlashInfoResponse info = info_res.value();

  info.flash_size = FromLittleEndian(info.flash_size);

  return info;
}

std::expected<void, BootromError> BootromComm::SystemReset() {
  std::println("--- Executing System Reset (0x12) ---");

  EgisBootrom::SystemResetCommand req{};
  DumpRawCmd(AsBytes(req));

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

}  // namespace egis
