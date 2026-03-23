// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iapcontrol.h"
#include "utility.h"

#include <algorithm>
#include <cstring>

namespace ElanApp
{
namespace
{

	// ---------------------------------------------------------
	// Internal Constants
	// ---------------------------------------------------------
	constexpr bool kDebugLog = false;
	constexpr size_t kCountInterface = 4;
	constexpr int kTimeoutTransferMs = 1000;
	constexpr uint8_t kGoogleFwVerPrefix = 0x10;

	// ---------------------------------------------------------
	// Endpoints & Lengths
	// ---------------------------------------------------------
	constexpr uint8_t kEp1Addr = 0x01;
	constexpr uint8_t kEp2Addr = 0x82;
	constexpr uint8_t kEp3Addr = 0x03;
	constexpr uint8_t kEp4Addr = 0x84;

	constexpr size_t kEp1Length = 8;
	constexpr size_t kEp2Length = 8;
	constexpr size_t kEp3Length = 64;
	constexpr size_t kEp4Length = 64;

	// ---------------------------------------------------------
	// Command Codes
	// ---------------------------------------------------------
	constexpr uint8_t kCmdRunIap = 0x20;
	constexpr uint8_t kCmdFinishedIap = 0x21;
	constexpr uint8_t kCmdSoftwareReset = 0x22;
	constexpr uint8_t kCmdGetFwVersion = 0x41;
	constexpr uint8_t kCmdAbortProcess = 0x48;
	constexpr uint8_t kCmdWriteRom = 0xA0;
	constexpr uint8_t kCmdWriteRomFinish = 0xA1;
	constexpr uint8_t kCmdSetRomAddress = 0xA4;
	constexpr uint8_t kCmdReadRom = 0xE0;
	constexpr uint8_t kCmdReadRomFinish = 0xE1;

	// ---------------------------------------------------------
	// Status Codes
	// ---------------------------------------------------------
	// constexpr uint8_t kErrStaReady = 0x00;
	constexpr uint8_t kErrStaSuccess = 0x01;

} // namespace

IapControl::IapControl()
{
	m_libusbInitialized = false;
	m_deviceStatus = -1;
}

IapControl::~IapControl()
{
	m_deviceStatus = -1;
	releaseResource();

	if (m_libusbInitialized) {
		m_libusbInitialized = false;
		libusb_exit(nullptr);
	}
}

int IapControl::initialize(uint16_t vid, uint16_t pid)
{
	releaseResource();

	if (!m_libusbInitialized) {
		int errCode = libusb_init(nullptr);
		if (errCode != 0) {
			logInfo("[IAP] {} - libusb_init failed with code: {} ({})\n",
				__func__, errCode, libusb_error_name(errCode));
			return -1;
		}

		m_libusbInitialized = true;
	}

	m_deviceStatus = connectDevice(vid, pid);
	if (m_deviceStatus == -1) {
		logInfo("[IAP] {} - device not found\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::runIapProcess(std::span<const uint8_t> rom)
{
	std::vector<uint8_t> romRead(rom.size());
	uint32_t romAddr = 0x0000;
	uint32_t romLength = rom.size();
	uint32_t checksum = 0x0000;
	uint8_t fwVer_Google = 0;
	uint8_t iapMode = 0;

	// abort process
	if (abortProcess()) {
		logInfo("[IAP] {} - abort process fail\n", __func__);
		return -1;
	}

	// get fw version
	auto fwFwInfo = getFwVersion();
	if (fwFwInfo == std::nullopt) {
		logInfo("[IAP] {} - get fw version fail\n", __func__);
		return -1;
	}

	// fw ver start from 0x10 is only for Google
	fwVer_Google = fwFwInfo->fwVer >> 8;
	if (fwVer_Google != kGoogleFwVerPrefix) {
		logInfo("[IAP] {} - boot code is not support this project\n",
			__func__);
		return -1;
	}

	// run iap
	if (runIap(iapMode)) {
		logInfo("[IAP] {} - run iap fail\n", __func__);
		return -1;
	}

	// write rom
	logInfo("[IAP] {} - write rom data\n", __func__);
	if (setRomAddress(romAddr)) {
		logInfo("[IAP] {} - set (write) rom address fail\n", __func__);
		return -1;
	}

	if (writeRom(romLength)) {
		logInfo("[IAP] {} - write rom command fail\n", __func__);
		return -1;
	}

	if (writeRomData(rom)) {
		logInfo("[IAP] {} - write rom data fail\n", __func__);
		return -1;
	}

	if (auto result = getChecksum(rom)) {
		checksum = *result;
	} else {
		logInfo("[IAP] {} - calculate (write rom) checksum fail\n",
			__func__);
		return -1;
	}

	if (writeRomFinish(checksum)) {
		logInfo("[IAP] {} - write rom finish fail\n", __func__);
		return -1;
	}

	// read rom
	logInfo("[IAP] {} - verify rom data\n", __func__);
	if (setRomAddress(romAddr)) {
		logInfo("[IAP] {} - set (read) rom address fail\n", __func__);
		return -1;
	}

	if (readRom(romLength)) {
		logInfo("[IAP] {} - read rom command fail\n", __func__);
		return -1;
	}

	if (readRomData(romRead)) {
		logInfo("[IAP] {} - read rom data fail\n", __func__);
		return -1;
	}

	if (auto result = getChecksum(romRead)) {
		checksum = *result;
	} else {
		logInfo("[IAP] {} - calculate (read rom) checksum fail\n",
			__func__);
		return -1;
	}

	if (readRomFinish(checksum)) {
		logInfo("[IAP] {} - read rom finish fail\n", __func__);
		return -1;
	}

	// compare data
	if (!std::ranges::equal(rom, romRead)) {
		logInfo("[IAP] {} - verify rom data fail\n", __func__);
		return -1;
	}

	// finish iap
	if (auto result = getChecksum(rom)) {
		checksum = *result;
	} else {
		logInfo("[IAP] {} - calculate (finish iap) checksum fail\n",
			__func__);
		return -1;
	}

	if (finishIap(checksum)) {
		logInfo("[IAP] {} - finish iap fail\n", __func__);
		return -1;
	}

	logInfo("[IAP] {} - iap success\n", __func__);

	if (softwareReset()) {
		logInfo("[IAP] {} - sw reset fail\n", __func__);
		return -1;
	}

	logInfo("[IAP] {} - sw reset\n", __func__);

	return 0;
}

void IapControl::setProgressCallback(
	std::function<void(size_t, size_t)> callback)
{
	m_progressCallback = callback;
}

int IapControl::runIap(uint8_t iapMode)
{
	uint8_t dataSend[kEp1Length] = {};
	int result = 0;

	dataSend[0] = kCmdRunIap;
	dataSend[4] = iapMode;

	result = iapCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::finishIap(uint32_t checksum)
{
	uint8_t dataSend[kEp1Length] = {};
	int result = 0;

	dataSend[0] = kCmdFinishedIap;

	if (set32Le(std::span<uint8_t>(dataSend + 4, 4), checksum)) {
		logInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	result = iapCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::softwareReset()
{
	uint8_t dataSend[kEp1Length] = {};
	int result = 0;

	dataSend[0] = kCmdSoftwareReset;

	result = iapCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

std::optional<FWVERINFO> IapControl::getFwVersion()
{
	FWVERINFO fwVerInfo;
	uint8_t dataSend[kEp1Length] = {};
	uint8_t statRecv[kEp2Length] = {};
	int result = 0;

	dataSend[0] = kCmdGetFwVersion;

	result = iapSendCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - send command fail\n", __func__);
		return std::nullopt;
	}

	result = iapRecvStatus(statRecv);
	if (result) {
		logInfo("[IAP] {} - receive status fail\n", __func__);
		return std::nullopt;
	}

	if (statRecv[0] != dataSend[0]) {
		logInfo("[IAP] {} - return command code does not match (0x{:02X})\n",
			__func__, statRecv[0]);
		return std::nullopt;
	}

	if (statRecv[1] != kErrStaSuccess) {
		logInfo("[IAP] {} - fail, error code (0x{:02X})\n", __func__,
			statRecv[1]);
		return std::nullopt;
	}

	auto fwVer = get32Le(std::span<const uint8_t>(statRecv + 4, 4));

	if (fwVer == std::nullopt) {
		logInfo("[IAP] {} -  decode fw version fail\n", __func__);
		return std::nullopt;
	}

	fwVerInfo.fwVer = *fwVer;

	fwVerInfo.bootType = statRecv[2];

	logInfo("[IAP] {} - fw version: 0x{:08X}, boot type: 0x{:02X}\n",
		__func__, fwVerInfo.fwVer, fwVerInfo.bootType);

	return fwVerInfo;
}

int IapControl::abortProcess()
{
	uint8_t dataSend[kEp1Length] = {};
	int result = 0;

	dataSend[0] = kCmdAbortProcess;

	result = iapCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::writeRom(uint32_t length)
{
	uint8_t dataSend[kEp1Length] = {};
	int result = 0;

	dataSend[0] = kCmdWriteRom;

	if (set32Le(std::span<uint8_t>(dataSend + 4, 4), length)) {
		logInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	result = iapCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::writeRomFinish(uint32_t checksum)
{
	uint8_t dataSend[kEp1Length] = {};
	int result = 0;

	dataSend[0] = kCmdWriteRomFinish;

	if (set32Le(std::span<uint8_t>(dataSend + 4, 4), checksum)) {
		logInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	result = iapCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::setRomAddress(uint32_t address)
{
	uint8_t dataSend[kEp1Length] = {};
	int result = 0;

	dataSend[0] = kCmdSetRomAddress;

	if (set32Le(std::span<uint8_t>(dataSend + 4, 4), address)) {
		logInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	result = iapCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::readRom(uint32_t length)
{
	uint8_t dataSend[kEp1Length] = {};
	int result = 0;

	dataSend[0] = kCmdReadRom;

	if (set32Le(std::span<uint8_t>(dataSend + 4, 4), length)) {
		logInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	result = iapCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::readRomFinish(uint32_t checksum)
{
	uint8_t dataSend[kEp1Length] = {};
	int result = 0;

	dataSend[0] = kCmdReadRomFinish;

	if (set32Le(std::span<uint8_t>(dataSend + 4, 4), checksum)) {
		logInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	result = iapCommand(dataSend);
	if (result) {
		logInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::writeRomData(std::span<const uint8_t> buff)
{
	size_t offset = 0;
	const size_t pageSize = kEp3Length;

	if (buff.empty())
		return -1;

	while (offset < buff.size()) {
		// Calculate remaining bytes to avoid overflow in the final
		// chunk
		size_t currentChunkSize =
			std::min(pageSize, buff.size() - offset);

		// Create a view of the current chunk
		auto chunk = buff.subspan(offset, currentChunkSize);

		if (iapSendData(chunk)) {
			logInfo("[IAP] {} - fail, count: {}\n", __func__,
				offset);
			return -1;
		}

		offset += chunk.size();

		if (m_progressCallback) {
			m_progressCallback(offset, buff.size());
		}
	}

	return 0;
}

int IapControl::readRomData(std::span<uint8_t> buff)
{
	size_t offset = 0;
	const size_t pageSize = kEp4Length;

	if (buff.empty())
		return -1;

	while (offset < buff.size()) {
		size_t currentChunkSize =
			std::min(pageSize, buff.size() - offset);

		// Create a mutable view of the current chunk
		auto chunk = buff.subspan(offset, currentChunkSize);

		if (iapRecvData(chunk)) {
			logInfo("[IAP] {} - fail, count: {}\n", __func__,
				offset);
			return -1;
		}

		offset += chunk.size();

		if (m_progressCallback) {
			m_progressCallback(offset, buff.size());
		}
	}

	return 0;
}

int IapControl::connectDevice(uint16_t vid, uint16_t pid)
{
	// We only care if AT LEAST one device exists.
	if (getDeviceCount(vid, pid) <= 0) {
		logInfo("[IAP] {} - device not found\n", __func__);
		return -1;
	}

	// Try to open it
	if (openDevice(vid, pid) != 0) {
		logInfo("[IAP] {} - failed to open and claim usb device\n",
			__func__);
		return -1;
	}

	return 0; // Success!
}

int IapControl::getDeviceCount(uint16_t vid, uint16_t pid)
{
	libusb_device **rawList = nullptr;
	ssize_t usbDeviceCount = libusb_get_device_list(nullptr, &rawList);
	LibusbDeviceListPtr list(rawList);

	if (usbDeviceCount < 0) {
		logInfo("[IAP] {} - get device list failed\n", __func__);
		return -1;
	}

	int count = 0;
	for (ssize_t i = 0; i < usbDeviceCount; i++) {
		struct libusb_device_descriptor desc = { 0 };
		if (libusb_get_device_descriptor(list.get()[i], &desc) == 0) {
			if (desc.idVendor == vid && desc.idProduct == pid) {
				count++;
			}
		}
	}

	return count;
}

int IapControl::openDevice(uint16_t vid, uint16_t pid)
{
	libusb_device **rawList = nullptr;
	ssize_t usbDeviceCount = libusb_get_device_list(nullptr, &rawList);
	LibusbDeviceListPtr list(rawList);
	int errCode = 0;

	if (usbDeviceCount < 0) {
		logInfo("[IAP] {} - get device list failed\n", __func__);
		return -1;
	}

	// 1. Find the target device
	libusb_device *found_dev = nullptr;
	for (ssize_t i = 0; i < usbDeviceCount; i++) {
		struct libusb_device_descriptor desc = { 0 };
		if (libusb_get_device_descriptor(list.get()[i], &desc) == 0) {
			if (desc.idVendor == vid && desc.idProduct == pid) {
				logInfo("[IAP] {} - find device vid: 0x{:04X}, device pid: 0x{:04X}\n",
					__func__, vid, pid);
				found_dev = list.get()[i];
				break;
			}
		}
	}

	if (!found_dev) {
		logInfo("[IAP] {} - device not found\n", __func__);
		return -1;
	}

	// 2. Read configuration and validate HID interface
	libusb_config_descriptor *config = nullptr;
	errCode = libusb_get_config_descriptor(found_dev, 0, &config);
	if (errCode != 0) {
		logInfo("[IAP] {} - get config descriptor failed, error code: {} ({})\n",
			__func__, errCode, libusb_error_name(errCode));
		return -1;
	}

	m_hidInterfaces.clear();

	for (int i = 0; i < config->bNumInterfaces; i++) {
		const struct libusb_interface *inter = &config->interface[i];
		for (int j = 0; j < inter->num_altsetting; j++) {
			const struct libusb_interface_descriptor *alt =
				&inter->altsetting[j];

			if (alt->bInterfaceClass != LIBUSB_CLASS_HID) {
				continue;
			}

			m_hidInterfaces.push_back(alt->bInterfaceNumber);

			break; // Found the HID interface, stop searching
		}
	}

	libusb_free_config_descriptor(config);

	if (m_hidInterfaces.empty()) {
		logInfo("[IAP] {} - HID interface not found\n", __func__);
		return -1;
	}

	if (m_hidInterfaces.size() != kCountInterface) {
		logInfo("[IAP] {} - Expected {} HID interface, but found {}\n",
			__func__, kCountInterface, m_hidInterfaces.size());
		return -1;
	}

	// 3. Claim Device and Detach Kernel Drivers
	libusb_device_handle *handle = nullptr;
	errCode = libusb_open(found_dev, &handle);
	if (errCode != 0) {
		logInfo("[IAP] {} - open device failed, error code: {} ({})\n",
			__func__, errCode, libusb_error_name(errCode));
		return -1;
	}

	m_devHandlePtr.reset(handle);

	for (size_t i = 0; i < m_hidInterfaces.size(); i++) {
		if (libusb_kernel_driver_active(m_devHandlePtr.get(),
						m_hidInterfaces[i])) {
			libusb_detach_kernel_driver(m_devHandlePtr.get(),
						    m_hidInterfaces[i]);

			m_hidDetachInterfaces.push_back(m_hidInterfaces[i]);
			if (kDebugLog) {
				logInfo("[IAP] {} - detach kernel driver for interface ({})\n",
					__func__, m_hidInterfaces[i]);
			}
		}

		errCode = libusb_claim_interface(m_devHandlePtr.get(),
						 m_hidInterfaces[i]);
		if (errCode != 0) {
			logInfo("[IAP] {} - claim interface ({}) failed, error code: {} ({})\n",
				__func__, m_hidInterfaces[i], errCode,
				libusb_error_name(errCode));
			return -1;
		}
	}

	return 0;
}

int IapControl::iapCommand(std::span<const uint8_t> buff)
{
	uint8_t statRecv[kEp2Length] = {};
	int result = 0;

	result = iapSendCommand(buff);
	if (result) {
		logInfo("[IAP] {} - send command fail\n", __func__);
		return -1;
	}

	result = iapRecvStatus(statRecv);
	if (result) {
		logInfo("[IAP] {} - receive status fail\n", __func__);
		return -1;
	}

	if (statRecv[0] != buff[0]) {
		logInfo("[IAP] {} - return command code does not match (0x{:02X})\n",
			__func__, statRecv[0]);
		return -1;
	}

	if (statRecv[1] != kErrStaSuccess) {
		auto debugVal =
			get32Le(std::span<const uint8_t>(statRecv + 4, 4));
		auto debugStr = debugVal ? std::format("0x{:08X}", *debugVal) :
					   "N/A";

		logInfo("[IAP] {} - fail, error code: 0x{:02X}, value: {}\n",
			__func__, statRecv[1], debugStr);
		return -1;
	}

	if (kDebugLog)
		logInfo("[IAP] {} - success\n", __func__);

	return result;
}

int IapControl::iapSendCommand(std::span<const uint8_t> buff)
{
	uint8_t ep_out = kEp1Addr;
	int transferred = 0;
	int errCode = 0;
	int result = 0;

	if (!m_devHandlePtr) {
		logInfo("[IAP] {} - handle is null\n", __func__);
		return -1;
	}

	if (buff.size() == 0) {
		logInfo("[IAP] {} - buffer is null\n", __func__);
		return -1;
	}

	errCode = libusb_interrupt_transfer(
		m_devHandlePtr.get(), ep_out,
		const_cast<unsigned char *>(buff.data()),
		static_cast<int>(buff.size()), &transferred,
		kTimeoutTransferMs);

	if (errCode != 0) {
		logInfo("[IAP] {} - fail, error code: {} ({})\n", __func__,
			errCode, libusb_error_name(errCode));
		return -1;
	}

	if (transferred != static_cast<int>(buff.size())) {
		logInfo("[IAP] {} - fail, data transferred {} is less than expected\n",
			__func__, transferred);
		return -1;
	}

	if (kDebugLog)
		logInfo("[IAP] {} - transfer: {}\n", __func__, transferred);

	return result;
}

int IapControl::iapRecvStatus(std::span<uint8_t> buff)
{
	uint8_t ep_in = kEp2Addr;
	int transferred = 0;
	int errCode = 0;
	int result = 0;

	if (!m_devHandlePtr) {
		logInfo("[IAP] {} - handle is null\n", __func__);
		return -1;
	}

	if (buff.size() == 0) {
		logInfo("[IAP] {} - buffer is null\n", __func__);
		return -1;
	}

	errCode = libusb_interrupt_transfer(m_devHandlePtr.get(), ep_in,
					    buff.data(),
					    static_cast<int>(buff.size()),
					    &transferred, kTimeoutTransferMs);

	if (errCode != 0) {
		logInfo("[IAP] {} - fail, error code: {} ({})\n", __func__,
			errCode, libusb_error_name(errCode));
		return -1;
	}

	if (transferred != static_cast<int>(buff.size())) {
		logInfo("[IAP] {} - fail, data transferred {} is less than expected\n",
			__func__, transferred);
		return -1;
	}

	if (kDebugLog)
		logInfo("[IAP] {} - transfer: {}\n", __func__, transferred);

	return result;
}

int IapControl::iapSendData(std::span<const uint8_t> buff)
{
	uint8_t ep_out = kEp3Addr;
	int transferred = 0;
	int errCode = 0;
	int result = 0;

	if (!m_devHandlePtr) {
		logInfo("[IAP] {} - handle is null\n", __func__);
		return -1;
	}

	if (buff.size() == 0) {
		logInfo("[IAP] {} - buffer is null\n", __func__);
		return -1;
	}

	errCode = libusb_interrupt_transfer(
		m_devHandlePtr.get(), ep_out,
		const_cast<unsigned char *>(buff.data()),
		static_cast<int>(buff.size()), &transferred,
		kTimeoutTransferMs);

	if (errCode != 0) {
		logInfo("[IAP] {} - fail, error code: {} ({})\n", __func__,
			errCode, libusb_error_name(errCode));
		return -1;
	}

	if (transferred != static_cast<int>(buff.size())) {
		logInfo("[IAP] {} - fail, data transferred {} is less than expected\n",
			__func__, transferred);
		return -1;
	}

	return result;
}

int IapControl::iapRecvData(std::span<uint8_t> buff)
{
	uint8_t ep_in = kEp4Addr;
	int transferred = 0;
	int errCode = 0;
	int result = 0;

	if (!m_devHandlePtr) {
		logInfo("[IAP] {} - handle is null\n", __func__);
		return -1;
	}

	if (buff.size() == 0) {
		logInfo("[IAP] {} - buffer is null\n", __func__);
		return -1;
	}

	errCode = libusb_interrupt_transfer(m_devHandlePtr.get(), ep_in,
					    buff.data(),
					    static_cast<int>(buff.size()),
					    &transferred, kTimeoutTransferMs);

	if (errCode != 0) {
		logInfo("[IAP] {} - fail, error code: {} ({})\n", __func__,
			errCode, libusb_error_name(errCode));
		return -1;
	}

	if (transferred != static_cast<int>(buff.size())) {
		logInfo("[IAP] {} - fail, data transferred {} is less than expected\n",
			__func__, transferred);
		return -1;
	}

	return result;
}

int IapControl::set32Le(std::span<uint8_t> buff, uint32_t value)
{
	if (buff.size() < 4) {
		logInfo("[IAP] {} - buff is less than 4 bytes\n", __func__);
		return -1;
	}

	buff[0] = static_cast<uint8_t>(value & 0xFF);
	buff[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
	buff[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
	buff[3] = static_cast<uint8_t>((value >> 24) & 0xFF);

	return 0;
}

std::optional<uint32_t> IapControl::get32Le(std::span<const uint8_t> buff)
{
	if (buff.size() < 4) {
		logInfo("[IAP] {} - buff is less than 4 bytes\n", __func__);
		return std::nullopt;
	}

	return static_cast<uint32_t>(buff[0]) |
	       (static_cast<uint32_t>(buff[1]) << 8) |
	       (static_cast<uint32_t>(buff[2]) << 16) |
	       (static_cast<uint32_t>(buff[3]) << 24);
}

std::optional<uint32_t> IapControl::getChecksum(std::span<const uint8_t> buff)
{
	uint32_t checksum = 0;

	if ((buff.size() % 4) != 0)
		return std::nullopt;

	for (size_t i = 0; i < buff.size(); i += 4) {
		checksum += static_cast<uint32_t>(buff[i]) |
			    (static_cast<uint32_t>(buff[i + 1]) << 8) |
			    (static_cast<uint32_t>(buff[i + 2]) << 16) |
			    (static_cast<uint32_t>(buff[i + 3]) << 24);
	}

	return checksum;
}

int IapControl::releaseResource()
{
	int ret = 0;
	int errCode = 0;

	if (m_devHandlePtr == nullptr)
		return 0;

	if (m_hidInterfaces.size() == 0)
		return 0;

	if (kDebugLog)
		logInfo("[IAP] {} - start\n", __func__);

	for (size_t i = 0; i < m_hidInterfaces.size(); i++) {
		errCode = libusb_release_interface(m_devHandlePtr.get(),
						   m_hidInterfaces[i]);
		if (errCode != 0) {
			logInfo("[IAP] {} - release interface ({}) failed,"
				"error code: {} ({})\n",
				__func__, m_hidInterfaces[i], errCode,
				libusb_error_name(errCode));
			ret = -1;
		}
	}

	for (size_t i = 0; i < m_hidDetachInterfaces.size(); i++) {
		errCode = libusb_attach_kernel_driver(m_devHandlePtr.get(),
						      m_hidDetachInterfaces[i]);
		if (errCode != 0) {
			logInfo("[IAP] {} - reattach kernel driver for interface ({}) failed,"
				"error code: {} ({})\n",
				__func__, m_hidDetachInterfaces[i], errCode,
				libusb_error_name(errCode));
			ret = -1;
		}
	}

	m_hidDetachInterfaces.clear();

	m_devHandlePtr.reset();

	if (kDebugLog)
		logInfo("[IAP] {} - exit\n", __func__);

	return ret;
}
}
