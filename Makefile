# Copyright 2011 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system
#

# Allow for masking of some targets based on the build architecture. When
# building using a portage package (such as chromeos-ec), this variable will
# already be set. To support the typical developer workflow a default value is
# provided matching the typical architecture of developer workstations. Note
# that amd64 represents the entire x84_64 architecture including intel CPUs.
# This is used to exclude build targets that depend on sanitizers on
# architectures that don't support sanitizers yet (e.g. arm).
ARCH?=amd64
BOARD ?= host

# Only host utilities are built with this Makefile. EC firmware is built with
# zmake.
ifneq ($(BOARD),host)
$(error Only BOARD=host is supported, use zmake to build EC firmware)
endif

PROJECT?=ec

# An empty string.
# "-DMACRO" leads to MACRO=1.  Define an empty string "-DMACRO=" to take
# advantage of IS_ENABLED magic macro, which only allows an empty string.
EMPTY=

# Output directory for build objects
out?=build/$(BOARD)

include Makefile.toolchain

# Define the traditional first target. The dependencies of this are near the
# bottom as they can be altered by chip and board files.
.PHONY: all
all:
	@echo "make all is deprecated"

# Returns the opposite of a configuration variable
# y  ->
# ro -> rw
# rw -> ro
#    -> y
# usage: common-$(call not_cfg,$(CONFIG_FOO))+=bar.o
not_cfg = $(notdir $(filter $(strip $(1))/%, y/ ro/rw rw/ro /y))

# Returns the logical conjuction of two configuration variables
# usage: common-$(call and_cfg,$(CONFIG_FOO),$(CONFIG_BAR))+=foobar.o
and_cfg = $(notdir $(filter $(strip $(1))_$(strip $(2))/%, \
	y_y/y y_ro/ro y_rw/rw y_/ ro_y/ro ro_ro/ro ro_rw/ ro_/ \
	rw_y/rw rw_ro/ rw_rw/rw rw_/ _y/ _ro/ _rw/ _/))

# Run the given shell command and capture the output, but echo the command
# itself, if V is not 0 or empty.
# Usage: $(call shell_echo,<shell-command>)
shell_echo = $(if $(filter-out 0,$(V)),$(info $(1)))$(shell $(1))

CHIP:=host
CORE:=host
CROSS_COMPILE_HOST_DEFAULT:=x86_64-pc-linux-gnu-
$(call set-option,CROSS_COMPILE,$(CROSS_COMPILE_host),\
	$(CROSS_COMPILE_HOST_DEFAULT))
CFLAGS_CPU=-fno-builtin

-include build/Makefile.sdk

CROSS_COMPILE_TARGET_arm:=arm-eabi
CROSS_COMPILE_TARGET_riscv:=riscv64-elf
CROSS_COMPILE_TARGET_x86:=i386-elf
CROSS_COMPILE_TARGET_nds32:=nds32le-elf

CROSS_COMPILE_TOOLCHAIN:=$(CROSS_COMPILE_TARGET_$(COREBOOT_TOOLCHAIN))
CROSS_COREBOOT:=$(CROSS_COMPILE_TARGET_$(COREBOOT_TOOLCHAIN))

ifeq (riscv,$(COREBOOT_TOOLCHAIN))
CROSS_COMPILE_TOOLCHAIN:=riscv-elf
endif
ifneq (,$(COREBOOT_SDK_ROOT_$(COREBOOT_TOOLCHAIN)))
CROSS_COMPILE:=$(COREBOOT_SDK_ROOT_$(COREBOOT_TOOLCHAIN))/bin/$(CROSS_COREBOOT)-
else
ifneq (,$(USE_COREBOOT_SDK))
SDK_SCRIPT := util/coreboot_sdk.py
SDK_FLAGS := --toolchain $(CROSS_COMPILE_TOOLCHAIN)
SDK_COMMAND := $(SDK_SCRIPT) $(SDK_FLAGS)
PYTHON_RESULT:=$(shell $(SDK_COMMAND); echo $$?)
CROSS_COMPILE:=$(word 1,$(PYTHON_RESULT))/bin/$(CROSS_COREBOOT)-
EXIT_CODE := $(word 2,$(PYTHON_RESULT))
ifneq ($(EXIT_CODE),0)
CROSS_COMPILE:=/opt/coreboot-sdk/bin/$(CROSS_COREBOOT)-
endif
endif
endif

# Create uppercase config variants, to avoid mixed case constants.
# Also translate '-' to '_', so 'cortex-m' turns into 'CORTEX_M'.  This must
# be done before evaluating config.h.
uppercase = $(shell echo $(1) | tr '[:lower:]-' '[:upper:]_')
UC_BOARD:=$(call uppercase,$(BOARD))
UC_CHIP:=$(call uppercase,$(CHIP))
UC_CHIP_FAMILY:=$(call uppercase,$(CHIP_FAMILY))
UC_CHIP_VARIANT:=$(call uppercase,$(CHIP_VARIANT))
UC_CORE:=$(call uppercase,$(CORE))
UC_PROJECT:=$(call uppercase,$(PROJECT))

# Include paths.
includes=include include/driver $(out) third_party

$(eval BOARD_$(UC_BOARD)=y)
$(eval CHIP_$(UC_CHIP)=y)
$(eval CORE_$(UC_CORE)=y)
$(eval CHIP_VARIANT_$(UC_CHIP_VARIANT)=y)
$(eval CHIP_FAMILY_$(UC_CHIP_FAMILY)=y)

# Get build configuration for host tools
include util/build.mk
include util/lock/build.mk

# Collect all includes.
includes+=$(includes-y)

# The following variables are meant to be initialized in the
# board's build.mk. They will later be appended to in util/build.mk with
# utils that should be generated for all boards.
#
# host-util-bin-y  - Utils for the target platform on top of the EC.
#                    For example, the 32-bit x86 Chromebook.
#
# The util targets added to these variable will pickup extra build objects
# from their optional <util_name>-objs make variable.
#
# See commit bc4c1b4 for more context.
host-utils := $(addprefix $(out)/util/,$(host-util-bin-y))
host-utils-cxx := $(addprefix $(out)/util/,$(host-util-bin-cxx-y))
# Use the util_name with an added .c/.cc AND the special <util_name>-objs
# variable to compute per-utility source dependencies.
$(foreach u,$(host-util-bin-y),$(eval $(u)-srcs := \
	$(sort $($(u)-objs:%.o=util/%.c) $(wildcard util/$(u).c))))
$(foreach u,$(host-util-bin-cxx-y),$(eval $(u)-srcs := \
	$(sort $($(u)-objs:%.o=util/%.cc) $(wildcard util/$(u).cc))))

common_dirs=util

host-deps := $(addsuffix .d, $(host-utils) $(host-utils-cxx))

deps := $(host-deps) $(deps-y)

include Makefile.rules
export CROSS_COMPILE CFLAGS CC CPP LD NM AR OBJCOPY OBJDUMP
