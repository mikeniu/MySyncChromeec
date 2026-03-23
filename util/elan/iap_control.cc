// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iap_control.h"
#include "utility.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>

namespace elan
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
	libusb_initialized_ = false;
	device_status_ = -1;
}

IapControl::~IapControl()
{
	device_status_ = -1;
	ReleaseResource();

	if (libusb_initialized_) {
		libusb_initialized_ = false;
		libusb_exit(nullptr);
	}
}

int IapControl::Initialize(uint16_t vid, uint16_t pid)
{
	ReleaseResource();

	if (!libusb_initialized_) {
		int err_code = libusb_init(nullptr);
		if (err_code != 0) {
			LogInfo("[IAP] {} - libusb_init failed with code: {} ({})\n",
				__func__, err_code,
				libusb_error_name(err_code));
			return -1;
		}

		libusb_initialized_ = true;
	}

	device_status_ = ConnectDevice(vid, pid);
	if (device_status_ == -1) {
		LogInfo("[IAP] {} - device not found\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::RunIapProcess(std::span<const uint8_t> rom)
{
	// abort process
	if (AbortProcess()) {
		LogInfo("[IAP] {} - abort process fail\n", __func__);
		return -1;
	}

	// get fw version
	auto fw_ver_info = GetFwVersion();
	if (fw_ver_info == std::nullopt) {
		LogInfo("[IAP] {} - get fw version fail\n", __func__);
		return -1;
	}

	// fw ver start from 0x10 is only for Google
	uint8_t fw_ver_google = fw_ver_info->fw_ver >> 8;
	if (fw_ver_google != kGoogleFwVerPrefix) {
		LogInfo("[IAP] {} - boot code is not support this project\n",
			__func__);
		return -1;
	}

	// run iap
	uint8_t iap_mode = 0;
	if (RunIap(iap_mode)) {
		LogInfo("[IAP] {} - run iap fail\n", __func__);
		return -1;
	}

	// write rom
	LogInfo("[IAP] {} - write rom data\n", __func__);

	uint32_t rom_addr = 0x0000;
	if (SetRomAddress(rom_addr)) {
		LogInfo("[IAP] {} - set (write) rom address fail\n", __func__);
		return -1;
	}

	uint32_t rom_length = rom.size();
	if (WriteRom(rom_length)) {
		LogInfo("[IAP] {} - write rom command fail\n", __func__);
		return -1;
	}

	if (WriteRomData(rom)) {
		LogInfo("[IAP] {} - write rom data fail\n", __func__);
		return -1;
	}

	uint32_t checksum = 0x0000;
	if (auto result = GetChecksum(rom)) {
		checksum = *result;
	} else {
		LogInfo("[IAP] {} - calculate (write rom) checksum fail\n",
			__func__);
		return -1;
	}

	if (WriteRomFinish(checksum)) {
		LogInfo("[IAP] {} - write rom finish fail\n", __func__);
		return -1;
	}

	// read rom
	LogInfo("[IAP] {} - verify rom data\n", __func__);
	if (SetRomAddress(rom_addr)) {
		LogInfo("[IAP] {} - set (read) rom address fail\n", __func__);
		return -1;
	}

	if (ReadRom(rom_length)) {
		LogInfo("[IAP] {} - read rom command fail\n", __func__);
		return -1;
	}

	std::vector<uint8_t> rom_read(rom.size());
	if (ReadRomData(rom_read)) {
		LogInfo("[IAP] {} - read rom data fail\n", __func__);
		return -1;
	}

	if (auto result = GetChecksum(rom_read)) {
		checksum = *result;
	} else {
		LogInfo("[IAP] {} - calculate (read rom) checksum fail\n",
			__func__);
		return -1;
	}

	if (ReadRomFinish(checksum)) {
		LogInfo("[IAP] {} - read rom finish fail\n", __func__);
		return -1;
	}

	// compare data
	if (!std::ranges::equal(rom, rom_read)) {
		LogInfo("[IAP] {} - verify rom data fail\n", __func__);
		return -1;
	}

	// finish iap
	if (auto result = GetChecksum(rom)) {
		checksum = *result;
	} else {
		LogInfo("[IAP] {} - calculate (finish iap) checksum fail\n",
			__func__);
		return -1;
	}

	if (FinishIap(checksum)) {
		LogInfo("[IAP] {} - finish iap fail\n", __func__);
		return -1;
	}

	LogInfo("[IAP] {} - iap success\n", __func__);

	if (SoftwareReset()) {
		LogInfo("[IAP] {} - sw reset fail\n", __func__);
		return -1;
	}

	LogInfo("[IAP] {} - sw reset\n", __func__);

	return 0;
}

void IapControl::SetProgressCallback(
	std::function<void(size_t, size_t)> callback)
{
	progress_callback_ = callback;
}

int IapControl::RunIap(uint8_t iap_mode)
{
	uint8_t data_send[kEp1Length] = {};

	data_send[0] = kCmdRunIap;
	data_send[4] = iap_mode;

	int result = IapCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::FinishIap(uint32_t checksum)
{
	uint8_t data_send[kEp1Length] = {};

	data_send[0] = kCmdFinishedIap;

	if (Set32Le(std::span<uint8_t>(data_send + 4, 4), checksum)) {
		LogInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	int result = IapCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::SoftwareReset()
{
	uint8_t data_send[kEp1Length] = {};

	data_send[0] = kCmdSoftwareReset;

	int result = IapCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

std::optional<FwVerInfo> IapControl::GetFwVersion()
{
	uint8_t data_send[kEp1Length] = {};
	uint8_t stat_recv[kEp2Length] = {};
	int result = 0;

	data_send[0] = kCmdGetFwVersion;

	result = IapSendCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - send command fail\n", __func__);
		return std::nullopt;
	}

	result = IapRecvStatus(stat_recv);
	if (result) {
		LogInfo("[IAP] {} - receive status fail\n", __func__);
		return std::nullopt;
	}

	if (stat_recv[0] != data_send[0]) {
		LogInfo("[IAP] {} - return command code does not match (0x{:02X})\n",
			__func__, stat_recv[0]);
		return std::nullopt;
	}

	if (stat_recv[1] != kErrStaSuccess) {
		LogInfo("[IAP] {} - fail, error code (0x{:02X})\n", __func__,
			stat_recv[1]);
		return std::nullopt;
	}

	auto fw_ver = Get32Le(std::span<const uint8_t>(stat_recv + 4, 4));

	if (fw_ver == std::nullopt) {
		LogInfo("[IAP] {} -  decode fw version fail\n", __func__);
		return std::nullopt;
	}

	LogInfo("[IAP] {} - fw version: 0x{:08X}, boot type: 0x{:02X}\n",
		__func__, *fw_ver, stat_recv[2]);

	return FwVerInfo{ .fw_ver = *fw_ver, .boot_type = stat_recv[2] };
}

int IapControl::AbortProcess()
{
	uint8_t data_send[kEp1Length] = {};

	data_send[0] = kCmdAbortProcess;

	int result = IapCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::WriteRom(uint32_t length)
{
	uint8_t data_send[kEp1Length] = {};

	data_send[0] = kCmdWriteRom;

	if (Set32Le(std::span<uint8_t>(data_send + 4, 4), length)) {
		LogInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	int result = IapCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::WriteRomFinish(uint32_t checksum)
{
	uint8_t data_send[kEp1Length] = {};

	data_send[0] = kCmdWriteRomFinish;

	if (Set32Le(std::span<uint8_t>(data_send + 4, 4), checksum)) {
		LogInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	int result = IapCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::SetRomAddress(uint32_t address)
{
	uint8_t data_send[kEp1Length] = {};

	data_send[0] = kCmdSetRomAddress;

	if (Set32Le(std::span<uint8_t>(data_send + 4, 4), address)) {
		LogInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	int result = IapCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::ReadRom(uint32_t length)
{
	uint8_t data_send[kEp1Length] = {};

	data_send[0] = kCmdReadRom;

	if (Set32Le(std::span<uint8_t>(data_send + 4, 4), length)) {
		LogInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	int result = IapCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::ReadRomFinish(uint32_t checksum)
{
	uint8_t data_send[kEp1Length] = {};

	data_send[0] = kCmdReadRomFinish;

	if (Set32Le(std::span<uint8_t>(data_send + 4, 4), checksum)) {
		LogInfo("[IAP] {} - set value fail\n", __func__);
		return -1;
	}

	int result = IapCommand(data_send);
	if (result) {
		LogInfo("[IAP] {} - command fail\n", __func__);
		return -1;
	}

	return 0;
}

int IapControl::WriteRomData(std::span<const uint8_t> buff)
{
	if (buff.empty())
		return -1;

	size_t offset = 0;
	const size_t page_size = kEp3Length;

	while (offset < buff.size()) {
		// Calculate remaining bytes to avoid overflow in the final
		// chunk
		size_t current_chunk_size =
			std::min(page_size, buff.size() - offset);

		// Create a view of the current chunk
		auto chunk = buff.subspan(offset, current_chunk_size);

		if (IapSendData(chunk)) {
			LogInfo("[IAP] {} - fail, count: {}\n", __func__,
				offset);
			return -1;
		}

		offset += chunk.size();

		if (progress_callback_) {
			progress_callback_(offset, buff.size());
		}
	}

	return 0;
}

int IapControl::ReadRomData(std::span<uint8_t> buff)
{
	if (buff.empty())
		return -1;

	size_t offset = 0;
	const size_t page_size = kEp4Length;

	while (offset < buff.size()) {
		size_t current_chunk_size =
			std::min(page_size, buff.size() - offset);

		// Create a mutable view of the current chunk
		auto chunk = buff.subspan(offset, current_chunk_size);

		if (IapRecvData(chunk)) {
			LogInfo("[IAP] {} - fail, count: {}\n", __func__,
				offset);
			return -1;
		}

		offset += chunk.size();

		if (progress_callback_) {
			progress_callback_(offset, buff.size());
		}
	}

	return 0;
}

int IapControl::ConnectDevice(uint16_t vid, uint16_t pid)
{
	// We only care if AT LEAST one device exists.
	if (GetDeviceCount(vid, pid) <= 0) {
		LogInfo("[IAP] {} - device not found\n", __func__);
		return -1;
	}

	// Try to open it
	if (OpenDevice(vid, pid) != 0) {
		LogInfo("[IAP] {} - failed to open and claim usb device\n",
			__func__);
		return -1;
	}

	return 0; // Success!
}

int IapControl::GetDeviceCount(uint16_t vid, uint16_t pid)
{
	libusb_device **raw_list = nullptr;
	ssize_t usb_dev_count = libusb_get_device_list(nullptr, &raw_list);
	LibusbDeviceListPtr list(raw_list);

	if (usb_dev_count < 0) {
		LogInfo("[IAP] {} - get device list failed\n", __func__);
		return -1;
	}

	int count = 0;
	for (ssize_t i = 0; i < usb_dev_count; i++) {
		struct libusb_device_descriptor desc = { 0 };
		if (libusb_get_device_descriptor(list.get()[i], &desc) == 0) {
			if (desc.idVendor == vid && desc.idProduct == pid) {
				count++;
			}
		}
	}

	return count;
}

int IapControl::OpenDevice(uint16_t vid, uint16_t pid)
{
	libusb_device **raw_list = nullptr;
	ssize_t usb_dev_count = libusb_get_device_list(nullptr, &raw_list);
	LibusbDeviceListPtr list(raw_list);
	int err_code = 0;

	if (usb_dev_count < 0) {
		LogInfo("[IAP] {} - get device list failed\n", __func__);
		return -1;
	}

	// 1. Find the target device
	libusb_device *found_dev = nullptr;
	for (ssize_t i = 0; i < usb_dev_count; i++) {
		struct libusb_device_descriptor desc = { 0 };
		if (libusb_get_device_descriptor(list.get()[i], &desc) == 0) {
			if (desc.idVendor == vid && desc.idProduct == pid) {
				LogInfo("[IAP] {} - find device vid: 0x{:04X}, device pid: 0x{:04X}\n",
					__func__, vid, pid);
				found_dev = list.get()[i];
				break;
			}
		}
	}

	if (!found_dev) {
		LogInfo("[IAP] {} - device not found\n", __func__);
		return -1;
	}

	// 2. Read configuration and validate HID interface
	libusb_config_descriptor *config = nullptr;
	err_code = libusb_get_config_descriptor(found_dev, 0, &config);
	if (err_code != 0) {
		LogInfo("[IAP] {} - get config descriptor failed, error code: {} ({})\n",
			__func__, err_code, libusb_error_name(err_code));
		return -1;
	}

	hid_interfaces_.clear();

	for (int i = 0; i < config->bNumInterfaces; i++) {
		const struct libusb_interface *inter = &config->interface[i];
		for (int j = 0; j < inter->num_altsetting; j++) {
			const struct libusb_interface_descriptor *alt =
				&inter->altsetting[j];

			if (alt->bInterfaceClass != LIBUSB_CLASS_HID) {
				continue;
			}

			hid_interfaces_.push_back(alt->bInterfaceNumber);

			break; // Found the HID interface, stop searching
		}
	}

	libusb_free_config_descriptor(config);

	if (hid_interfaces_.empty()) {
		LogInfo("[IAP] {} - HID interface not found\n", __func__);
		return -1;
	}

	if (hid_interfaces_.size() != kCountInterface) {
		LogInfo("[IAP] {} - Expected {} HID interface, but found {}\n",
			__func__, kCountInterface, hid_interfaces_.size());
		return -1;
	}

	// 3. Claim Device and Detach Kernel Drivers
	libusb_device_handle *handle = nullptr;
	err_code = libusb_open(found_dev, &handle);
	if (err_code != 0) {
		LogInfo("[IAP] {} - open device failed, error code: {} ({})\n",
			__func__, err_code, libusb_error_name(err_code));
		return -1;
	}

	dev_handle_ptr_.reset(handle);

	for (size_t i = 0; i < hid_interfaces_.size(); i++) {
		if (libusb_kernel_driver_active(dev_handle_ptr_.get(),
						hid_interfaces_[i])) {
			libusb_detach_kernel_driver(dev_handle_ptr_.get(),
						    hid_interfaces_[i]);

			hid_detach_interfaces_.push_back(hid_interfaces_[i]);
			if (kDebugLog) {
				LogInfo("[IAP] {} - detach kernel driver for interface ({})\n",
					__func__, hid_interfaces_[i]);
			}
		}

		err_code = libusb_claim_interface(dev_handle_ptr_.get(),
						  hid_interfaces_[i]);
		if (err_code != 0) {
			LogInfo("[IAP] {} - claim interface ({}) failed, error code: {} ({})\n",
				__func__, hid_interfaces_[i], err_code,
				libusb_error_name(err_code));
			ReleaseResource();
			return -1;
		}
	}

	return 0;
}

int IapControl::IapCommand(std::span<const uint8_t> buff)
{
	uint8_t stat_recv[kEp2Length] = {};
	int result = 0;

	result = IapSendCommand(buff);
	if (result) {
		LogInfo("[IAP] {} - send command fail\n", __func__);
		return -1;
	}

	result = IapRecvStatus(stat_recv);
	if (result) {
		LogInfo("[IAP] {} - receive status fail\n", __func__);
		return -1;
	}

	if (stat_recv[0] != buff[0]) {
		LogInfo("[IAP] {} - return command code does not match (0x{:02X})\n",
			__func__, stat_recv[0]);
		return -1;
	}

	if (stat_recv[1] != kErrStaSuccess) {
		auto debug_val =
			Get32Le(std::span<const uint8_t>(stat_recv + 4, 4));
		auto debug_str =
			debug_val ? std::format("0x{:08X}", *debug_val) : "N/A";

		LogInfo("[IAP] {} - fail, error code: 0x{:02X}, value: {}\n",
			__func__, stat_recv[1], debug_str);
		return -1;
	}

	if (kDebugLog)
		LogInfo("[IAP] {} - success\n", __func__);

	return result;
}

int IapControl::IapSendCommand(std::span<const uint8_t> buff)
{
	if (!dev_handle_ptr_) {
		LogInfo("[IAP] {} - handle is null\n", __func__);
		return -1;
	}

	if (buff.size() == 0) {
		LogInfo("[IAP] {} - buffer is null\n", __func__);
		return -1;
	}

	uint8_t ep_out = kEp1Addr;
	int transferred = 0;
	int err_code = 0;

	err_code = libusb_interrupt_transfer(
		dev_handle_ptr_.get(), ep_out,
		const_cast<unsigned char *>(buff.data()),
		static_cast<int>(buff.size()), &transferred,
		kTimeoutTransferMs);

	if (err_code != 0) {
		LogInfo("[IAP] {} - fail, error code: {} ({})\n", __func__,
			err_code, libusb_error_name(err_code));
		return -1;
	}

	if (transferred != static_cast<int>(buff.size())) {
		LogInfo("[IAP] {} - fail, data transferred {} is less than expected\n",
			__func__, transferred);
		return -1;
	}

	if (kDebugLog)
		LogInfo("[IAP] {} - transfer: {}\n", __func__, transferred);

	return 0;
}

int IapControl::IapRecvStatus(std::span<uint8_t> buff)
{
	if (!dev_handle_ptr_) {
		LogInfo("[IAP] {} - handle is null\n", __func__);
		return -1;
	}

	if (buff.size() == 0) {
		LogInfo("[IAP] {} - buffer is null\n", __func__);
		return -1;
	}

	uint8_t ep_in = kEp2Addr;
	int transferred = 0;
	int err_code = 0;

	err_code = libusb_interrupt_transfer(dev_handle_ptr_.get(), ep_in,
					     buff.data(),
					     static_cast<int>(buff.size()),
					     &transferred, kTimeoutTransferMs);

	if (err_code != 0) {
		LogInfo("[IAP] {} - fail, error code: {} ({})\n", __func__,
			err_code, libusb_error_name(err_code));
		return -1;
	}

	if (transferred != static_cast<int>(buff.size())) {
		LogInfo("[IAP] {} - fail, data transferred {} is less than expected\n",
			__func__, transferred);
		return -1;
	}

	if (kDebugLog)
		LogInfo("[IAP] {} - transfer: {}\n", __func__, transferred);

	return 0;
}

int IapControl::IapSendData(std::span<const uint8_t> buff)
{
	if (!dev_handle_ptr_) {
		LogInfo("[IAP] {} - handle is null\n", __func__);
		return -1;
	}

	if (buff.size() == 0) {
		LogInfo("[IAP] {} - buffer is null\n", __func__);
		return -1;
	}

	uint8_t ep_out = kEp3Addr;
	int transferred = 0;
	int err_code = 0;

	err_code = libusb_interrupt_transfer(
		dev_handle_ptr_.get(), ep_out,
		const_cast<unsigned char *>(buff.data()),
		static_cast<int>(buff.size()), &transferred,
		kTimeoutTransferMs);

	if (err_code != 0) {
		LogInfo("[IAP] {} - fail, error code: {} ({})\n", __func__,
			err_code, libusb_error_name(err_code));
		return -1;
	}

	if (transferred != static_cast<int>(buff.size())) {
		LogInfo("[IAP] {} - fail, data transferred {} is less than expected\n",
			__func__, transferred);
		return -1;
	}

	return 0;
}

int IapControl::IapRecvData(std::span<uint8_t> buff)
{
	if (!dev_handle_ptr_) {
		LogInfo("[IAP] {} - handle is null\n", __func__);
		return -1;
	}

	if (buff.size() == 0) {
		LogInfo("[IAP] {} - buffer is null\n", __func__);
		return -1;
	}

	uint8_t ep_in = kEp4Addr;
	int transferred = 0;
	int err_code = 0;

	err_code = libusb_interrupt_transfer(dev_handle_ptr_.get(), ep_in,
					     buff.data(),
					     static_cast<int>(buff.size()),
					     &transferred, kTimeoutTransferMs);

	if (err_code != 0) {
		LogInfo("[IAP] {} - fail, error code: {} ({})\n", __func__,
			err_code, libusb_error_name(err_code));
		return -1;
	}

	if (transferred != static_cast<int>(buff.size())) {
		LogInfo("[IAP] {} - fail, data transferred {} is less than expected\n",
			__func__, transferred);
		return -1;
	}

	return 0;
}

int IapControl::Set32Le(std::span<uint8_t> buff, uint32_t value)
{
	if (buff.size() < sizeof(value)) {
		return -1;
	}

	if constexpr (std::endian::native == std::endian::big) {
		value = std::byteswap(value);
	}

	std::memcpy(buff.data(), &value, sizeof(value));

	return 0;
}

std::optional<uint32_t> IapControl::Get32Le(std::span<const uint8_t> buff)
{
	uint32_t value;

	if (buff.size() < sizeof(value)) {
		return std::nullopt;
	}

	std::memcpy(&value, buff.data(), sizeof(value));

	if constexpr (std::endian::native == std::endian::big) {
		value = std::byteswap(value);
	}

	return value;
}

std::optional<uint32_t> IapControl::GetChecksum(std::span<const uint8_t> buff)
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

int IapControl::ReleaseResource()
{
	if (dev_handle_ptr_ == nullptr)
		return 0;

	if (hid_interfaces_.size() == 0)
		return 0;

	int ret = 0;
	int err_code = 0;

	if (kDebugLog)
		LogInfo("[IAP] {} - start\n", __func__);

	for (size_t i = 0; i < hid_interfaces_.size(); i++) {
		err_code = libusb_release_interface(dev_handle_ptr_.get(),
						    hid_interfaces_[i]);
		if (err_code != 0) {
			LogInfo("[IAP] {} - release interface ({}) failed, "
				"error code: {} ({})\n",
				__func__, hid_interfaces_[i], err_code,
				libusb_error_name(err_code));
			ret = -1;
		}
	}

	for (size_t i = 0; i < hid_detach_interfaces_.size(); i++) {
		err_code = libusb_attach_kernel_driver(
			dev_handle_ptr_.get(), hid_detach_interfaces_[i]);
		if (err_code != 0) {
			LogInfo("[IAP] {} - reattach kernel driver for interface ({}) failed,"
				"error code: {} ({})\n",
				__func__, hid_detach_interfaces_[i], err_code,
				libusb_error_name(err_code));
			ret = -1;
		}
	}

	hid_detach_interfaces_.clear();

	dev_handle_ptr_.reset();

	if (kDebugLog)
		LogInfo("[IAP] {} - exit\n", __func__);

	return ret;
}
}
