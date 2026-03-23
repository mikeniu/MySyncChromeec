// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iapcontrol.h"

#define DEBUG_LOG 0

#define EP1_LENGTH 8
#define EP2_LENGTH 8
#define EP3_LENGTH 64
#define EP4_LENGTH 64

#define CMD_RUN_IAP 0x20
#define CMD_FINISHED_IAP 0x21
#define CMD_SOFTWARE_RESET 0x22
#define CMD_GET_FW_VERSION 0x41
#define CMD_ABORT_PROCESS 0x48
#define CMD_WRITE_ROM 0xA0
#define CMD_WRITE_ROM_FINISH 0xA1
#define CMD_WRITE_OPTION 0xA2
#define CMD_WRITE_OPTION_FINISH 0xA3
#define CMD_SET_ROM_ADDRESS 0xA4
#define CMD_SET_OPTION_ADDRESS 0xA5
#define CMD_READ_ROM 0xE0
#define CMD_READ_ROM_FINISH 0xE1
#define CMD_READ_OPTION 0xE2
#define CMD_READ_OPTION_FINISH 0xE3

#define ERR_STA_READY 0x00
#define ERR_STA_SUCCESS 0x01

int g_deviceCount = 1;
DEVINFO g_devInfo;

int runIap(uint8_t iapMode)
{
	const char funcName[] = "run iap";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_RUN_IAP;
	dataSend[4] = iapMode;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int finishIap(uint32_t checksum)
{
	const char funcName[] = "finish iap";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_FINISHED_IAP;
	*(uint32_t *)(dataSend + 4) = checksum;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int softwareReset()
{
	const char funcName[] = "software reset";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_SOFTWARE_RESET;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int getFwVersion(uint32_t *pfwVer, uint8_t *pbootType)
{
	const char funcName[] = "get fw ver";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_GET_FW_VERSION;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	*pfwVer = *(uint32_t *)(statRecv + 4);

	*pbootType = statRecv[2];

	printf("[IAP] %s - fw version: 0x%08X, boot type: 0x%02X\n", funcName,
	       *pfwVer, statRecv[2]);

Exit:

	return result;
}

int abortProcess()
{
	const char funcName[] = "abort process";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_ABORT_PROCESS;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int writeRom(uint32_t length)
{
	const char funcName[] = "write rom command";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_WRITE_ROM;
	*(uint32_t *)(dataSend + 4) = length;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int writeRomFinish(uint32_t checksum)
{
	const char funcName[] = "write rom finish";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_WRITE_ROM_FINISH;
	*(uint32_t *)(dataSend + 4) = checksum;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X), checksum from fw: 0x%08X\n",
		       funcName, statRecv[1], *(uint32_t *)(statRecv + 4));
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int setRomAddress(uint32_t address)
{
	const char funcName[] = "set rom address";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_SET_ROM_ADDRESS;
	*(uint32_t *)(dataSend + 4) = address;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int setOptionAddress(uint32_t address)
{
	const char funcName[] = "set option address";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_SET_OPTION_ADDRESS;
	*(uint32_t *)(dataSend + 4) = address;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int readRom(uint32_t length)
{
	const char funcName[] = "read rom command";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_READ_ROM;
	*(uint32_t *)(dataSend + 4) = length;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int readRomFinish(uint32_t checksum)
{
	const char funcName[] = "read rom finish";
	unsigned char dataSend[EP1_LENGTH];
	unsigned char statRecv[EP2_LENGTH];
	int result = 0;

	memset(dataSend, 0x00, sizeof(dataSend));
	memset(statRecv, 0x00, sizeof(statRecv));

	dataSend[0] = CMD_READ_ROM_FINISH;
	*(uint32_t *)(dataSend + 4) = checksum;

	result = iapSendCommand(dataSend, sizeof(dataSend));
	if (result) {
		printf("[IAP] %s - send command fail\n", funcName);
		result = -1;
		goto Exit;
	}

	result = iapRecvStatus(statRecv, sizeof(statRecv));
	if (result) {
		printf("[IAP] %s - receive status fail\n", funcName);
		result = -1;
		goto Exit;
	}

	if (statRecv[0] != dataSend[0]) {
		printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n",
		       funcName, statRecv[0]);
		result = -1;
		goto Exit;
	}

	if (statRecv[1] != ERR_STA_SUCCESS) {
		printf("[IAP] %s - fail, error code (0x%02X)\n", funcName,
		       statRecv[1]);
		result = -1;
		goto Exit;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - success\n", funcName);

Exit:

	return result;
}

int iapSendCommand(unsigned char *pBuff, int length)
{
	const char funcName[] = "send command";
	unsigned char ep_out = g_devInfo.ep1_out;
	int transferred = 0;
	int errCode = 0;
	int result = 0;

	if (g_devInfo.handle == NULL) {
		printf("[IAP] %s - handle is null\n", funcName);
		return -1;
	}

	if (pBuff == NULL) {
		printf("[IAP] %s - buffer is null\n", funcName);
		return -1;
	}

	errCode = libusb_interrupt_transfer(g_devInfo.handle, ep_out, pBuff,
					    length, &transferred,
					    TIMEOUT_TRANSFER);

	if (errCode != 0) {
		printf("[IAP] %s - fail, error code: %d\n", funcName, errCode);
		result = -1;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - transfer: %d\n", funcName, transferred);

	return result;
}

int iapRecvStatus(unsigned char *pBuff, int length)
{
	const char funcName[] = "receive status";
	unsigned char ep_in = g_devInfo.ep2_in;
	int transferred = 0;
	int errCode = 0;
	int result = 0;

	if (g_devInfo.handle == NULL) {
		printf("[IAP] %s - handle is null\n", funcName);
		return -1;
	}

	if (pBuff == NULL) {
		printf("[IAP] %s - buffer is null\n", funcName);
		return -1;
	}

	errCode = libusb_interrupt_transfer(g_devInfo.handle, ep_in, pBuff,
					    length, &transferred,
					    TIMEOUT_TRANSFER);

	if (errCode != 0) {
		printf("[IAP] %s - fail, error code: %d\n", funcName, errCode);
		result = -1;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - transfer: %d\n", funcName, transferred);

	return result;
}

int iapSendData(unsigned char *pBuff, int length)
{
	const char funcName[] = "send data";
	unsigned char ep_out = g_devInfo.ep3_out;
	int newLength = length + 1;
	int transferred = 0;
	int errCode = 0;
	int result = 0;

	if (g_devInfo.handle == NULL) {
		printf("[IAP] %s - handle is null\n", funcName);
		return -1;
	}

	if (pBuff == NULL) {
		printf("[IAP] %s - buffer is null\n", funcName);
		return -1;
	}

	errCode = libusb_interrupt_transfer(g_devInfo.handle, ep_out, pBuff,
					    length, &transferred,
					    TIMEOUT_TRANSFER);

	if (errCode != 0) {
		printf("[IAP] %s - fail, error code: %d\n", funcName, errCode);
		result = -1;
	}

	return result;
}

int iapRecvData(unsigned char *pBuff, int length)
{
	const char funcName[] = "receive data";
	unsigned char ep_in = g_devInfo.ep4_in;
	int newLength = length + 1;
	int transferred = 0;
	int errCode = 0;
	int result = 0;

	if (g_devInfo.handle == NULL) {
		printf("[IAP] %s - handle is null\n", funcName);
		return -1;
	}

	if (pBuff == NULL) {
		printf("[IAP] %s - buffer is null\n", funcName);
		return -1;
	}

	errCode = libusb_interrupt_transfer(g_devInfo.handle, ep_in, pBuff,
					    length, &transferred,
					    TIMEOUT_TRANSFER);

	if (errCode != 0) {
		printf("[IAP] %s - fail, error code: %d\n", funcName, errCode);
		result = -1;
	}

	return result;
}

int connectDevice(unsigned short vid, unsigned short pid)
{
	const char funcName[] = "connect device";
	int count = 0;

	// printf("[Connect Device] Start\n");

	count = getDeviceCount(vid, pid);

	if (DEBUG_LOG)
		printf("[IAP] %s - device count: %d\n", funcName, count);

	if (count > 0) {
		if (g_deviceCount) {
			return DEVSTA_EXISTED;
		}

		g_deviceCount = count;

		return DEVSTA_INSERT;
	}

	if (g_deviceCount) {
		releaseResource();

		g_deviceCount = 0;

		return DEVSTA_REMOVE;
	}

	printf("[IAP] %s - exit\n", funcName);

	return DEVSTA_NON;
}

int getDeviceCount(unsigned short vid, unsigned short pid)
{
	const char funcName[] = "get device count";
	ssize_t usbDeviceCount = 0;
	libusb_device **devs = NULL;
	libusb_device *found_dev = NULL;
	libusb_device_handle *handle = NULL;
	libusb_config_descriptor *config = NULL;
	int count = 0;
	int interface = -1;
	int result;

	libusb_init(NULL);

	usbDeviceCount = libusb_get_device_list(NULL, &devs);

	// find device
	for (ssize_t i = 0; i < usbDeviceCount; i++) {
		struct libusb_device_descriptor desc;
		libusb_get_device_descriptor(devs[i], &desc);

		if (desc.idVendor == vid && desc.idProduct == pid) {
			printf("[IAP] %s - find device vid: 0x%04X, device pid: 0x%04X\n",
			       funcName, vid, pid);
			found_dev = devs[i];
			count++;
			break;
		}
	}

	if (!found_dev) {
		printf("[IAP] %s - device not found\n", funcName);
		return 0;
	}

	// get descriptor
	libusb_get_config_descriptor(found_dev, 0, &config);

	for (int i = 0; i < config->bNumInterfaces; i++) {
		const struct libusb_interface *inter = &config->interface[i];

		for (int j = 0; j < inter->num_altsetting; j++) {
			const struct libusb_interface_descriptor *alt =
				&inter->altsetting[j];

			if (alt == NULL) {
				if (DEBUG_LOG)
					printf("[IAP] %s - alt setting is null\n",
					       funcName);

				continue;
			}

			if (alt->bInterfaceClass == LIBUSB_CLASS_HID) {
				interface = alt->bInterfaceNumber;
				if (DEBUG_LOG)
					printf("[IAP] %s - interface number: %d\n",
					       funcName, interface);

				for (int k = 0; k < alt->bNumEndpoints; k++) {
					const struct libusb_endpoint_descriptor
						*ep = &alt->endpoint[k];

					if (ep == NULL) {
						if (DEBUG_LOG)
							printf("[IAP] %s - ep descriptor is null\n",
							       funcName);

						continue;
					}

					if ((ep->bmAttributes &
					     LIBUSB_TRANSFER_TYPE_MASK) ==
					    LIBUSB_TRANSFER_TYPE_INTERRUPT) {
						if (DEBUG_LOG)
							printf("[IAP] %s - ep %s: 0x%02X, size: %d\n",
							       funcName,
							       (ep->bEndpointAddress &
								LIBUSB_ENDPOINT_IN) ?
								       "in" :
								       "out",
							       ep->bEndpointAddress,
							       ep->wMaxPacketSize);
					}
				}
			}
		}
	}

	libusb_free_config_descriptor(config);

	if (interface < 0) {
		printf("[IAP] %s - interface not found\n", funcName);
		return 0;
	}

	// open device
	if (libusb_open(found_dev, &handle)) {
		printf("[IAP] %s - open device failed\n", funcName);
		return 0;
	}

	for (int i = 0; i < COUNT_INTERFACE; i++) {
		if (libusb_kernel_driver_active(handle, i)) {
			libusb_detach_kernel_driver(handle, i);
		}

		result = libusb_claim_interface(handle, i);
		if (result != 0) {
			printf("[IAP] %s - claim interface (%d) failed\n",
			       funcName, i);
			return 0;
		}
	}

	g_devInfo.handle = handle;

	libusb_free_device_list(devs, 1);

	return count;
}

void releaseResource()
{
	const char funcName[] = "release resource";

	if (DEBUG_LOG)
		printf("[IAP] %s - start\n", funcName);

	if (g_devInfo.handle) {
		libusb_close(g_devInfo.handle);
		g_devInfo.handle = NULL;
	}

	if (DEBUG_LOG)
		printf("[IAP] %s - exit\n", funcName);
}

void initialIapControl()
{
	releaseResource();

	g_devInfo.ep1_out = 0x01;
	g_devInfo.ep2_in = 0x82;
	g_devInfo.ep3_out = 0x03;
	g_devInfo.ep4_in = 0x84;
}

void releaseIapcontrol()
{
	releaseResource();

	libusb_exit(NULL);
}
