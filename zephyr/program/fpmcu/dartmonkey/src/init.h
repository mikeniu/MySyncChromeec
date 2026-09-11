// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __DARTMONKEY_INIT_H__
#define __DARTMONKEY_INIT_H__

#include <stdbool.h>

/**
 * @brief Check if the target is a development board or production hardware.
 *
 * @return 0 on success, negative error code on failure.
 */
int is_board_prod_hw(bool *is_prod);

#endif // __DARTMONKEY_INIT_H__
