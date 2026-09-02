/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator self-reboot procedure */

#ifndef CHIP_HOST_REBOOT_H_
#define CHIP_HOST_REBOOT_H_

#if !(defined(CONFIG_ZTEST))
__noreturn
#endif
	void emulator_reboot(void);

#endif /* CHIP_HOST_REBOOT_H_ */
