// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_IAPCONTROL_H_
#define UTIL_ELAN_IAPCONTROL_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct FwVerInfo {
	uint32_t fw_ver;
	uint8_t boot_type;
};

namespace elan
{
constexpr uint16_t kBootVid = 0x04F3;
constexpr uint16_t kBootPid = 0x0910;

class IapControl {
	struct LibusbDeviceListDeleter {
		void operator()(libusb_device **list) const
		{
			if (list)
				libusb_free_device_list(list, 1);
		}
	};

	using LibusbDeviceListPtr =
		std::unique_ptr<libusb_device *, LibusbDeviceListDeleter>;

	struct LibusbDeviceHandleDeleter {
		void operator()(libusb_device_handle *handle) const
		{
			if (handle) {
				libusb_close(handle);
			}
		}
	};

	using LibusbDeviceHandlePtr =
		std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter>;

    public:
	IapControl();
	~IapControl();

	[[nodiscard]] int Initialize(uint16_t vid, uint16_t pid);

	int RunIapProcess(std::span<const uint8_t> rom);

	void SetProgressCallback(std::function<void(size_t, size_t)> callback);

    private:
	int RunIap(uint8_t iap_mode);
	int FinishIap(uint32_t checksum);
	int SoftwareReset();
	std::optional<FwVerInfo> GetFwVersion();
	int AbortProcess();
	int WriteRom(uint32_t length);
	int WriteRomFinish(uint32_t checksum);
	int SetRomAddress(uint32_t address);
	int ReadRom(uint32_t length);
	int ReadRomFinish(uint32_t checksum);
	int WriteRomData(std::span<const uint8_t> buff);
	int ReadRomData(std::span<uint8_t> buff);

	int ConnectDevice(uint16_t vid, uint16_t pid);
	int GetDeviceCount(uint16_t vid, uint16_t pid);
	int OpenDevice(uint16_t vid, uint16_t pid);
	int IapCommand(std::span<const uint8_t> buff);
	int IapSendCommand(std::span<const uint8_t> buff);
	int IapRecvStatus(std::span<uint8_t> buff);
	int IapSendData(std::span<const uint8_t> buff);
	int IapRecvData(std::span<uint8_t> buff);
	int Set32Le(std::span<uint8_t> buff, uint32_t value);
	std::optional<uint32_t> Get32Le(std::span<const uint8_t> buff);
	std::optional<uint32_t> GetChecksum(std::span<const uint8_t> buff);
	int ReleaseResource();

	std::function<void(size_t, size_t)> progress_callback_;
	std::vector<uint8_t> hid_interfaces_;
	std::vector<uint8_t> hid_detach_interfaces_;
	LibusbDeviceHandlePtr dev_handle_ptr_;
	bool libusb_initialized_;
	int device_status_;
};
}

#endif
