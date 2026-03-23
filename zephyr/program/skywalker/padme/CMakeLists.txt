<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Padme Makeifile
zephyr_library_sources("src/board.c" "src/dp_alt_mode.c" "src/rt9490_workaround.c"
			"src/sensor.c" "src/thermal.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DETACHABLE_BASE
			"src/base_detect.c")
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Padme Makeifile
zephyr_library_sources("src/board.c" "src/dp_alt_mode.c" "src/rt9490_workaround.c"
			"src/sensor.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DETACHABLE_BASE
			"src/base_detect.c")
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
