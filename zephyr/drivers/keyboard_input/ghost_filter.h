/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CONFIG_CROS_EC_GHOST_FILTER
static inline void input_kbd_matrix_ghost_filter(const struct device *dev)
{
}
#else
void input_kbd_matrix_ghost_filter(const struct device *dev);
#endif
