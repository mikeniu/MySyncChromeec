<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_EGIS630	fingerprint_egis630.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_EGIS630_PAL
			     fingerprint_egis630_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_ELAN80SERIES	fingerprint_elan80series.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_ELAN80SERIES_PAL
			     fingerprint_elan80series_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1025	fingerprint_fpc1025.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1025_PAL
			     fingerprint_fpc1025_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1145	fingerprint_fpc1145.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1145_PAL
			     fingerprint_fpc1145_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_SIMULATOR
			     fingerprint_sensor_sim.c)
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_EGIS630	fingerprint_egis630.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_EGIS630_PAL
			     fingerprint_egis630_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_ELAN80SG	fingerprint_elan80sg.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_ELAN80SG_PAL
			     fingerprint_elan80sg_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1025	fingerprint_fpc1025.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1025_PAL
			     fingerprint_fpc1025_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1145	fingerprint_fpc1145.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1145_PAL
			     fingerprint_fpc1145_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_SIMULATOR
			     fingerprint_sensor_sim.c)
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
