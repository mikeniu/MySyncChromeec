/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// RtsUsbDevice: RTS5817 (Realtek USB DFU device) access for the flashing
// tool. All hardware access goes through RtsUsbBackend.

#ifndef PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_DEVICE_H_
#define PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_DEVICE_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "rts5817_usb_backend.h"

namespace realtekfp {

enum class RtsError {
  kDeviceNotFound,
  kAccessDenied,
  kUsbError,
  kInvalidParameter,
  kTimeout,
  kStall,
  kProtocolError,
  kNoDfuInterface,
  kDeviceRejected,
};

inline std::string_view ToString(RtsError error) {
  switch (error) {
    case RtsError::kDeviceNotFound:
      return "Device not found";
    case RtsError::kAccessDenied:
      return "Permission denied accessing the USB device";
    case RtsError::kUsbError:
      return "USB error";
    case RtsError::kInvalidParameter:
      return "Invalid parameter";
    case RtsError::kTimeout:
      return "Timed out waiting for the device to re-enumerate";
    case RtsError::kStall:
      return "The device stalled the request";
    case RtsError::kProtocolError:
      return "Unexpected response from the device";
    case RtsError::kNoDfuInterface:
      return "No DFU interface found on the device";
    case RtsError::kDeviceRejected:
      return "The device reported a DFU error";
  }
  return "Unknown error";
}

// RTS5817 vendor-specific USB control requests (same as rtsflash.py).
//
// XMEM_READ reads 4 bytes (little endian) of device memory; the address is
// split into wValue (low 16 bits) and wIndex (high 16 bits).
inline constexpr uint8_t kRtsVreqXmemRead = 0x41;
// The initial vector table address tells which image is running.
inline constexpr uint32_t kRtsInitVtorAddr = 0x401E2090;
inline constexpr uint32_t kRtsInitVtorBootRom = 0x00000000;
inline constexpr uint32_t kRtsInitVtorFlashImage = 0x01800000;
// GET_STATUS / SET_STATUS only exist in the Boot ROM.
inline constexpr uint8_t kRtsStdGetStatus = 0x1A;
inline constexpr uint8_t kRtsStdSetStatus = 0x1B;
// wValue for kRtsStdSetStatus: enable download mode.
inline constexpr uint16_t kRtsEnableDownloadMode = 1;

// Pre-computed bmRequestType values (raw uint8_t values per USB spec). Avoids
// -Wdeprecated-enum-enum-conversion when OR'ing different libusb enum types.
//   0x80 = Device-to-host, Standard, Device
//   0x00 = Host-to-device, Standard, Device
//   0xC0 = Device-to-host, Vendor, Device
inline constexpr uint8_t kBmReqD2hStdDev = 0x80;
inline constexpr uint8_t kBmReqH2dStdDev = 0x00;
inline constexpr uint8_t kBmReqD2hVendorDev = 0xC0;

// DFU class-specific requests (bmRequestType = 0x21 or 0xA1).
enum class DfuRequest : uint8_t {
  kDetach = 0x00,
  kDownload = 0x01,
  kGetStatus = 0x03,
  kClrStatus = 0x04,
  kAbort = 0x06,
};

// DFU_GETSTATUS response: bStatus, bwPollTimeout[3], bState, iString.
inline constexpr size_t kDfuGetStatusResponseLen = 6;
inline constexpr size_t kDfuStatusOff = 0;
inline constexpr size_t kDfuPollTimeoutOff = 1;
inline constexpr size_t kDfuStateOff = 4;

// DFU status codes (from USB DFU spec).
enum class DfuStatus : uint8_t {
  kOk = 0x00,
  // The device could not finish the last request (RTS5817: image rejected).
  kNotDone = 0x09,
};

// DFU states (from USB DFU spec).
enum class DfuState : uint8_t {
  kDfuIdle = 2,
  kDfuDnloadSync = 3,
  kDfuDnloadBusy = 4,
  kDfuDnloadIdle = 5,
  kDfuManifestSync = 6,
  kDfuManifest = 7,
  kDfuManifestWaitReset = 8,
  kDfuUploadIdle = 9,
  kDfuError = 10,
};

// The six-byte DFU_GETSTATUS response.
struct DfuStatusResponse {
  uint8_t status = 0;
  std::chrono::milliseconds poll_timeout{0};
  uint8_t state = 0;

  // Decodes the response; bwPollTimeout is a 24-bit little-endian value.
  static DfuStatusResponse FromBytes(
      std::span<const uint8_t, kDfuGetStatusResponseLen> raw);
};

// Print download progress every this many percent.
inline constexpr size_t kDfuProgressStepPct = 10;

// Poll delay used when the device reports bwPollTimeout == 0.
inline constexpr std::chrono::milliseconds kDfuMinPollDelay{1};

// DFU descriptor type.
inline constexpr uint8_t kDfuDescriptorType = 0x21;
// DFU functional descriptor layout (byte offsets and minimum length). The
// RTS5817 reports the 7-byte DFU 1.0 layout (no bcdDFUVersion); wTransferSize
// is the last field read.
inline constexpr size_t kDfuDescTypeOff = 1;
inline constexpr size_t kDfuDescXferSizeOff = 5;
inline constexpr size_t kDfuDescMinLen = kDfuDescXferSizeOff + sizeof(uint16_t);

// DFU class/subclass/protocol codes.
inline constexpr uint8_t kDfuClass = 0xFE;
inline constexpr uint8_t kDfuSubclass = 0x01;

// bmRequestType: 0x21 = Host-to-device, Class, Interface.
inline constexpr uint8_t kBmReqH2dClsIf = 0x21;
// bmRequestType: 0xA1 = Device-to-host, Class, Interface.
inline constexpr uint8_t kBmReqD2hClsIf = 0xA1;

// USB I/O timeouts.
inline constexpr std::chrono::milliseconds kUsbCtrlTimeout{1000};
// Covers the manifest phase, where the device programs and verifies flash.
inline constexpr std::chrono::seconds kUsbDfuTimeout{60};

// Default transfer size if we cannot parse the DFU functional descriptor. This
// is the RTS5817 SPI flash sector size.
inline constexpr uint16_t kDfuDefaultTransferSize = 4096;

// Extracts wTransferSize from the class-specific descriptors that follow a
// DFU interface descriptor. Returns kDfuDefaultTransferSize if there is no
// valid DFU functional descriptor or if it reports a zero transfer size.
uint16_t ParseDfuTransferSize(std::span<const uint8_t> extra);

// The DFU interface of a device and the transfer size it advertises.
struct DfuInterfaceInfo {
  int number = 0;
  uint16_t transfer_size = kDfuDefaultTransferSize;
};

class RtsUsbDevice {
 public:
  using SleepFn = std::function<void(std::chrono::milliseconds)>;

  // `backend` is the USB transport; ownership is taken. `sleep` can be
  // replaced by tests.
  explicit RtsUsbDevice(
      std::unique_ptr<RtsUsbBackend> backend,
      SleepFn sleep = [](std::chrono::milliseconds delay) {
        std::this_thread::sleep_for(delay);
      });
  RtsUsbDevice(const RtsUsbDevice&) = delete;
  RtsUsbDevice& operator=(const RtsUsbDevice&) = delete;
  ~RtsUsbDevice();

  // Opens the device.
  std::expected<void, RtsError> Init(UsbDeviceId id);

  // Polls every `poll_interval`, up to `timeout`, for the device to
  // (re-)appear on the USB bus, e.g. after it re-enumerates. Retries only
  // while the device is absent; any other failure is returned immediately.
  std::expected<void, RtsError> WaitForDevice(
      UsbDeviceId id, std::chrono::milliseconds timeout,
      std::chrono::milliseconds poll_interval);

  // Releases USB resources.
  void Release();

  // Enables DFU download mode via vendor control request. Only the Boot ROM
  // implements the request; a running Flash Image (or a device that stalls the
  // mode query) is left untouched so the caller can go straight to
  // DFU_DETACH.
  std::expected<void, RtsError> EnableDownload();

  // Sends DFU_DETACH to transition the device from Runtime mode to DFU mode.
  // After this call the device re-enumerates; close the handle, wait, and
  // re-open before issuing DFU_DOWNLOAD. `detach_timeout` is the time the
  // device waits before detaching.
  // It is sent in a 16-bit field, so it must fit in 65535 ms.
  std::expected<void, RtsError> DfuDetach(
      std::chrono::milliseconds detach_timeout);

  // Performs the full DFU firmware download and then detaches so the device
  // boots the new image.
  //
  // Must be called on the device in DFU mode (0BDA:5800), i.e. after
  // DfuDetach() made it re-enumerate and WaitForDevice() reopened it.
  // `firmware` must already have any trailing DFU suffix stripped: the
  // RTS5817 rejects it as payload because the downloaded length no longer
  // matches the image header.
  //
  // Fails if the device rejects the image, which it reports by stalling the
  // final zero-length DFU_DNLOAD (RTS5817 validates the length and a SHA-256
  // of the whole image there).
  std::expected<void, RtsError> DfuDownload(std::span<const uint8_t> firmware);

 private:
  // Claims an interface for the lifetime of the object, so every return path
  // releases it.
  class InterfaceClaim {
   public:
    InterfaceClaim(RtsUsbBackend& backend, int interface_number)
        : backend_(backend), interface_number_(interface_number) {}
    InterfaceClaim(const InterfaceClaim&) = delete;
    InterfaceClaim& operator=(const InterfaceClaim&) = delete;
    ~InterfaceClaim() {
      if (claimed_) {
        backend_.ReleaseInterface(interface_number_);
      }
    }

    // Claims the interface; fails with the libusb error code.
    std::expected<void, int> Claim() {
      auto res = backend_.ClaimInterface(interface_number_);

      claimed_ = res.has_value();
      return res;
    }

   private:
    RtsUsbBackend& backend_;
    int interface_number_;
    bool claimed_ = false;
  };

  // Finds the DFU interface and parses the DFU functional descriptor.
  std::expected<DfuInterfaceInfo, RtsError> FindDfuInterface();

  // Reads DFU_GETSTATUS.
  std::expected<DfuStatusResponse, RtsError> DfuGetStatus(int interface);

  // Polls DFU_GETSTATUS until the device is no longer busy and returns the
  // state it settled in: dfuIDLE or dfuDNLOAD-IDLE.
  std::expected<DfuState, RtsError> DfuPollIdle(int interface);

  // Sends DFU_CLRSTATUS. Only valid in dfuERROR; the RTS5817 stalls it, and
  // enters dfuERROR, in any other state.
  std::expected<void, RtsError> DfuClearStatus(int interface);

  // Sends DFU_ABORT, which returns dfuDNLOAD-IDLE / dfuUPLOAD-IDLE to dfuIDLE.
  std::expected<void, RtsError> DfuAbort(int interface);

  // Brings a device left in a non-idle state by an earlier, interrupted
  // transfer back to dfuIDLE.
  std::expected<void, RtsError> DfuRecoverToIdle(int interface);

  // Clears the error state after a failed transfer, but only if the device is
  // in dfuERROR: CLRSTATUS anywhere else makes the RTS5817 stall and enter it.
  void DfuClearIfInError(int interface);

  // Prints the device's bStatus/bState, to explain a rejected transfer.
  void DfuReportStatus(int interface);

  // Sends a zero-length class request to `interface`.
  std::expected<void, RtsError> DfuSendRequest(DfuRequest request,
                                               uint16_t value, int interface);

  std::unique_ptr<RtsUsbBackend> backend_;
  SleepFn sleep_;
};

}  // namespace realtekfp

#endif  // PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_DEVICE_H_
