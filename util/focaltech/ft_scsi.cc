#include <endian.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "ft_log.h"
#include "usb_context.h"

namespace {

constexpr uint32_t kUsbMsCbwSignature = 0x43425355;
constexpr uint32_t kUsbMsCswSignature = 0x53425355;
constexpr size_t kFtCmdLength = 16;

struct BulkCbWrap {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_transfer_length;
  uint8_t flags;
  uint8_t lun;
  uint8_t cmd_len;
  uint8_t cmd[16];
} __attribute__((packed));

struct BulkCsWrap {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_residue;
  uint8_t status;
} __attribute__((packed));

struct FtCmdFormat {
  uint8_t opcode;
  uint32_t para2;
  uint32_t para3;
  uint32_t para4;
  uint8_t subcmd;
  uint8_t sub_para;
  uint8_t reserved;
} __attribute__((packed));

uint32_t g_cdb_tag = 1;

}  // namespace

std::array<uint8_t, kFtCmdLength> BuildFtCmd(uint8_t opcode, uint8_t subcmd,
                                             uint32_t para2, uint32_t para3,
                                             uint32_t para4, uint8_t sub_para) {
  std::array<uint8_t, kFtCmdLength> cmd_buf{};
  auto* cmd = reinterpret_cast<FtCmdFormat*>(cmd_buf.data());

  cmd->opcode = opcode;
  cmd->subcmd = subcmd;
  cmd->sub_para = sub_para;
  cmd->para2 = para2;
  cmd->para3 = para3;
  cmd->para4 = para4;

  return cmd_buf;
}

std::expected<void, FtError> SendBulkCommand(const UsbDevice& device,
                                             std::span<const uint8_t> scsi_cmd,
                                             std::span<uint8_t> data_buffer,
                                             int is_data_in,
                                             uint32_t timeout_ms) {
  if (scsi_cmd.size() > 16 || scsi_cmd.empty()) {
    LOGE("Invalid SCSI command length.");
    return std::unexpected(FtError::kParameterError);
  }

  // Build CBW
  BulkCbWrap cbw{};
  cbw.signature = htole32(kUsbMsCbwSignature);
  cbw.tag = htole32(g_cdb_tag++);
  cbw.flags = !is_data_in ? 0x00 : 0x80;
  cbw.lun = 0;
  cbw.cmd_len = static_cast<uint8_t>(scsi_cmd.size());
  cbw.data_transfer_length = htole32(data_buffer.size());
  std::memcpy(cbw.cmd, scsi_cmd.data(), scsi_cmd.size());

  // Send CBW
  auto cbw_span = std::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(&cbw), sizeof(cbw));
  auto send_res = device.Send(cbw_span, timeout_ms);
  if (!send_res) {
    LOGE("CBW transfer failed.");
    return std::unexpected(send_res.error());
  }
  if (*send_res != sizeof(cbw)) {
    LOGE("CBW transfer short: sent {} of {}", *send_res, sizeof(cbw));
    return std::unexpected(FtError::kResultFail);
  }

  // MCU reset, cannot receive data – wait for device removal instead
  if (is_data_in == kUsbSendOnly) {
    if (device.ResetComplete(3)) {  // renamed method
      return std::unexpected(FtError::kResultFail);
    }
    return {};
  }

  // Data phase
  if (!data_buffer.empty()) {
    if (is_data_in) {  // read from device
      auto recv_res = device.Recv(data_buffer, timeout_ms);
      if (!recv_res) {
        LOGE("Data IN transfer failed.");
        return std::unexpected(recv_res.error());
      }
      // actual received length is returned; caller can check if needed
    } else {  // write to device
      auto send_res = device.Send(data_buffer, timeout_ms);
      if (!send_res) {
        LOGE("Data OUT transfer failed.");
        return std::unexpected(send_res.error());
      }
      if (*send_res != static_cast<int>(data_buffer.size())) {
        LOGE("Data OUT transfer short: sent {} of {}", *send_res,
             data_buffer.size());
        return std::unexpected(FtError::kResultFail);
      }
    }
  }

  // Receive CSW
  BulkCsWrap csw{};
  auto csw_span =
      std::span<uint8_t>(reinterpret_cast<uint8_t*>(&csw), sizeof(csw));
  auto recv_res = device.Recv(csw_span, timeout_ms);
  if (!recv_res) {
    LOGE("CSW transfer failed.");
    return std::unexpected(recv_res.error());
  }
  if (*recv_res != sizeof(csw)) {
    LOGE("CSW transfer short: received {} of {}", *recv_res, sizeof(csw));
    return std::unexpected(FtError::kResultFail);
  }

  // Validate CSW
  if (le32toh(csw.signature) != kUsbMsCswSignature ||
      le32toh(csw.tag) != le32toh(cbw.tag)) {
    LOGE("CSW signature/tag mismatch.");
    return std::unexpected(FtError::kResultFail);
  }

  if (csw.status != 0x00) {
    LOGW("Command failed with CSW status: {:02x}", csw.status);
    return std::unexpected(FtError::kResultFail);
  }

  return {};
}