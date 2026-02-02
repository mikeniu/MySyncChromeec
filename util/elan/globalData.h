// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hidapi/hidapi.h>

enum {
	DEVSTA_NON,
	DEVSTA_INSERT,
	DEVSTA_EXISTED,
	DEVSTA_REMOVE,
};

typedef struct _DEVINFO {
	hid_device *handle_ep1;
	hid_device *handle_ep2;
	hid_device *handle_ep3;
	hid_device *handle_ep4;
} DEVINFO;
