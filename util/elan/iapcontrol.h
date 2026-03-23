// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IAPCONTROL_H
#define IAPCONTROL_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <libusb.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#define BOOT_VID 0x04F3
#define BOOT_PID 0x0910

struct FWVERINFO {
	uint32_t fwVer;
	uint8_t bootType;
};

namespace ElanApp
{

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

	[[nodiscard]] int initialize(uint16_t vid, uint16_t pid);

	int runIapProcess(std::span<const uint8_t> rom);

	void setProgressCallback(std::function<void(size_t, size_t)> callback);

    private:
	int runIap(uint8_t iapMode);
	int finishIap(uint32_t checksum);
	int softwareReset();
	std::optional<FWVERINFO> getFwVersion();
	int abortProcess();
	int writeRom(uint32_t length);
	int writeRomFinish(uint32_t checksum);
	int setRomAddress(uint32_t address);
	int setOptionAddress(uint32_t address);
	int readRom(uint32_t length);
	int readRomFinish(uint32_t checksum);
	int writeRomData(std::span<const uint8_t> buff);
	int readRomData(std::span<uint8_t> buff);

	int connectDevice(uint16_t vid, uint16_t pid);
	int getDeviceCount(uint16_t vid, uint16_t pid);
	int openDevice(uint16_t vid, uint16_t pid);
	int iapCommand(std::span<const uint8_t> buff);
	int iapSendCommand(std::span<const uint8_t> buff);
	int iapRecvStatus(std::span<uint8_t> buff);
	int iapSendData(std::span<const uint8_t> buff);
	int iapRecvData(std::span<uint8_t> buff);
	int set32Le(std::span<uint8_t> buff, uint32_t value);
	std::optional<uint32_t> get32Le(std::span<const uint8_t> buff);
	std::optional<uint32_t> getChecksum(std::span<const uint8_t> buff);
	int releaseResource();

	std::function<void(size_t, size_t)> m_progressCallback;
	std::vector<uint8_t> m_hidInterfaces;
	LibusbDeviceHandlePtr m_devHandlePtr;
	int m_deviceStatus;
};
}

#endif
