<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE file.

zephyr_library_sources("src/led.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/dp_alt_mode.c")
zephyr_library_sources("src/usbc.c")
zephyr_library_sources("src/usb_mux.c")
zephyr_library_sources("src/touch.c")
zephyr_library_sources("src/suspend_resume.c")
zephyr_library_sources("src/thermal.c")
zephyr_library_sources("src/power_config.c")
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE file.

zephyr_library_sources("src/led.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/dp_alt_mode.c")
zephyr_library_sources("src/usb_mux.c")
zephyr_library_sources("src/touch.c")
zephyr_library_sources("src/suspend_resume.c")
zephyr_library_sources("src/thermal.c")
zephyr_library_sources("src/power_config.c")
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
