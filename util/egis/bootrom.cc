/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "bootrom.h"

#include "crypto_util.h"
#include "egis_util.h"

// C system files
#include <cstdio>
#include <cstring>

// C++ system files
#include <algorithm>
#include <bit>
#include <chrono>
#include <concepts>
#include <fstream>
#include <limits>
#include <print>
#include <ranges>
#include <thread>
#include <vector>

namespace egis {

using namespace std::chrono_literals;

// ========================================================
// Bootrom Communication Implementation
// ========================================================
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

  if (ret[0] != 0 || ret[1] != 0)
    return std::unexpected(BootromError::kInvalidStatusHeader);

  if (ret.size() < min_expected_rx_size)
    return std::unexpected(BootromError::kResponseTruncated);

  return ret;
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

  SecureCommandShaHeader req_hdr{
      .algo = static_cast<uint8_t>(wrapped_algo_),
      .payload_len = ToLittleEndian(static_cast<uint32_t>(data.size()))};
  crypto::Sha256(data, req_hdr.hash);

  std::vector<uint8_t> cmd(sizeof(req_hdr) + data.size());
  if (!WriteStruct(std::span<uint8_t>(cmd), req_hdr)) {
    return std::unexpected(BootromError::kBufferTooSmall);
  }
  std::memcpy(cmd.data() + sizeof(req_hdr), data.data(), data.size());

  auto ret_res = DoUsbTransfer(cmd, sizeof(SecureResponseHeader), timeout);
  if (!ret_res) return std::unexpected(ret_res.error());
  auto &ret = ret_res.value();

  auto rsp_hdr_res =
      ReadStruct<SecureResponseHeader>(std::span<const uint8_t>(ret));
  if (!rsp_hdr_res) return std::unexpected(BootromError::kResponseTruncated);
  SecureResponseHeader rsp_hdr = rsp_hdr_res.value();

  uint32_t rsp_wrapped_len = FromLittleEndian(rsp_hdr.payload_len);
  if (rsp_wrapped_len > ret.size() - sizeof(SecureResponseHeader)) {
    rsp_wrapped_len = ret.size() - sizeof(SecureResponseHeader);
  }

  auto rsp_payload = std::span<const uint8_t>(ret).subspan(
      sizeof(SecureResponseHeader), rsp_wrapped_len);

  return std::vector<uint8_t>(rsp_payload.begin(), rsp_payload.end());
}

std::expected<std::vector<uint8_t>, BootromError>
BootromComm::SendBootromCommandAes(std::span<const uint8_t> data,
                                   std::chrono::milliseconds timeout) {
  if (data.size() > std::numeric_limits<uint32_t>::max()) {
    std::println(stderr, "[ERROR] Payload size exceeds uint32_t max: {}",
                 data.size());
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }

  SecureCommandAesHeader req_hdr{
      .algo = static_cast<uint8_t>(wrapped_algo_),
      .payload_len = ToLittleEndian(static_cast<uint32_t>(data.size()))};

  if (auto res = crypto::GenerateGcmIv(req_hdr.iv); !res) {
    std::println(stderr, "[ERROR] IV Generation Failed: {}",
                 crypto::ToString(res.error()));
    return std::unexpected(BootromError::kGcmIvGenerationFailed);
  }

  // Construct AAD directly on the stack without vectors
  uint32_t aad_val = ToLittleEndian(EgisBootrom::kChipIdAad);
  std::span<const uint8_t> aad_span(reinterpret_cast<const uint8_t *>(&aad_val),
                                    sizeof(aad_val));

  // Allocate the final command buffer exactly once
  std::vector<uint8_t> cmd(sizeof(req_hdr) + data.size());

  // Encrypt directly into the payload section of the command buffer (Zero-copy)
  auto ciphertext_span = std::span<uint8_t>(cmd).subspan(sizeof(req_hdr));
  if (auto res = crypto::AesGcmEncrypt(aes_key_, req_hdr.iv, aad_span, data,
                                       ciphertext_span, req_hdr.tag);
      !res) {
    std::println(stderr, "[ERROR] Encryption Failed: {}",
                 crypto::ToString(res.error()));
    return std::unexpected(BootromError::kGcmEncryptionFailed);
  }

  if (!WriteStruct(std::span<uint8_t>(cmd), req_hdr)) {
    return std::unexpected(BootromError::kBufferTooSmall);
  }

  size_t min_resp_size =
      sizeof(SecureResponseHeader) + kAesGcmIvSize + kAesGcmTagSize;
  auto ret_res = DoUsbTransfer(cmd, min_resp_size, timeout);
  if (!ret_res) return std::unexpected(ret_res.error());
  auto &ret = ret_res.value();

  auto rsp_hdr_res =
      ReadStruct<SecureResponseHeader>(std::span<const uint8_t>(ret));
  if (!rsp_hdr_res) return std::unexpected(BootromError::kResponseTruncated);
  SecureResponseHeader rsp_hdr = rsp_hdr_res.value();

  uint32_t rsp_wrapped_len = FromLittleEndian(rsp_hdr.payload_len);
  if (rsp_wrapped_len > ret.size() - min_resp_size) {
    rsp_wrapped_len = ret.size() - min_resp_size;
  }

  std::span<uint8_t> ret_span = ret;
  size_t offset = sizeof(SecureResponseHeader);
  auto rsp_iv = ret_span.subspan(offset, kAesGcmIvSize);
  offset += kAesGcmIvSize;
  auto rsp_tag = ret_span.subspan(offset, kAesGcmTagSize);
  offset += kAesGcmTagSize;
  auto rsp_payload = ret_span.subspan(offset, rsp_wrapped_len);

  if (auto res = crypto::AesGcmDecrypt(aes_key_, rsp_iv, aad_span, rsp_payload,
                                       rsp_tag, rsp_payload);
      !res) {
    std::println(stderr, "[ERROR] Decryption Failed: {}",
                 crypto::ToString(res.error()));
    return std::unexpected(BootromError::kGcmAuthenticationFailed);
  }

  return std::vector<uint8_t>(rsp_payload.begin(), rsp_payload.end());
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
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

  if ((data[0] >= EgisBootrom::kCmdMemRead) &&
      (data[0] <= EgisBootrom::kCmdOtpWrite)) {
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
  EgisBootrom::MemWriteRegCommand req{.address = ToLittleEndian(addr),
                                      .count = ToLittleEndian(uint32_t{1}),
                                      .value = ToLittleEndian(value)};

  std::span<const uint8_t> req_span(reinterpret_cast<const uint8_t *>(&req),
                                    sizeof(req));
  auto resp = SendBootromCommand(req_span);
  if (!resp) return std::unexpected(resp.error());
  return {};
}

std::expected<uint32_t, BootromError> BootromComm::ReadReg(uint32_t addr) {
  EgisBootrom::MemReadCommand req{.address = ToLittleEndian(addr),
                                  .access_size = EgisBootrom::kAccessSizeUint32,
                                  .count = ToLittleEndian(uint32_t{1})};

  std::span<const uint8_t> req_span(reinterpret_cast<const uint8_t *>(&req),
                                    sizeof(req));
  auto resp = SendBootromCommand(req_span);
  if (!resp) return std::unexpected(resp.error());

  auto value_out = ReadStruct<uint32_t>(std::span<const uint8_t>(*resp));
  if (!value_out) return std::unexpected(BootromError::kResponseTruncated);
  return FromLittleEndian(value_out.value());
}

std::expected<void, BootromError> BootromComm::WriteEnable() {
  if (auto res = WriteReg(EgisBootrom::kSpiRegTfmat, 0); !res) return res;
  uint32_t tctrl = EgisBootrom::kTrnsModeNoneData | EgisBootrom::kTctrlCmdEnMsk;
  if (auto res = WriteReg(EgisBootrom::kSpiRegTctrl, tctrl); !res) return res;
  if (auto res = WriteReg(EgisBootrom::kSpiRegTfmat, EgisBootrom::kTfmat); !res)
    return res;
  if (auto res = WriteReg(EgisBootrom::kSpiRegCmd, EgisBootrom::kFlashWren);
      !res)
    return res;
  return WaitForFlashReady();
}

std::expected<uint8_t, BootromError> BootromComm::GetSr1() {
  if (auto res = WriteReg(EgisBootrom::kSpiRegRdcnt, 0); !res)
    return std::unexpected(res.error());
  uint32_t tctrl = EgisBootrom::kTrnsModeReadOnly | EgisBootrom::kTctrlCmdEnMsk;
  if (auto res = WriteReg(EgisBootrom::kSpiRegTctrl, tctrl); !res)
    return std::unexpected(res.error());
  if (auto res = WriteReg(EgisBootrom::kSpiRegTfmat, EgisBootrom::kTfmat); !res)
    return std::unexpected(res.error());
  if (auto res = WriteReg(EgisBootrom::kSpiRegCmd, EgisBootrom::kFlashRdsr);
      !res)
    return std::unexpected(res.error());
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  auto res = ReadReg(EgisBootrom::kSpiRegData);
  if (!res) return std::unexpected(res.error());

  return static_cast<uint8_t>(res.value() & 0xFF);
}

std::expected<void, BootromError> BootromComm::SetSr1(uint8_t sr1) {
  if (auto res = WriteEnable(); !res) return res;

  if (auto res = WriteReg(EgisBootrom::kSpiRegWrcnt, 0); !res) return res;
  uint32_t tctrl =
      EgisBootrom::kTrnsModeWriteOnly | EgisBootrom::kTctrlCmdEnMsk;
  if (auto res = WriteReg(EgisBootrom::kSpiRegTctrl, tctrl); !res) return res;
  if (auto res = WriteReg(EgisBootrom::kSpiRegTfmat, EgisBootrom::kTfmat); !res)
    return res;
  if (auto res = WriteReg(EgisBootrom::kSpiRegData, static_cast<uint32_t>(sr1));
      !res)
    return res;
  if (auto res = WriteReg(EgisBootrom::kSpiRegCmd, EgisBootrom::kFlashWrsr);
      !res)
    return res;

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
  std::ofstream outfile(std::string(filename),
                        std::ios::binary | std::ios::trunc);
  if (!outfile.is_open())
    return std::unexpected(BootromError::kFileOperationFailed);

  std::println("--- Executing CMD_MEM_READ (0x20) ---");

  for (uint32_t addr = 0; addr < total_size; addr += kMaxFlashReadSize) {
    uint32_t current_read =
        std::min<uint32_t>(kMaxFlashReadSize, total_size - addr);
    uint32_t flash_addr = EgisBootrom::kFlashBaseAddr + addr;

    EgisBootrom::MemReadCommand req{
        .address = ToLittleEndian(flash_addr),
        .access_size = EgisBootrom::kAccessSizeUint8,
        .count = ToLittleEndian(current_read)};

    std::span<const uint8_t> req_span(reinterpret_cast<const uint8_t *>(&req),
                                      sizeof(req));
    auto resp = SendBootromCommand(req_span, kFlashTimeout);
    if (!resp.has_value()) {
      std::println(stderr, "\n[HALT] Failed at address 0x{:x}", addr);
      return std::unexpected(resp.error());
    }

    size_t received = resp->size();

    if (received < current_read) {
      std::println(stderr,
                   "\n[WARN] Incomplete payload received, only got {} bytes",
                   received);
      return std::unexpected(BootromError::kResponseTruncated);
    }
    outfile.write(reinterpret_cast<const char *>(resp->data()), received);

    std::print("\rBackup Progress: {} / {} ({:.1f}%)", addr + current_read,
               total_size, (100.0 * (addr + current_read) / total_size));
    std::fflush(stdout);
  }

  outfile.close();
  std::println("\nFlash Backup Completed");
  return {};
}

std::expected<std::vector<uint8_t>, BootromError> BootromComm::ReadFlash(
    uint32_t offset, uint32_t length) {
  std::vector<uint8_t> buffer;
  buffer.reserve(length);

  for (uint32_t addr_offset = 0; addr_offset < length;
       addr_offset += kMaxFlashReadSize) {
    uint32_t current_read =
        std::min<uint32_t>(kMaxFlashReadSize, length - addr_offset);
    uint32_t flash_addr = EgisBootrom::kFlashBaseAddr + offset + addr_offset;

    EgisBootrom::MemReadCommand req{
        .address = ToLittleEndian(flash_addr),
        .access_size = EgisBootrom::kAccessSizeUint8,
        .count = ToLittleEndian(current_read)};

    std::span<const uint8_t> req_span(reinterpret_cast<const uint8_t *>(&req),
                                      sizeof(req));
    auto resp = SendBootromCommand(req_span, kFlashTimeout);

    if (!resp.has_value()) {
      std::println(stderr, "[ERROR] read_flash failed at addr 0x{:x}: {}",
                   flash_addr, ToString(resp.error()));
      return std::unexpected(resp.error());
    }
    if (resp->size() < current_read) {
      std::println(stderr,
                   "[ERROR] read_flash failed at addr 0x{:x} (size mismatch: "
                   "got {}, expected {})",
                   flash_addr, resp->size(), current_read);
      return std::unexpected(BootromError::kResponseTruncated);
    }

    buffer.insert(buffer.end(), resp->begin(), resp->end());
  }
  return buffer;
}

std::expected<void, BootromError> BootromComm::EraseFlash(uint32_t offset,
                                                          uint32_t size) {
  std::println("--- Executing CMD_FLASH_ERASE (0x22) ---");

  uint32_t addr = EgisBootrom::kFlashBaseAddr + offset;

  EgisBootrom::FlashEraseCommand req{.address = ToLittleEndian(addr),
                                     .size = ToLittleEndian(size)};
  std::span<const uint8_t> req_span(reinterpret_cast<const uint8_t *>(&req),
                                    sizeof(req));
  auto resp = SendBootromCommand(req_span, kFlashTimeout);

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
  if (data.size() > kMaxFlashWriteSize) {
    std::println(stderr,
                 "[ERROR] Payload size {} exceeds max chunk limit of {}",
                 data.size(), kMaxFlashWriteSize);
    return std::unexpected(BootromError::kPayloadSizeExceedsLimit);
  }

  uint32_t addr = EgisBootrom::kFlashBaseAddr + offset;

  EgisBootrom::BlockWriteCommand req_hdr{
      .address = ToLittleEndian(addr),
      .count = ToLittleEndian(static_cast<uint32_t>(data.size()))};

  std::vector<uint8_t> cmd(sizeof(req_hdr) + data.size());
  if (!WriteStruct(std::span<uint8_t>(cmd), req_hdr)) {
    return std::unexpected(BootromError::kBufferTooSmall);
  }
  std::memcpy(cmd.data() + sizeof(req_hdr), data.data(), data.size());

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
  if (auto res =
          WriteReg(EgisBootrom::kRegPwmCtrl, EgisBootrom::kPwmCtrlDisableMask);
      !res)
    return res;
  if (auto res = WriteReg(EgisBootrom::kRegPwmReload,
                          EgisBootrom::kPwmReloadTimeoutVal);
      !res)
    return res;
  if (auto res =
          WriteReg(EgisBootrom::kRegPwmChnen, EgisBootrom::kPwmChnenDisableVal);
      !res)
    return res;
  if (auto res =
          WriteReg(EgisBootrom::kRegPwmMux, EgisBootrom::kPwmMuxDisableVal);
      !res)
    return res;
  return {};
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

std::expected<void, BootromError> BootromComm::GetBootromInfo() {
  EgisBootrom::GetBootromInfoCommand req;
  std::span<const uint8_t> req_span(reinterpret_cast<const uint8_t *>(&req),
                                    sizeof(req));
  auto resp = SendBootromCommand(req_span);
  if (!resp) return std::unexpected(resp.error());

  auto info_res = ReadStruct<EgisBootrom::BootromInfoResponse>(
      std::span<const uint8_t>(*resp));
  if (!info_res) {
    return std::unexpected(BootromError::kResponseTruncated);
  }
  EgisBootrom::BootromInfoResponse info = info_res.value();

  std::println("Bootrom Version: 0x{:x}", FromLittleEndian(info.version));
  std::println("Max Command Size: {} bytes",
               FromLittleEndian(info.max_cmd_size));
  std::println("Max FW Upgrade Size: {} bytes",
               FromLittleEndian(info.max_fw_upgrade_size));
  return {};
}

std::expected<void, BootromError> BootromComm::GetFlashInfo() {
  std::println("--- Fetching Flash Information ---");

  EgisBootrom::GetFlashInfoCommand req;
  std::span<const uint8_t> req_span(reinterpret_cast<const uint8_t *>(&req),
                                    sizeof(req));
  auto resp = SendBootromCommand(req_span);
  if (!resp.has_value()) {
    std::println(stderr, "[ERROR] Unable to retrieve Flash info.");
    return std::unexpected(resp.error());
  }

  auto info_res = ReadStruct<EgisBootrom::FlashInfoResponse>(
      std::span<const uint8_t>(*resp));
  if (!info_res) {
    std::println(stderr, "[ERROR] Response length insufficient: {}",
                 resp->size());
    return std::unexpected(BootromError::kResponseTruncated);
  }
  EgisBootrom::FlashInfoResponse info = info_res.value();

  std::array<char, 9> prod_name{};
  std::memcpy(prod_name.data(), info.prod_name, 8);

  uint32_t flash_size = FromLittleEndian(info.flash_size);

  std::println("[SUCCESS] Flash Data Received:");
  std::println("  Product Name  : {}", prod_name.data());
  std::println("  Flash Size  : {} bytes ({} MB)", flash_size,
               flash_size / 1024 / 1024);
  return {};
}

std::expected<void, BootromError> BootromComm::SystemReset() {
  std::println("--- Executing System Reset (0x12) ---");

  EgisBootrom::SystemResetCommand req;
  std::span<const uint8_t> req_span(reinterpret_cast<const uint8_t *>(&req),
                                    sizeof(req));

  DumpRawCmd(req_span);

  // For System Reset, the device will instantly disconnect from USB.
  // If we attempt to read a response, it will inevitably trigger
  // LIBUSB_ERROR_IO. Therefore, we only send the command and do not wait for a
  // response.
  if (auto res = usb_.Send(req_span, kResetTimeout); !res) {
    std::println(stderr,
                 "[WARN] USB write failed during reset (device may have reset "
                 "instantly).");
  } else {
    std::println("[INFO] Command sent, device is resetting...");
  }
  return {};
}

}  // namespace egis
