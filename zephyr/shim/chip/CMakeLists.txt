<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if (DEFINED CONFIG_SOC_FAMILY_NPCX)
	add_subdirectory(npcx)
elseif (DEFINED CONFIG_SOC_FAMILY_ITE_EC)
	add_subdirectory(it8xxx2)
elseif (DEFINED CONFIG_SOC_FAMILY_MICROCHIP_MEC)
	add_subdirectory(mchp)
elseif (DEFINED CONFIG_SOC_FAMILY_STM32)
	add_subdirectory(stm32)
elseif (DEFINED CONFIG_SOC_FAMILY_REALTEK_EC)
	add_subdirectory(rtk)
elseif (DEFINED CONFIG_SOC_EGIS_ET171)
	add_subdirectory(egis)
else ()
	add_subdirectory(stub)
endif()
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if (DEFINED CONFIG_SOC_FAMILY_NPCX)
	add_subdirectory(npcx)
elseif (DEFINED CONFIG_SOC_FAMILY_ITE_EC)
	add_subdirectory(it8xxx2)
elseif (DEFINED CONFIG_SOC_FAMILY_MICROCHIP_MEC)
	add_subdirectory(mchp)
elseif (DEFINED CONFIG_SOC_FAMILY_STM32)
	add_subdirectory(stm32)
elseif (DEFINED CONFIG_SOC_FAMILY_REALTEK_EC)
	add_subdirectory(rtk)
else ()
	add_subdirectory(stub)
endif()
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
