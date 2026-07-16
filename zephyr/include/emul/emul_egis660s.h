/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_EGIS660S_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_EGIS660S_H_

/* EGIS660S example hardware id - different from egis660 */
#define EGIS660S_HWID 0x2D12

/**
 * Set hardware id returned by emulator
 *
 * @param target The target emulator to modify
 * @param hardware_id new hardware id
 */
void egis660s_set_hwid(const struct emul *target, uint16_t hardware_id);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_EGIS660S_H_ */
