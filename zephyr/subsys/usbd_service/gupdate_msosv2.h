/*
 * Copyright (c) 2016-2019 Intel Corporation
 * Copyright (c) 2023-2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef GOOGLE_UPDATE_MSOSV2_H_
#define GOOGLE_UPDATE_MSOSV2_H_

#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/usb/usbd.h>

#define GOOGLE_UPDATE_MSOS_VENDOR_CODE 0x02
#define GOOGLE_UPDATE_WINDOWS_VERSION 0x06030000UL

#define GOOGLE_UPDATE_INTERFACE_GUID                                           \
	'{', 0x00, 'F', 0x00, 'A', 0x00, '6', 0x00, '1', 0x00, '1', 0x00, 'C', \
		0x00, 'C', 0x00, '3', 0x00, '-', 0x00, '7', 0x00, '0', 0x00,   \
		'5', 0x00, '7', 0x00, '-', 0x00, '4', 0x00, '2', 0x00, 'E',    \
		0x00, 'E', 0x00, '-', 0x00, '9', 0x00, 'D', 0x00, '8', 0x00,   \
		'2', 0x00, '-', 0x00, '4', 0x00, '9', 0x00, '1', 0x00, '9',    \
		0x00, '6', 0x00, '3', 0x00, '9', 0x00, '5', 0x00, '6', 0x00,   \
		'2', 0x00, 'B', 0x00, '3', 0x00, '}', 0x00, 0x00, 0x00, 0x00,  \
		0x00

struct google_update_msos_descriptor {
	struct msosv2_descriptor_set_header header;
	struct msosv2_configuration_subset_header configuration;
	struct msosv2_function_subset_header function;
	struct msosv2_compatible_id compatible_id;
	struct msosv2_guids_property guid;
} __packed;

static struct google_update_msos_descriptor google_update_msos_desc = {
	.header = {
		.wLength = sizeof(struct msosv2_descriptor_set_header),
		.wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR,
		.dwWindowsVersion =
			sys_cpu_to_le32(GOOGLE_UPDATE_WINDOWS_VERSION),
		.wTotalLength =
			sys_cpu_to_le16(sizeof(google_update_msos_desc)),
	},

	.configuration = {
		.wLength =
			sizeof(struct msosv2_configuration_subset_header),
		.wDescriptorType =
			MS_OS_20_SUBSET_HEADER_CONFIGURATION,
		.bConfigurationValue = 0,
		.bReserved = 0,
		.wTotalLength = sys_cpu_to_le16(
			sizeof(struct msosv2_configuration_subset_header) +
			sizeof(struct msosv2_function_subset_header) +
			sizeof(struct msosv2_compatible_id) +
			sizeof(struct msosv2_guids_property)),
	},

	.function = {
		.wLength = sizeof(struct msosv2_function_subset_header),
		.wDescriptorType = MS_OS_20_SUBSET_HEADER_FUNCTION,
		.bFirstInterface = 3,
		.bReserved = 0,
		.wSubsetLength = sys_cpu_to_le16(
			sizeof(struct msosv2_function_subset_header) +
			sizeof(struct msosv2_compatible_id) +
			sizeof(struct msosv2_guids_property)),
	},

	.compatible_id = {
		.wLength = sizeof(struct msosv2_compatible_id),
		.wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID,
		.CompatibleID = {
			'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00
		},
		.SubCompatibleID = { 0 },
	},

	.guid = {
		.wLength = sizeof(struct msosv2_guids_property),
		.wDescriptorType = MS_OS_20_FEATURE_REG_PROPERTY,
		.wPropertyDataType = MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ,
		.wPropertyNameLength = 42,
		.PropertyName = { DEVICE_INTERFACE_GUIDS_PROPERTY_NAME },
		.wPropertyDataLength = 80,
		.bPropertyData = { GOOGLE_UPDATE_INTERFACE_GUID },
	},
};

struct google_update_bos_msos_descriptor {
	struct usb_bos_platform_descriptor platform;
	struct usb_bos_capability_msos capability;
} __packed;

static struct google_update_bos_msos_descriptor google_update_bos_desc = {
	.platform = {
		.bLength =
			sizeof(struct usb_bos_platform_descriptor) +
			sizeof(struct usb_bos_capability_msos),
		.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
		.bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
		.bReserved = 0,

		/* Microsoft OS 2.0 Platform Capability UUID */
		.PlatformCapabilityUUID = {
			0xDF, 0x60, 0xDD, 0xD8,
			0x89, 0x45,
			0xC7, 0x4C,
			0x9C, 0xD2,
			0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
		},
	},

	.capability = {
		.dwWindowsVersion =
			sys_cpu_to_le32(GOOGLE_UPDATE_WINDOWS_VERSION),
		.wMSOSDescriptorSetTotalLength =
			sys_cpu_to_le16(sizeof(google_update_msos_desc)),
		.bMS_VendorCode = GOOGLE_UPDATE_MSOS_VENDOR_CODE,
		.bAltEnumCode = 0,
	},
};

static struct net_buf *
google_update_msos_to_host(const struct usbd_context *const ctx,
			   const struct usb_setup_packet *const setup)
{
	struct net_buf *buf;
	uint16_t length;

	printk("MSOS request=%02x index=%04x length=%u\n", setup->bRequest,
	       setup->wIndex, setup->wLength);

	if (setup->bRequest != GOOGLE_UPDATE_MSOS_VENDOR_CODE ||
	    setup->wIndex != MS_OS_20_DESCRIPTOR_INDEX) {
		return NULL;
	}

	length = MIN(setup->wLength, sizeof(google_update_msos_desc));

	buf = usbd_ep_ctrl_data_in_alloc(ctx, length);
	if (buf == NULL) {
		return NULL;
	}

	net_buf_add_mem(buf, &google_update_msos_desc, length);
	return buf;
}

USBD_DESC_BOS_VREQ_DEFINE(google_update_msos, sizeof(google_update_bos_desc),
			  &google_update_bos_desc,
			  GOOGLE_UPDATE_MSOS_VENDOR_CODE,
			  google_update_msos_to_host, NULL);

#endif /* GOOGLE_UPDATE_MSOSV2_H_ */
