# -*- makefile -*-
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Power management for application processor and peripherals
#

power-$(CONFIG_CHIPSET_ALDERLAKE_SLG4BD44540)+=alderlake_slg4bd44540.o
power-$(CONFIG_CHIPSET_ALDERLAKE_SLG4BD44540)+=intel_x86.o
power-$(CONFIG_CHIPSET_APL_GLK)+=intel_x86.o
power-$(CONFIG_CHIPSET_CANNONLAKE)+=intel_x86.o
power-$(CONFIG_CHIPSET_COMETLAKE)+=cometlake.o intel_x86.o
power-$(CONFIG_CHIPSET_COMETLAKE_DISCRETE)+=intel_x86.o
power-$(CONFIG_CHIPSET_ICELAKE)+=icelake.o intel_x86.o
power-$(CONFIG_CHIPSET_MT8186)+=mt8186.o
power-$(CONFIG_CHIPSET_MT8192)+=mt8192.o
power-$(CONFIG_CHIPSET_CEZANNE)+=amd_x86.o
power-$(CONFIG_CHIPSET_SC7180)+=qcom.o
power-$(CONFIG_CHIPSET_SC7280)+=qcom.o
power-$(CONFIG_CHIPSET_QC_EXP)+=qcom_exp.o
power-$(CONFIG_CHIPSET_SKYLAKE)+=intel_x86.o
power-$(CONFIG_CHIPSET_STONEY)+=amd_x86.o
power-$(CONFIG_POWER_COMMON)+=common.o
power-$(CONFIG_POWER_TRACK_HOST_SLEEP_STATE)+=host_sleep.o
