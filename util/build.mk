# -*- makefile -*-
# Copyright 2014 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Host tools build
#

# See Makefile for description.
build-util-art-y += util/export_taskinfo.so

build-util-bin-$(CHIP_NPCX) += ecst

# USB type-C Vendor Information File generation
ifeq ($(CONFIG_USB_POWER_DELIVERY),y)
build-util-bin-y+=genvif
build-util-art-y+=$(BOARD)_vif.xml

# usb_pd_policy.c can be in baseboard, or board, or both.
genvif-pd-srcs=$(sort $(wildcard $(BASEDIR)/usb_pd_pdo.c \
			board/$(BOARD)/usb_pd_pdo.c))
genvif-pd-objs=$(genvif-pd-srcs:%.c=$(out)/util/%.o)
genvif-pd-objs += $(out)/common/usb_common.o $(out)/common/usb_pd_pdo.o
deps-$(CONFIG_USB_POWER_DELIVERY) += $(genvif-pd-objs:%.o=%.o.d)

$(out)/util/genvif: $(genvif-pd-objs) util/genvif.h board/$(BOARD)/board.h \
			include/usb_pd.h include/usb_pd_tcpm.h
$(out)/util/genvif: BUILD_LDFLAGS+=$(genvif-pd-objs) -flto

STANDALONE_FLAGS=-ffreestanding -fno-builtin -nostdinc \
			-Ibuiltin/ -D"__keep= " -DVIF_BUILD=$(EMPTY)

$(out)/util/%/usb_pd_policy.o: %/usb_pd_policy.c
	-@ mkdir -p $(@D)
	$(call quiet,c_to_vif,BUILDCC)
$(out)/util/%/usb_pd_pdo.o: %/usb_pd_pdo.c
	-@ mkdir -p $(@D)
	$(call quiet,c_to_vif,BUILDCC)
$(out)/common/usb_common.o: common/usb_common.c
	-@ mkdir -p $(@D)
	$(call quiet,c_to_vif,BUILDCC)
$(out)/common/usb_pd_pdo.o: common/usb_pd_pdo.c
	-@ mkdir -p $(@D)
	$(call quiet,c_to_vif,BUILDCC)
endif # CONFIG_USB_POWER_DELIVERY

ifneq ($(CONFIG_BOOTBLOCK),)
build-util-bin-y += gen_emmc_transfer_data

# Bootblock is only packed in RO image.
$(out)/util/gen_emmc_transfer_data: BUILD_LDFLAGS += -DSECTION_IS_RO=$(EMPTY)
endif # CONFIG_BOOTBLOCK

ifneq ($(CONFIG_IPI),)
build-util-bin-y += gen_ipi_table

$(out)/util/gen_ipi_table: board/$(BOARD)/board.h
$(out)/ipi_table_gen.inc: $(out)/util/gen_ipi_table
	$(call quiet,ipi_table,IPITBL )
endif

ifneq ($(CONFIG_TOUCHPAD_HASH_FW),)
build-util-bin-y += gen_touchpad_hash

# Assume RW section (touchpad FW must be identical for both RO+RW)
$(out)/util/gen_touchpad_hash: BUILD_LDFLAGS += -DSECTION_IS_RW=$(EMPTY)

BUILD_OPENSSL_CFLAGS := $(shell $(BUILD_PKG_CONFIG) --cflags openssl)
BUILD_OPENSSL_LDFLAGS := $(shell $(BUILD_PKG_CONFIG) --libs openssl)

$(out)/util/gen_touchpad_hash: BUILD_CFLAGS += $(BUILD_OPENSSL_CFLAGS)
$(out)/util/gen_touchpad_hash: BUILD_LDFLAGS += $(BUILD_OPENSSL_LDFLAGS)

deps-y += $(out)/util/gen_touchpad_hash.d
endif # CONFIG_TOUCHPAD_VIRTUAL_OFF

$(out)/util/export_taskinfo.so: $(out)/util/export_taskinfo_ro.o \
			$(out)/util/export_taskinfo_rw.o
	$(call quiet,link_taskinfo,BUILDLD)

$(out)/util/export_taskinfo_ro.o: util/export_taskinfo.c
	$(call quiet,c_to_taskinfo,BUILDCC,RO)

$(out)/util/export_taskinfo_rw.o: util/export_taskinfo.c
	$(call quiet,c_to_taskinfo,BUILDCC,RW)

deps-y += $(out)/util/export_taskinfo_ro.o.d $(out)/util/export_taskinfo_rw.o.d
