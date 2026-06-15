# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Default to gcc compiler, but allow override
CROSS_COMPILE_CC_NAME?=gcc

CROSS_COMPILE_ARM_DEFAULT:=arm-eabi
COREBOOT_TOOLCHAIN:=arm
USE_SDK?=1

# Use SDK toolchain
$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_arm),\
	arm-none-eabi-)
