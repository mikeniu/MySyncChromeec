#
#
# Copyright (C) 2017-2025 FocalTech Systems Co., Ltd. All Rights Reserved.
#
# Initial Author:  <zhangpiaoxiang@focaltech-electronics.com>
#

# Use the public definitions.

LOCAL_PATH:= $(shell pwd)



######################################
#
#
######################################
  
#target you can change test to what you want
#共享库文件名，lib*.a
TARGET  := focaltech_fingerprint_iap_linux
  
#compile and lib parameter
#编译参数
STRIP	=strip
CC      := gcc

LIBS    :=
LDFLAGS :=
DEFINES :=

#i think you should do anything here
INCLUDE :=	-I$(LOCAL_PATH)/libusb-1.0.29/libusb 				\
  			-I$(LOCAL_PATH)/libusb-1.0.29/libusb/os 			\
  			-I$(LOCAL_PATH)/focaltech/include

CFLAGS  :=  -fvisibility=hidden -fPIC -g -Wall -O2 $(DEFINES) $(INCLUDE)
CXXFLAGS:= -fvisibility=hidden $(CFLAGS) -DHAVE_CONFIG_H
  

  
#source file

SOURCE  :=                                                \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/libusb_port.c        \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/core.c               \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/descriptor.c         \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/hotplug.c            \
  $(LOCAL_PATH)/libusb-1.0.29/libusb/io.c 			         	\
  $(LOCAL_PATH)/libusb-1.0.29/libusb/sync.c 			       	\
  $(LOCAL_PATH)/libusb-1.0.29/libusb/strerror.c 		     	\
  $(LOCAL_PATH)/libusb-1.0.29/libusb/os/linux_usbfs.c 	 		\
  $(LOCAL_PATH)/libusb-1.0.29/libusb/os/events_posix.c 	 		\
  $(LOCAL_PATH)/libusb-1.0.29/libusb/os/threads_posix.c  		\
  $(LOCAL_PATH)/libusb-1.0.29/libusb/os/linux_netlink.c  		\
  $(LOCAL_PATH)/openssl_tiny/sm3.c				               	\
  $(LOCAL_PATH)/focaltech/src/ff_util.c 			           	\
  $(LOCAL_PATH)/focaltech/src/ft_sg_io.c       		       		\
  $(LOCAL_PATH)/focaltech/src/net_usb.c 			           	\
  $(LOCAL_PATH)/focaltech/src/fun.c 				            \
  $(LOCAL_PATH)/focaltech/src/main.c 
		

OBJS    := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE)))
  
.PHONY : everything objs clean veryclean rebuild
	  
everything : $(TARGET)
	  
all : $(TARGET)
	  
objs : $(OBJS)
	  
rebuild: veryclean everything

clean :
	rm -rf $(OBJS)

	
	    
veryclean : clean
	rm -fr $(TARGET)
	  
$(TARGET) : $(OBJS)
	$(CC) -o $(TARGET) $(OBJS)
	$(STRIP)  -x $(TARGET)
