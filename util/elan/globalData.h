// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <../include/libusb.h>

enum {
	DEVSTA_NON,
	DEVSTA_INSERT,
	DEVSTA_EXISTED,
	DEVSTA_REMOVE,
};

typedef struct _DEVINFO {
	libusb_device_handle *handle;
	int ep1_out;
	int ep2_in;
	int ep3_out;
	int ep4_in;
} DEVINFO;
