/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_HID_TOUCH_SCREEN_H
#define __CROS_EC_USB_HID_TOUCH_SCREEN_H

#define TOUCH_SCREEN_USAGE(reportid_max_count)                                 \
	{                                                                      \
		0x05, 0x0D, /* Usage Page (Digitizer) */                       \
		0x09, 0x04, /* Usage (Touch Screen) */                         \
		0xA1, 0x01, /* Collection (Application) */                     \
		0x85, 0x1C, /* Report ID (28) */                               \
		0x95, 0x01, /* Report Count (1) */                             \
		0x75, 0x08, /* Report Size (8) */                              \
		0x26, 0xFF, 0x00, /* Logical Maximum (255) */                  \
		0x15, 0x00, /* Logical Minimum (0) */                          \
		0x81, 0x03, /* Input (Const,Var,Abs,No Wrap,Linear,Preferred   \
			       State,No Null Position) */                      \
		0x09, 0x54, /* Usage (0x54) */                                 \
		0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred    \
			       State,No Null Position) */                      \
		0x05, 0x0D, /* Usage Page (Digitizer) */                       \
		0x09, 0x22, /* Usage (Finger) */                               \
		0xA1, 0x02, /* Collection (Logical) */                         \
		0x09, 0x42, /* Usage (Tip Switch) */                           \
		0x15, 0x00, /* Logical Minimum (0) */                          \
		0x25, 0x01, /* Logical Maximum (1) */                          \
		0x75, 0x01, /* Report Size (1) */                              \
		0x95, 0x01, /* Report Count (1) */                             \
		0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred    \
			       State,No Null Position) */                      \
		0x81, 0x03, /* Input (Const,Var,Abs,No Wrap,Linear,Preferred   \
			       State,No Null Position) */                      \
		0x09, 0x47, /* Usage (0x47) */                                 \
		0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred    \
			       State,No Null Position) */                      \
		0x95, 0x05, /* Report Count (5) */                             \
		0x81, 0x03, /* Input (Const,Var,Abs,No Wrap,Linear,Preferred   \
			       State,No Null Position) */                      \
		0x09, 0x51, /* Usage (0x51) */                                 \
		0x26, 0xFF, 0x00, /* Logical Maximum (255) */                  \
		0x75, 0x10, /* Report Size (16) */                             \
		0x95, 0x01, /* Report Count (1) */                             \
		0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred    \
			       State,No Null Position) */                      \
		0x05, 0x01, /* Usage Page (Generic Desktop Ctrls) */           \
		0x75, 0x10, /* Report Size (16) */                             \
		0x95, 0x01, /* Report Count (1) */                             \
		0x55, 0x0E, /* Unit Exponent (-2) */                           \
		0x65, 0x11, /* Unit (System: SI Linear, Length: Centimeter) */ \
		0x09, 0x30, /* Usage (X) */                                    \
		0x26, 0x20, 0x2F, /* Logical Maximum (12064) */                \
		0x35, 0x00, /* Physical Minimum (0) */                         \
		0x46, 0xC8, 0x0B, /* Physical Maximum (3016) */                \
		0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred    \
			       State,No Null Position) */                      \
		0x46, 0x5D, 0x07, /* Physical Maximum (1885) */                \
		0x09, 0x31, /* Usage (Y) */                                    \
		0x26, 0x74, 0x1D, /* Logical Maximum (7540) */                 \
		0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred    \
			       State,No Null Position) */                      \
		0xC0, /* End Collection */                                     \
		0x05, 0x0D, /* Usage Page (Digitizer) */                       \
		0x09, 0x22, /* Usage (Finger) */                               \
		0xA1, 0x02, /* Collection (Logical) */                         \
		0x09, 0x42, /* Usage (Tip Switch) */                           \
		0x15, 0x00, /* Logical Minimum (0) */                          \
		0x25, 0x01, /* Logical Maximum (1) */                          \
		0x75, 0x01, /* Report Size (1) */                              \
		0x95, 0x01, /* Report Count (1) */                             \
		0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred    \
			       State,No Null Position) */                      \
		0x81, 0x03, /* Input (Const,Var,Abs,No Wrap,Linear,Preferred   \
			       State,No Null Position) */                      \
		0x09, 0x47, /* Usage (0x47) */                                 \
		0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred    \
			       State,No Null Position) */                      \
		0x95, 0x05, /* Report Count (5) */                             \
		0x81, 0x03, /* Input (Const,Var,Abs,No Wrap,Linear,Preferred   \
			       State,No Null Position) */                      \
		0x09, 0x51, /* Usage (0x51) */                                 \
		0x26, 0xFF, 0x00, /* Logical Maximum (255) */                  \
		0x75, 0x10, /* Report Size (16) */                             \
		0x95, 0x01, /* Report Count (1) */                             \
		0x81, 0x02, /* Input (Data,Var,Abs,No Wrap,Linear,Preferred    \
			       State,No Null Position) */                      \
		0x05, 0x01, /* Usage Page (Generic Desktop Ctrls) */           \
		0x75, 0x10, /* Report Size (16) */                             \
		0x95, 0x01, /* Report Count (1) */                             \
		0x55, 0x0E, /*     Unit Exponent (-2) */                       \
		0x65, 0x11, /*     Unit (System: SI Linear, Length:            \
			       Centimeter) */                                  \
		0x09, 0x30, /*     Usage (X) */                                \
		0x26, 0x20, 0x2F, /*     Logical Maximum (12064) */            \
		0x35, 0x00, /*     Physical Minimum (0) */                     \
		0x46, 0xC8, 0x0B, /*     Physical Maximum (3016) */            \
		0x81, 0x02, /*     Input (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x46, 0x5D, 0x07, /*     Physical Maximum (1885) */            \
		0x09, 0x31, /*     Usage (Y) */                                \
		0x26, 0x74, 0x1D, /*     Logical Maximum (7540) */             \
		0x81, 0x02, /*     Input (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0xC0, /*   End Collection */                                   \
		0x05, 0x0D, /*   Usage Page (Digitizer) */                     \
		0x27, 0xFF, 0xFF,                                              \
		0x00, 0x00, /*   Logical Maximum (65534) */                    \
		0x75, 0x10, /*   Report Size (16) */                           \
		0x95, 0x01, /*   Report Count (1) */                           \
		0x09, 0x56, /*   Usage (0x56) */                               \
		0x81, 0x02, /*   Input (Data,Var,Abs,No Wrap,Linear,Preferred  \
			       State,No Null Position) */                      \
		0x85, 0x0C, /*   Report ID (12) */                             \
		0x09, 0x55, /*   Usage (0x55) */                               \
		0x75, 0x08, /*   Report Size (8) */                            \
		0x95, 0x01, /*   Report Count (1) */                           \
		0x26, 0xFF, 0x00, /*   Logical Maximum (255) */                \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0x85, 0x0A, /*   Report ID (10) */                             \
		0x06, 0x00, 0xFF, /*   Usage Page (Vendor Defined 0xFF00) */   \
		0x09, 0xC5, /*   Usage (0xC5) */                               \
		0x96, 0x00, 0x01, /*   Report Count (256) */                   \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0xC0, /* End Collection */                                     \
		0x06, 0x11, 0xFF, /* Usage Page (Vendor Defined 0xFF11) */     \
		0x09, 0x11, /* Usage (0x11) */                                 \
		0xA1, 0x01, /* Collection (Application) */                     \
		0x85, 0x13, /*   Report ID (19) */                             \
		0xA1, 0x02, /*   Collection (Logical) */                       \
		0x09, 0x00, /*     Usage (0x00) */                             \
		0x75, 0x08, /*     Report Size (8) */                          \
		0x15, 0x00, /*     Logical Minimum (0) */                      \
		0x26, 0xFF, 0x00, /*     Logical Maximum (255) */              \
		0x95, 0x12, /*     Report Count (18) */                        \
		0x81, 0x02, /*     Input (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0xC0, /*   End Collection */                                   \
		0x85, 0x02, /*   Report ID (2) */                              \
		0x09, 0x00, /*   Usage (0x00) */                               \
		0x95, 0x01, /*   Report Count (1) */                           \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0x85, 0x03, /*   Report ID (3) */                              \
		0x09, 0x00, /*   Usage (0x00) */                               \
		0x95, 0x3F, /*   Report Count (63) */                          \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0x85, 0x04, /*   Report ID (4) */                              \
		0x09, 0x00, /*   Usage (0x00) */                               \
		0x95, 0x0F, /*   Report Count (15) */                          \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0x85, 0x07, /*   Report ID (7) */                              \
		0x09, 0x00, /*   Usage (0x00) */                               \
		0x96, 0x00, 0x01, /*   Report Count (256) */                   \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0x85, 0x08, /*   Report ID (8) */                              \
		0x09, 0x00, /*   Usage (0x00) */                               \
		0x96, 0x87, 0x00, /*   Report Count (135) */                   \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0x85, 0x09, /*   Report ID (9) */                              \
		0x09, 0x00, /*   Usage (0x00) */                               \
		0x96, 0x3F, 0x00, /*   Report Count (63) */                    \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0x85, 0x0D, /*   Report ID (13) */                             \
		0x09, 0x00, /*   Usage (0x00) */                               \
		0x95, 0x07, /*   Report Count (7) */                           \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0x85, 0xFF, /*   Report ID (-1) */                             \
		0x09, 0x00, /*   Usage (0x00) */                               \
		0x95, 0x01, /*   Report Count (1) */                           \
		0x81, 0x03, /*   Input (Const,Var,Abs,No Wrap,Linear,Preferred \
			       State,No Null Position) */                      \
		0xC0, /* End Collection */                                     \
		0x05, 0x0D, /* Usage Page (Digitizer) */                       \
		0x09, 0x0E, /* Usage (0x0E) */                                 \
		0xA1, 0x01, /* Collection (Application) */                     \
		0x85, 0x0E, /*   Report ID (14) */                             \
		0x09, 0x23, /*   Usage (0x23) */                               \
		0xA1, 0x02, /*   Collection (Logical) */                       \
		0x09, 0x52, /*     Usage (0x52) */                             \
		0x09, 0x53, /*     Usage (0x53) */                             \
		0x15, 0x00, /*     Logical Minimum (0) */                      \
		0x25, 0x0A, /*     Logical Maximum (10) */                     \
		0x75, 0x08, /*     Report Size (8) */                          \
		0x95, 0x02, /*     Report Count (2) */                         \
		0xB1, 0x02, /*     Feature (Data,Var,Abs,No                    \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0xC0, /*   End Collection */                                   \
		0xC0, /* End Collection */                                     \
		0x06, 0x14, 0xFF, /* Usage Page (Vendor Defined 0xFF14) */     \
		0x09, 0x0E, /* Usage (0x0E) */                                 \
		0xA1, 0x01, /* Collection (Application) */                     \
		0x85, 0x12, /*   Report ID (18) */                             \
		0x09, 0x00, /*   Usage (0x00) */                               \
		0x75, 0x08, /*   Report Size (8) */                            \
		0x15, 0x00, /*   Logical Minimum (0) */                        \
		0x26, 0xFF, 0x00, /*   Logical Maximum (255) */                \
		0x95, 0x3F, /*   Report Count (63) */                          \
		0xB1, 0x02, /*   Feature (Data,Var,Abs,No                      \
			       Wrap,Linear,Preferred State,No Null             \
			       Position,Non-volatile) */                       \
		0xC0, /* End Collection */                                     \
		0x05, 0x0D, /* Usage Page (Digitizer) */                       \
		0x09, 0x02, /* Usage (Pen) */                                  \
		0xA1, 0x01, /* Collection (Application) */                     \
		0x85, 0x14, /*   Report ID (20) */                             \
		0xA4, /*   Push */                                             \
		0x09, 0x20, /*     Usage (Stylus) */                           \
		0xA1, 0x00, /*     Collection (Physical) */                    \
		0x09, 0x42, /*       Usage (Tip Switch) */                     \
		0x09, 0x44, /*       Usage (Barrel Switch) */                  \
		0x09, 0x45, /*       Usage (Eraser) */                         \
		0x09, 0x3C, /*       Usage (Invert) */                         \
		0x09, 0x5A, /*       Usage (0x5A) */                           \
		0x09, 0x32, /*       Usage (In Range) */                       \
		0x15, 0x00, /*       Logical Minimum (0) */                    \
		0x25, 0x01, /*       Logical Maximum (1) */                    \
		0x75, 0x01, /*       Report Size (1) */                        \
		0x95, 0x06, /*       Report Count (6) */                       \
		0x81, 0x02, /*       Input (Data,Var,Abs,No                    \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x95, 0x02, /*       Report Count (2) */                       \
		0x81, 0x03, /*       Input (Const,Var,Abs,No                   \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x05, 0x01, /*       Usage Page (Generic Desktop Ctrls) */     \
		0x09, 0x30, /*       Usage (X) */                              \
		0x27, 0xCF, 0x75,                                              \
		0x00, 0x00, /*       Logical Maximum (30158) */                \
		0x47, 0xCF, 0x75,                                              \
		0x00, 0x00, /*       Physical Maximum (30158) */               \
		0x65, 0x11, /*       Unit (System: SI Linear, Length:          \
			       Centimeter) */                                  \
		0x55, 0x0D, /*       Unit Exponent (-3) */                     \
		0x75, 0x10, /*       Report Size (16) */                       \
		0x95, 0x01, /*       Report Count (1) */                       \
		0x81, 0x02, /*       Input (Data,Var,Abs,No                    \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x09, 0x31, /*       Usage (Y) */                              \
		0x27, 0xA2, 0x49,                                              \
		0x00, 0x00, /*       Logical Maximum (18849) */                \
		0x47, 0xA2, 0x49,                                              \
		0x00, 0x00, /*       Physical Maximum (18849) */               \
		0x81, 0x02, /*       Input (Data,Var,Abs,No                    \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x45, 0x00, /*       Physical Maximum (0) */                   \
		0x65, 0x00, /*       Unit (None) */                            \
		0x55, 0x00, /*       Unit Exponent (0) */                      \
		0x05, 0x0D, /*       Usage Page (Digitizer) */                 \
		0x09, 0x30, /*       Usage (Tip Pressure) */                   \
		0x26, 0xFF, 0x0F, /*       Logical Maximum (4095) */           \
		0x75, 0x10, /*       Report Size (16) */                       \
		0x81, 0x02, /*       Input (Data,Var,Abs,No                    \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x06, 0x00, 0xFF, /*       Usage Page (Vendor Defined 0xFF00)  \
				   */                                          \
		0x09, 0x5B, /*       Usage (0x5B) */                           \
		0x16, 0x00, 0x80, /*       Logical Minimum (-32768) */         \
		0x26, 0xFF, 0x7F, /*       Logical Maximum (32767) */          \
		0x75, 0x10, /*       Report Size (16) */                       \
		0x81, 0x02, /*       Input (Data,Var,Abs,No                    \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x05, 0x0D, /*       Usage Page (Digitizer) */                 \
		0x09, 0x5B, /*       Usage (0x5B) */                           \
		0x17, 0x00, 0x00,                                              \
		0x00, 0x80, /*       Logical Minimum (-2147483649) */          \
		0x27, 0xFF, 0xFF,                                              \
		0xFF, 0x7F, /*       Logical Maximum (2147483646) */           \
		0x75, 0x20, /*       Report Size (32) */                       \
		0x81, 0x02, /*       Input (Data,Var,Abs,No                    \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x06, 0x00, 0xFF, /*       Usage Page (Vendor Defined 0xFF00)  \
				   */                                          \
		0x09, 0x00, /*       Usage (0x00) */                           \
		0x75, 0x08, /*       Report Size (8) */                        \
		0x26, 0xFF, 0x00, /*       Logical Maximum (255) */            \
		0x15, 0x00, /*       Logical Minimum (0) */                    \
		0x81, 0x02, /*       Input (Data,Var,Abs,No                    \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x05, 0x0D, /*       Usage Page (Digitizer) */                 \
		0x09, 0x3B, /*       Usage (Battery Strength) */               \
		0x81, 0x02, /*       Input (Data,Var,Abs,No                    \
			       Wrap,Linear,Preferred State,No Null Position)   \
			     */                                                \
		0x65, /*       Unit (None) */                                  \
	}

struct touch_finger {
	uint8_t tip_switch : 1;
	uint8_t reserved0 : 1;
	uint8_t confidence : 1;
	uint8_t reserved1 : 5;

	uint16_t contact_id;

	uint16_t x;
	uint16_t y;
} __packed;

struct usb_hid_touch_screen_report {
	uint8_t report_id;

	uint8_t contact_count;

	struct touch_finger finger[2];

	uint16_t scan_time;
} __packed;

void set_touch_screen_report(struct usb_hid_touch_screen_report *report);

#endif /* __CROS_EC_USB_HID_TOUCH_SCREEN_H */
