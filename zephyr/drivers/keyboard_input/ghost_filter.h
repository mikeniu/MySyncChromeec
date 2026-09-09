/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>

void input_kbd_matrix_ghost_filter_hook(const struct device *dev, int col);
