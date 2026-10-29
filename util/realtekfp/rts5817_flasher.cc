/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_flasher.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <print>
#include <span>

#include "rts5817_usb_backend.h"
#include "rts5817_usb_device.h"

namespace realtekfp {
namespace {

// DFU_DETACH wValue timeout.
constexpr std::chrono::milliseconds kDetachTimeout{1000};
// Max wait for the device to re-enumerate, and the poll period.
constexpr std::chrono::milliseconds kReEnumTimeout{10000};
constexpr std::chrono::milliseconds kReEnumPollInterval{100};

// Opens the device to flash. An explicit target is used as given. Otherwise the
// runtime device is tried first, then the DFU device, which is what a device
// left in DFU mode by an interrupted update enumerates as.
std::expected<UsbDeviceId, RtsError> OpenTarget(
    RtsUsbDevice& device, const std::optional<UsbDeviceId>& requested) {
  if (requested) {
    if (auto res = device.Init(*requested); !res) {
      return std::unexpected(res.error());
    }
    return *requested;
  }

  std::println("Using default VID:PID {:04X}:{:04X}", kRts5817RuntimeId.vid,
               kRts5817RuntimeId.pid);
  auto runtime = device.Init(kRts5817RuntimeId);
  if (runtime) {
    return kRts5817RuntimeId;
  }
  if (runtime.error() != RtsError::kDeviceNotFound) {
    return std::unexpected(runtime.error());
  }

  std::println("Not found; trying the DFU mode device {:04X}:{:04X}",
               kRts5817DfuId.vid, kRts5817DfuId.pid);
  if (auto dfu = device.Init(kRts5817DfuId); !dfu) {
    return std::unexpected(dfu.error());
  }
  return kRts5817DfuId;
}

}  // namespace

std::expected<void, RtsError> Flash(RtsUsbDevice& device,
                                    const std::optional<UsbDeviceId>& requested,
                                    std::span<const uint8_t> firmware) {
  auto target = OpenTarget(device, requested);
  if (!target) {
    return std::unexpected(target.error());
  }

  // A device that is already in DFU mode must not be asked to enter it again:
  // GET_STATUS stalls there and DFU_DETACH would leave DFU mode instead.
  const bool in_dfu_mode = target->pid == kRts5817DfuId.pid;
  const UsbDeviceId dfu_id = {.vid = target->vid, .pid = kRts5817DfuId.pid};

  if (!in_dfu_mode) {
    if (auto res = device.EnableDownload(); !res) {
      return res;
    }

    // Switch from Runtime mode to DFU mode; the device re-enumerates.
    if (auto res = device.DfuDetach(kDetachTimeout); !res) {
      return res;
    }

    // Poll for the re-enumeration instead of sleeping a fixed time.
    if (auto res =
            device.WaitForDevice(dfu_id, kReEnumTimeout, kReEnumPollInterval);
        !res) {
      return res;
    }
  }

  std::println("Starting DFU download...");

  // This also detaches afterwards, so the device boots the new image.
  if (auto res = device.DfuDownload(firmware); !res) {
    return res;
  }

  // Confirm that the device came back running the new image.
  const UsbDeviceId runtime_id = {.vid = target->vid,
                                  .pid = kRts5817RuntimeId.pid};
  return device.WaitForDevice(runtime_id, kReEnumTimeout, kReEnumPollInterval);
}

}  // namespace realtekfp
