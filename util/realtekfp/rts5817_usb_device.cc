/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <expected>
#include <print>
#include <span>
#include <utility>
#include <vector>

namespace realtekfp {

namespace {

RtsError ToRtsError(int usb_error) {
  switch (usb_error) {
    case kUsbErrorNoDevice:
    case kUsbErrorNotFound:
      return RtsError::kDeviceNotFound;
    case kUsbErrorAccess:
      return RtsError::kAccessDenied;
    case kUsbErrorPipe:
      return RtsError::kStall;
    default:
      return RtsError::kUsbError;
  }
}

// Maps a backend failure to an RtsError. Generic USB failures carry no
// detail in RtsError, so log the libusb error name for them.
std::unexpected<RtsError> Fail(const RtsUsbBackend& backend,
                               std::string_view what, int usb_error) {
  const RtsError error = ToRtsError(usb_error);

  if (error == RtsError::kUsbError) {
    std::println(stderr, "Error: {} failed: {}", what,
                 backend.ErrorName(usb_error));
  }
  return std::unexpected(error);
}

}  // namespace

RtsUsbDevice::RtsUsbDevice(std::unique_ptr<RtsUsbBackend> backend,
                           SleepFn sleep)
    : backend_(std::move(backend)), sleep_(std::move(sleep)) {}

RtsUsbDevice::~RtsUsbDevice() { Release(); }

std::expected<void, RtsError> RtsUsbDevice::Init(UsbDeviceId id) {
  Release();

  if (auto res = backend_->Init(); !res) {
    return Fail(*backend_, "libusb_init()", res.error());
  }

  if (auto res = backend_->Open(id); !res) {
    Release();
    return Fail(*backend_, "Opening the device", res.error());
  }

  std::println("Device {:04x}:{:04x} opened.", id.vid, id.pid);
  return {};
}

std::expected<void, RtsError> RtsUsbDevice::WaitForDevice(
    UsbDeviceId id, std::chrono::milliseconds timeout,
    std::chrono::milliseconds poll_interval) {
  if (timeout <= std::chrono::milliseconds::zero() ||
      poll_interval <= std::chrono::milliseconds::zero()) {
    return std::unexpected(RtsError::kInvalidParameter);
  }

  Release();

  if (auto res = backend_->Init(); !res) {
    return Fail(*backend_, "libusb_init()", res.error());
  }

  for (std::chrono::milliseconds waited{0}; waited < timeout;
       waited += poll_interval) {
    auto res = backend_->Open(id);

    if (res) {
      std::println("Device {:04x}:{:04x} opened.", id.vid, id.pid);
      return {};
    }
    if (ToRtsError(res.error()) != RtsError::kDeviceNotFound) {
      Release();
      return Fail(*backend_, "Opening the device", res.error());
    }
    sleep_(poll_interval);
  }

  Release();
  return std::unexpected(RtsError::kTimeout);
}

void RtsUsbDevice::Release() { backend_->Close(); }

std::expected<void, RtsError> RtsUsbDevice::EnableDownload() {
  if (!backend_->IsOpen()) {
    return std::unexpected(RtsError::kDeviceNotFound);
  }

  // Only the Boot ROM implements GET_STATUS / SET_STATUS. A Flash Image
  // stalls them, so check which image is running first.
  // bmRequestType = 0xC0: Device-to-host, Vendor, Device
  std::array<uint8_t, sizeof(uint32_t)> init_vector{};
  auto mode = backend_->ControlTransferIn(
      kBmReqD2hVendorDev, kRtsVreqXmemRead,
      static_cast<uint16_t>(kRtsInitVtorAddr & 0xFFFF),
      static_cast<uint16_t>(kRtsInitVtorAddr >> 16), init_vector,
      kUsbCtrlTimeout);

  if (!mode) {
    if (mode.error() == kUsbErrorPipe) {
      std::println("Mode query stalled; leaving download mode untouched.");
      return {};
    }
    return Fail(*backend_, "Reading R_INITVTOR", mode.error());
  }
  if (*mode < init_vector.size()) {
    std::println(stderr, "Error: Reading R_INITVTOR failed: short read");
    return std::unexpected(RtsError::kProtocolError);
  }

  const uint32_t vector = std::bit_cast<uint32_t>(init_vector);
  if (vector != kRtsInitVtorBootRom) {
    std::println("Not running from Boot ROM (R_INITVTOR = {:#010x}).", vector);
    return {};
  }

  // bmRequestType = 0x80: Device-to-host, Standard, Device
  // bRequest = kRtsStdGetStatus (0x1A), wValue = 0, wIndex = 0
  std::array<uint8_t, 1> status{};
  auto got = backend_->ControlTransferIn(kBmReqD2hStdDev, kRtsStdGetStatus, 0,
                                         0, status, kUsbCtrlTimeout);

  if (!got) {
    return Fail(*backend_, "GET_STATUS", got.error());
  }
  if (*got < status.size()) {
    std::println(stderr, "Error: GET_STATUS failed: short read");
    return std::unexpected(RtsError::kProtocolError);
  }

  if (status[0] != 0) {
    std::println("Download mode already active (status = {}).", status[0]);
    return {};
  }

  // bmRequestType = 0x00: Host-to-device, Standard, Device
  // bRequest = kRtsStdSetStatus (0x1B), wValue = kRtsEnableDownloadMode,
  // wIndex = 0
  auto set = backend_->ControlTransferOut(kBmReqH2dStdDev, kRtsStdSetStatus,
                                          kRtsEnableDownloadMode, 0, {},
                                          kUsbCtrlTimeout);

  if (!set) {
    return Fail(*backend_, "SET_STATUS", set.error());
  }
  std::println("Download mode enabled on device.");
  return {};
}

std::expected<void, RtsError> RtsUsbDevice::DfuDetach(
    std::chrono::milliseconds detach_timeout) {
  // wValue is 16 bits wide.
  if (detach_timeout < std::chrono::milliseconds::zero() ||
      detach_timeout > std::chrono::milliseconds(UINT16_MAX)) {
    return std::unexpected(RtsError::kInvalidParameter);
  }

  // DFU_DETACH tells the device to switch from Runtime mode to DFU mode. The
  // device responds by re-enumerating on the USB bus.
  auto info = FindDfuInterface();
  if (!info) {
    return std::unexpected(info.error());
  }

  // An interface-recipient request needs the interface claimed (which also
  // detaches any kernel driver); otherwise usbfs warns and may return EBUSY.
  InterfaceClaim claim(*backend_, info->number);
  if (auto claimed = claim.Claim(); !claimed) {
    return Fail(*backend_, "Claiming the DFU interface", claimed.error());
  }

  if (auto res = DfuSendRequest(DfuRequest::kDetach,
                                static_cast<uint16_t>(detach_timeout.count()),
                                info->number);
      !res) {
    return res;
  }

  std::println("DFU_DETACH sent.  Waiting for re-enumeration...");
  return {};
}

uint16_t ParseDfuTransferSize(std::span<const uint8_t> extra) {
  // Walk the descriptor stream. The DFU functional descriptor has type 0x21
  // and contains wTransferSize at byte offset 5-6.
  while (extra.size() > kDfuDescTypeOff) {
    uint8_t desc_len = extra[0];

    if (desc_len == 0 || desc_len > extra.size()) {
      break;
    }

    if (extra[kDfuDescTypeOff] == kDfuDescriptorType &&
        desc_len >= kDfuDescMinLen) {
      uint16_t size =
          extra[kDfuDescXferSizeOff] | (extra[kDfuDescXferSizeOff + 1] << 8);

      return size != 0 ? size : kDfuDefaultTransferSize;
    }
    extra = extra.subspan(desc_len);
  }

  return kDfuDefaultTransferSize;
}

std::expected<DfuInterfaceInfo, RtsError> RtsUsbDevice::FindDfuInterface() {
  if (!backend_->IsOpen()) {
    return std::unexpected(RtsError::kDeviceNotFound);
  }

  auto interfaces = backend_->GetInterfaces();

  if (!interfaces) {
    return Fail(*backend_, "Reading the config descriptor", interfaces.error());
  }

  // Walk all interfaces and alternate settings.
  const auto it =
      std::ranges::find_if(*interfaces, [](const RtsUsbInterface& alt) {
        return alt.usb_class == kDfuClass && alt.usb_subclass == kDfuSubclass;
      });

  if (it == interfaces->end()) {
    std::println(stderr, "Error: no DFU interface found on device.");
    return std::unexpected(RtsError::kNoDfuInterface);
  }

  const DfuInterfaceInfo info = {
      .number = it->number,
      .transfer_size = ParseDfuTransferSize(it->extra),
  };

  std::println("DFU transfer size: {} bytes", info.transfer_size);
  std::println("DFU interface found: #{}", info.number);
  return info;
}

DfuStatusResponse DfuStatusResponse::FromBytes(
    std::span<const uint8_t, kDfuGetStatusResponseLen> raw) {
  const uint32_t poll_timeout_ms =
      raw[kDfuPollTimeoutOff] |
      (static_cast<uint32_t>(raw[kDfuPollTimeoutOff + 1]) << 8) |
      (static_cast<uint32_t>(raw[kDfuPollTimeoutOff + 2]) << 16);

  return {
      .status = raw[kDfuStatusOff],
      .poll_timeout = std::chrono::milliseconds(poll_timeout_ms),
      .state = raw[kDfuStateOff],
  };
}

std::expected<void, RtsError> RtsUsbDevice::DfuSendRequest(DfuRequest request,
                                                           uint16_t value,
                                                           int interface) {
  // bmRequestType = 0x21: Host-to-Device, Class, Interface
  // wIndex = interface number
  auto res = backend_->ControlTransferOut(
      kBmReqH2dClsIf, std::to_underlying(request), value,
      static_cast<uint16_t>(interface), {}, kUsbCtrlTimeout);

  if (!res) {
    return Fail(*backend_, "A DFU request", res.error());
  }
  return {};
}

std::expected<DfuStatusResponse, RtsError> RtsUsbDevice::DfuGetStatus(
    int interface) {
  // bmRequestType = 0xA1: Device-to-Host, Class, Interface
  // Data (6 bytes): bStatus, bwPollTimeout[3], bState, iString
  std::array<uint8_t, kDfuGetStatusResponseLen> response{};
  auto got = backend_->ControlTransferIn(
      kBmReqD2hClsIf, std::to_underlying(DfuRequest::kGetStatus), 0,
      static_cast<uint16_t>(interface), response, kUsbDfuTimeout);

  if (!got) {
    return Fail(*backend_, "DFU_GETSTATUS", got.error());
  }
  if (*got < response.size()) {
    std::println(stderr, "Error: DFU_GETSTATUS failed: short read");
    return std::unexpected(RtsError::kProtocolError);
  }
  return DfuStatusResponse::FromBytes(response);
}

std::expected<DfuState, RtsError> RtsUsbDevice::DfuPollIdle(int interface) {
  using enum DfuState;

  // Bound the whole wait: the manifest phase (flash programming and image
  // verification) can take tens of seconds.
  std::chrono::milliseconds slept{0};

  while (slept < kUsbDfuTimeout) {
    auto status = DfuGetStatus(interface);
    if (!status) {
      return std::unexpected(status.error());
    }

    const auto state = static_cast<DfuState>(status->state);
    if (status->status != std::to_underlying(DfuStatus::kOk) ||
        state == kDfuError) {
      std::println(stderr, "DFU error: bStatus={}, bState={}", status->status,
                   status->state);
      return std::unexpected(RtsError::kDeviceRejected);
    }

    switch (state) {
      case kDfuIdle:
      case kDfuDnloadIdle:
        // Terminal success states.
        return state;

      case kDfuManifestWaitReset:
        // The device completed manifest and requests a USB reset. Try issuing
        // one; this may invalidate the device handle (if the device
        // re-enumerates), which is OK.
        std::println("Device requests USB reset after manifest.");
        backend_->ResetDevice();
        return state;

      case kDfuDnloadSync:
      case kDfuDnloadBusy:
      case kDfuManifestSync:
      case kDfuManifest:
        // Transient / busy states: wait for the suggested poll interval.
        break;

      default:
        std::println(stderr, "Unexpected DFU state: bState={}", status->state);
        return std::unexpected(RtsError::kProtocolError);
    }

    // Never sleep past the overall timeout.
    const auto delay = std::min(
        status->poll_timeout > std::chrono::milliseconds::zero()
            ? status->poll_timeout
            : kDfuMinPollDelay,
        std::chrono::duration_cast<std::chrono::milliseconds>(kUsbDfuTimeout) -
            slept);

    sleep_(delay);
    slept += delay;
  }

  std::println(stderr, "Timed out waiting for DFU idle state.");
  return std::unexpected(RtsError::kTimeout);
}

void RtsUsbDevice::DfuClearIfInError(int interface) {
  auto status = DfuGetStatus(interface);

  if (status && status->state == std::to_underlying(DfuState::kDfuError)) {
    DfuClearStatus(interface);
  }
}

void RtsUsbDevice::DfuReportStatus(int interface) {
  if (auto status = DfuGetStatus(interface)) {
    std::println(stderr, "Device DFU status: bStatus={:#04x}, bState={}",
                 status->status, status->state);
  }
}

std::expected<void, RtsError> RtsUsbDevice::DfuClearStatus(int interface) {
  return DfuSendRequest(DfuRequest::kClrStatus, 0, interface);
}

std::expected<void, RtsError> RtsUsbDevice::DfuAbort(int interface) {
  return DfuSendRequest(DfuRequest::kAbort, 0, interface);
}

std::expected<void, RtsError> RtsUsbDevice::DfuRecoverToIdle(int interface) {
  auto status = DfuGetStatus(interface);
  if (!status) {
    return std::unexpected(status.error());
  }

  switch (static_cast<DfuState>(status->state)) {
    case DfuState::kDfuIdle:
      return {};

    case DfuState::kDfuError:
      // CLRSTATUS is only valid here.
      return DfuClearStatus(interface);

    case DfuState::kDfuDnloadIdle:
    case DfuState::kDfuUploadIdle:
      return DfuAbort(interface);

    default:
      // Busy or otherwise unexpected: let the device settle first.
      if (auto idle = DfuPollIdle(interface); !idle) {
        return std::unexpected(idle.error());
      }
      return {};
  }
}

std::expected<void, RtsError> RtsUsbDevice::DfuDownload(
    std::span<const uint8_t> firmware) {
  if (firmware.empty()) {
    return std::unexpected(RtsError::kInvalidParameter);
  }

  auto info = FindDfuInterface();
  if (!info) {
    return std::unexpected(info.error());
  }
  const int interface = info->number;
  const size_t transfer_size = info->transfer_size;

  // A zero-length packet ends the transfer, and wValue is a 16-bit block
  // number: refuse images that cannot be numbered.
  const size_t block_count =
      (firmware.size() + transfer_size - 1) / transfer_size;
  if (block_count >= UINT16_MAX) {
    std::println(stderr, "Error: image needs {} blocks, more than {}",
                 block_count, UINT16_MAX - 1);
    return std::unexpected(RtsError::kInvalidParameter);
  }

  // Claim the interface (detaching any kernel driver); it is released on
  // every return path below.
  InterfaceClaim claim(*backend_, interface);
  if (auto claimed = claim.Claim(); !claimed) {
    return Fail(*backend_, "Claiming the DFU interface", claimed.error());
  }

  // An earlier interrupted transfer may have left the device mid-download.
  if (auto recovered = DfuRecoverToIdle(interface); !recovered) {
    return recovered;
  }

  std::println("Flashing {} bytes ({}-byte blocks)...", firmware.size(),
               transfer_size);

  std::span<const uint8_t> remaining = firmware;
  size_t printed_pct = 0;

  for (uint16_t block_num = 0; !remaining.empty(); ++block_num) {
    const auto chunk =
        remaining.first(std::min(remaining.size(), transfer_size));
    const size_t offset = firmware.size() - remaining.size();

    // bmRequestType = 0x21: Host-to-Device, Class, Interface
    // wValue = block_num, wIndex = interface number
    auto sent = backend_->ControlTransferOut(
        kBmReqH2dClsIf, std::to_underlying(DfuRequest::kDownload), block_num,
        static_cast<uint16_t>(interface), chunk, kUsbDfuTimeout);

    if (!sent || *sent != chunk.size()) {
      std::println(stderr,
                   "\nDFU_DOWNLOAD failed at block {} (offset {:#x}): {}",
                   block_num, offset,
                   !sent ? backend_->ErrorName(sent.error()) : "short write");
      DfuClearIfInError(interface);
      return !sent ? Fail(*backend_, "DFU_DOWNLOAD", sent.error())
                   : std::unexpected(RtsError::kProtocolError);
    }

    // Poll status to wait for the block to be processed.
    if (auto idle = DfuPollIdle(interface); !idle) {
      std::println(stderr, "DFU status error after block {} (offset {:#x}).",
                   block_num, offset);
      DfuClearIfInError(interface);
      return std::unexpected(idle.error());
    }

    remaining = remaining.subspan(chunk.size());

    // Print progress every 10%.
    const size_t pct =
        (firmware.size() - remaining.size()) * 100 / firmware.size();
    if (pct >= printed_pct + kDfuProgressStepPct) {
      std::print("\r  Progress: {}%", pct);
      fflush(stdout);
      printed_pct = pct;
    }
  }

  // The device validates the image length and a SHA-256 of the whole image in
  // the final zero-length DFU_DNLOAD, and stalls it if they do not match.
  const uint16_t last_block = static_cast<uint16_t>(block_count);
  std::println("\r  Progress: 100%");
  auto zlp = backend_->ControlTransferOut(
      kBmReqH2dClsIf, std::to_underlying(DfuRequest::kDownload), last_block,
      static_cast<uint16_t>(interface), {}, kUsbDfuTimeout);

  if (!zlp) {
    std::println(stderr, "Error: the device rejected the image: {}",
                 backend_->ErrorName(zlp.error()));
    DfuReportStatus(interface);
    DfuClearIfInError(interface);
    return Fail(*backend_, "The final DFU_DOWNLOAD", zlp.error());
  }

  // After the final ZLP the device goes through manifest. The RTS5817 is
  // manifestation tolerant, so a good image settles in dfuIDLE.
  std::println("Waiting for device to complete...");
  auto settled = DfuPollIdle(interface);
  if (!settled) {
    DfuClearIfInError(interface);
    return std::unexpected(settled.error());
  }
  if (*settled != DfuState::kDfuIdle) {
    std::println(stderr, "Error: device settled in state {}, not dfuIDLE",
                 std::to_underlying(*settled));
    return std::unexpected(RtsError::kProtocolError);
  }

  // The device stays in DFU mode until it is told to detach; only DFU_DETACH
  // makes it reset into the new image.
  if (auto detached = DfuSendRequest(DfuRequest::kDetach, 0, interface);
      !detached) {
    return detached;
  }
  std::println("DFU download complete; the device is restarting.");
  return {};
}

}  // namespace realtekfp
