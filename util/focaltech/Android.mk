
#
# Copyright (C) 2017-2025 FocalTech Systems Co., Ltd. All Rights Reserved.
#
# Initial Author:  <zhangpiaoxiang@focaltech-electronics.com>
#

ALLOW_MISSING_DEPS=true
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS += -pie -fPIE -O2
LOCAL_LDFLAGS += -pie -fPIE
LOCAL_LDLIBS    := -llog

LOCAL_SRC_FILES := 				                               \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/libusb_port.c 			       \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/core.c 			       \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/descriptor.c 		   \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/hotplug.c 		       \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/io.c 			         \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/sync.c 			       \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/strerror.c 		     \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/os/linux_usbfs.c 	 \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/os/events_posix.c 	 \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/os/threads_posix.c  \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/os/linux_netlink.c  \
  $(LOCAL_PATH)/openssl_tiny/sm3.c				               \
  $(LOCAL_PATH)/focaltech/src/ff_util.c 			           \
  $(LOCAL_PATH)/focaltech/src/ft_sg_io.c       		       \
  $(LOCAL_PATH)/focaltech/src/net_usb.c 			           \
  $(LOCAL_PATH)/focaltech/src/fun.c 				             \
  $(LOCAL_PATH)/focaltech/src/main.c 

LOCAL_C_INCLUDES += 				                             \
  $(LOCAL_PATH)/libusb-1.0.29/libusb 				             \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/os 			           \
  $(LOCAL_PATH)/focaltech/include

LOCAL_MODULE:= focaltech_fingerprint_iap
LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)
