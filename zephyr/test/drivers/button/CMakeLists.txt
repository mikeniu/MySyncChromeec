# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add INPUT_GPIO_KEYS specific tests
if(CONFIG_INPUT_GPIO_KEYS)
	zephyr_library_sources(src/input_gpio_keys.c)
else()
	zephyr_library_sources(src/main.c)
endif()
