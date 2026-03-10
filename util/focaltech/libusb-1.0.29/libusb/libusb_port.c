
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <asm/byteorder.h>
#include <libusb.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>

#include "ff_common.h"
#include "ff_log.h"

// #include "m_libusb_port.h"
#define API_EXPORTED __attribute__((visibility("default")))

#define USB_HUB_TIMEOUT 5000 /* milliseconds */
#define USB_PORT_FEAT_POWER 8
// Copied from kernel's uapi/linux/usb/ch11.h. Should this be obtained
// by plain #include ?
#define USB_PORT_FEAT_ENABLE 1
#define USB_PORT_FEAT_RESET 4

#define USB_DT_HUB (USB_TYPE_CLASS | 0x09)
#define USB_DT_HUB_SIZE 7

struct usb_hub_descriptor {
  __u8 bDescLength;
  __u8 bDescriptorType;
  __u8 bNbrPorts;
  __le16 wHubCharacteristics;
  __u8 bPwrOn2PwrGood;
  __u8 bHubContrCurrent;
} __attribute__((packed));

struct usb_port_status {
  __le16 wPortStatus;
  __le16 wPortChange;
} __attribute__((packed));

#define USB_PORT_STAT_CONNECTION 0x0001
#define USB_PORT_STAT_ENABLE 0x0002
#define USB_PORT_STAT_SUSPEND 0x0004
#define USB_PORT_STAT_OVERCURRENT 0x0008
#define USB_PORT_STAT_RESET 0x0010
#define USB_PORT_STAT_L1 0x0020
/* bits 6 to 7 are reserved */
#define USB_PORT_STAT_POWER 0x0100
#define USB_PORT_STAT_LOW_SPEED 0x0200
#define USB_PORT_STAT_HIGH_SPEED 0x0400
#define USB_PORT_STAT_TEST 0x0800
#define USB_PORT_STAT_INDICATOR 0x1000
#define USB_PORT_STAT_POWER_3 0x0200 /* USB 3.0 */

#define V_INIT_SUCCESS 0x5a

typedef struct focal_usb_device_port_st {
  uint8_t init;
  uint8_t bus;
  uint8_t hub;
  uint8_t port;
  uint8_t disabled;
} focal_usb_device_port_t;

typedef struct focal_usb_device_port_method_st {
  uint8_t init;
  int (*enable)(void);
  int (*disable)(void);
  int (*reset)(void);
  int (*get_port_status)(void);
  int (*reset_port_status)(void);
} focal_usb_device_port_method_t;

static int fd; /* Hub device file */
static int usb_level;
static focal_usb_device_port_t focal_dev_port;

static void usage(void) {
  FF_LOGE("Usage:"
          "\thubpower busnum:devnum power {portnum (on|off)} ...\n"
          "\thubpower busnum:devnum reset {portnum (on|off)} ...\n"
          "\thubpower busnum:devnum enable {portnum (on|off)} ...\n"
          "\thubpower busnum:devnum status\n"
          "\thubpower busnum:devnum bind\n");
}

static void port_status(int portnum) {
  struct usbdevfs_ctrltransfer ctrl;
  struct usb_port_status pstat;
  int rc;

  ctrl.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER;
  ctrl.bRequest = USB_REQ_GET_STATUS;
  ctrl.wValue = 0;
  ctrl.wIndex = portnum;
  ctrl.wLength = sizeof(pstat);
  ctrl.timeout = USB_HUB_TIMEOUT;
  ctrl.data = &pstat;
  rc = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
  if (rc == -1) {
    FF_LOGE("Error in ioctl "
            "(get port %d status): %s\n",
            portnum, strerror(errno));
    return;
  }

  FF_LOGD("Port %2d status: %04x ", portnum, pstat.wPortStatus);

  if (usb_level <= 2) {
    if (pstat.wPortStatus & USB_PORT_STAT_INDICATOR)
      FF_LOGD(" Indicator");
    if (pstat.wPortStatus & USB_PORT_STAT_TEST)
      FF_LOGD(" Test-Mode");
    if (pstat.wPortStatus & USB_PORT_STAT_HIGH_SPEED)
      FF_LOGD(" High-Speed");
    if (pstat.wPortStatus & USB_PORT_STAT_LOW_SPEED)
      FF_LOGD(" Low-Speed");
    if (pstat.wPortStatus & USB_PORT_STAT_POWER)
      FF_LOGD(" Power-On");
    else
      FF_LOGD(" Power-Off");
  } else if (usb_level == 3) {
    if (pstat.wPortStatus & USB_PORT_STAT_POWER_3)
      FF_LOGD(" Power-On");
    else
      FF_LOGD(" Power-Off");
  }

  if (pstat.wPortStatus & USB_PORT_STAT_RESET)
    FF_LOGD(" Resetting");
  if (pstat.wPortStatus & USB_PORT_STAT_OVERCURRENT)
    FF_LOGD(" Overcurrent");
  if (pstat.wPortStatus & USB_PORT_STAT_SUSPEND)
    FF_LOGD(" Suspended");
  if (pstat.wPortStatus & USB_PORT_STAT_ENABLE)
    FF_LOGD(" Enabled");
  if (pstat.wPortStatus & USB_PORT_STAT_CONNECTION)
    FF_LOGD(" Connected");
}

static int change_port_status(int argc, char **argv) {
  FF_LOGD("%s enter\n", __func__);
  int busnum, devnum, numports;
  enum { DO_POWER, DO_RESET, DO_ENABLE, DO_STATUS, DO_BIND } action=DO_STATUS;
  char fname1[40], fname2[40];
  int rc;
  int portnum;
  struct usb_device_descriptor dev_descr;
  struct usb_hub_descriptor hub_descr;
  struct usbdevfs_ctrltransfer ctrl;
  struct usbdevfs_ioctl usb_ioctl;
  int bus_endian;

  if (argc < 3)
    usage();
  if (sscanf(argv[1], "%d:%d", &busnum, &devnum) != 2 || busnum <= 0 ||
      busnum > 255 || devnum <= 0 || devnum > 255)
    usage();

  if (strcmp(argv[2], "power") == 0) {
    action = DO_POWER;
    if ((argc - 3) % 2 != 0)
      usage();
  } else if (strcmp(argv[2], "reset") == 0) {
    action = DO_RESET;
    if ((argc - 3) % 2 != 0)
      usage();
  } else if (strcmp(argv[2], "enable") == 0) {
    action = DO_ENABLE;
    if ((argc - 3) % 2 != 0)
      usage();
  } else if (strcmp(argv[2], "status") == 0) {
    action = DO_STATUS;
    if (argc != 3)
      usage();
  } else if (strcmp(argv[2], "bind") == 0) {
    action = DO_BIND;
    if (argc != 3)
      usage();
  } else {
    usage();
  }

  sprintf(fname1, "/dev/bus/usb/%03d/%03d", busnum, devnum);
  sprintf(fname2, "/proc/bus/usb/%03d/%03d", busnum, devnum);

  bus_endian = 1;
  fd = open(fname1, O_RDWR);
  if (fd < 0) {
    int err1 = errno;

    bus_endian = 0;
    fd = open(fname2, O_RDWR);
    if (fd < 0) {
      FF_LOGE("Unable to open device file %s: %s\n", fname1, strerror(err1));
      FF_LOGE("Unable to open device file %s: %s\n", fname2, strerror(errno));
      return 1;
    }
  }

  rc = read(fd, &dev_descr, USB_DT_DEVICE_SIZE);
  if (rc != USB_DT_DEVICE_SIZE) {
    FF_LOGE("Error reading device descriptor");
    return 1;
  }
  if (dev_descr.bDeviceClass != USB_CLASS_HUB) {
    FF_LOGE("Device %d:%d is not a hub\n", busnum, devnum);
    return 1;
  }
  if (bus_endian) {
    dev_descr.bcdUSB = __le16_to_cpu(dev_descr.bcdUSB);
  }
  usb_level = dev_descr.bcdUSB >> 8;

  ctrl.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE;
  ctrl.bRequest = USB_REQ_GET_DESCRIPTOR;
  ctrl.wValue = USB_DT_HUB << 8;
  ctrl.wIndex = 0;
  ctrl.wLength = USB_DT_HUB_SIZE;
  ctrl.timeout = USB_HUB_TIMEOUT;
  ctrl.data = &hub_descr;
  rc = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
  if (rc == -1) {
    FF_LOGE("Error in ioctl (read hub descriptor)");
    return 1;
  }
  numports = hub_descr.bNbrPorts;

  if (action == DO_STATUS) {
    for (portnum = 1; portnum <= numports; ++portnum)
      port_status(portnum);
    return 0;
  }

  if (action == DO_BIND) {
    usb_ioctl.ifno = 0;
    usb_ioctl.ioctl_code = USBDEVFS_CONNECT;
    usb_ioctl.data = NULL;
    rc = ioctl(fd, USBDEVFS_IOCTL, &usb_ioctl);
    if (rc == -1) {
      FF_LOGE("Error in ioctl (USBDEVFS_CONNECT)");
      return 1;
    }
    FF_LOGD("Bind-driver request sent to the kernel\n");
    return 0;
  }

  if ((action == DO_POWER) || (action == DO_RESET) || (action == DO_ENABLE)) {
    int i;

    for (i = 3; i < argc; i += 2) {
      portnum = atoi(argv[i]);
      if (portnum < 1 || portnum > numports) {
        FF_LOGE("Invalid port number: %d\n", portnum);
        continue;
      }

      if (strcmp(argv[i + 1], "on") == 0)
        ctrl.bRequest = USB_REQ_SET_FEATURE;
      else if (strcmp(argv[i + 1], "off") == 0)
        ctrl.bRequest = USB_REQ_CLEAR_FEATURE;
      else {
        FF_LOGE("Invalid port power level: %s\n)", argv[i + 1]);
        continue;
      }
      ctrl.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;

      if (action == DO_POWER)
        ctrl.wValue = USB_PORT_FEAT_POWER;
      else if (action == DO_RESET)
        ctrl.wValue = USB_PORT_FEAT_RESET;
      else if (action == DO_ENABLE)
        ctrl.wValue = USB_PORT_FEAT_ENABLE;

      ctrl.wIndex = portnum;
      ctrl.wLength = 0;
      ctrl.timeout = USB_HUB_TIMEOUT;
      ctrl.data = NULL;
      rc = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
      if (rc == -1) {
        FF_LOGE("Error in ioctl "
                "(set/clear port %d feature): %s\n",
                portnum, strerror(errno));
        continue;
      }

      port_status(portnum);
    }
  }
  return 0;
}

static void print_devs(libusb_device **devs) {
  libusb_device *dev, *parent;
  int i = 0;

  if (focal_dev_port.init != V_INIT_SUCCESS) {
    memset(&focal_dev_port, 0, sizeof(focal_usb_device_port_t));

  } else {
    return;
  }

  while ((dev = devs[i++]) != NULL) {
    struct libusb_device_descriptor desc;
    int r = libusb_get_device_descriptor(dev, &desc);
    if (r < 0) {
      FF_LOGE("failed to get device descriptor");
      return;
    }

    if (desc.idVendor == FOCAL_EC_VID) {

      if (desc.idProduct != FOCAL_EC_PID_9865 &&
          desc.idProduct != FOCAL_EC_PID_9869) {
        continue;
      }

      FF_LOGD("%04x:%04x (bus %d, device %d)", desc.idVendor, desc.idProduct,
              libusb_get_bus_number(dev), libusb_get_device_address(dev));

      focal_dev_port.port = libusb_get_port_number(dev);

      FF_LOGD("port=%d", focal_dev_port.port);

      parent = libusb_get_parent(dev);
      if (parent) {

        focal_dev_port.bus = libusb_get_bus_number(parent);
        focal_dev_port.hub = libusb_get_device_address(parent);

        FF_LOGD("parent info:  (bus %d, device %d)", focal_dev_port.bus,
                focal_dev_port.hub);

        focal_dev_port.init = V_INIT_SUCCESS;

        FF_LOGD("get port success");
      }

      break;
    }
  }
}

static int focal_dev_reset() {

  FF_LOGD("%s enter\n", __func__);
  if (focal_dev_port.init != V_INIT_SUCCESS) {
    return -1;
  }

  char *cmd[16]; //={"value_default1","reset","value_default2","on","value_default3"};

  for (int i = 0; i < 16; i++) {
    cmd[i] = (char *)malloc(16);
    memset(cmd[i], 0, 16);
  }

  sprintf(cmd[1], "%d:%d", focal_dev_port.bus, focal_dev_port.hub);
  FF_LOGD("[cmd]%s\n", cmd[1]);
  strcpy(cmd[2], "reset");
  strcpy(cmd[4], "on");

  sprintf(cmd[3], "%d", focal_dev_port.port);
  FF_LOGD("[cmd]%s\n", cmd[3]);

  change_port_status(5, cmd);

  for (int i = 0; i < 16; i++) {
    free(cmd[i]);
  }

  focal_dev_port.disabled = 0;
  return 0;
}

static int focal_dev_disable() {

  FF_LOGI("%s enter", __func__);
  if (focal_dev_port.init != V_INIT_SUCCESS) {
    return -1;
  }

  if (focal_dev_port.disabled) {
    return -2;
  }

  char *cmd[16]; //={"value_default1","reset","value_default2","on","value_default3"};

  for (int i = 0; i < 16; i++) {
    cmd[i] = (char *)malloc(16);
    memset(cmd[i], 0, 16);
  }

  sprintf(cmd[1], "%d:%d", focal_dev_port.bus, focal_dev_port.hub);
  FF_LOGD("[cmd]%s\n", cmd[1]);
  strcpy(cmd[2], "enable");
  strcpy(cmd[4], "off");

  sprintf(cmd[3], "%d", focal_dev_port.port);
  FF_LOGD("[cmd]%s\n", cmd[3]);

  change_port_status(5, cmd);

  for (int i = 0; i < 16; i++) {
    free(cmd[i]);
  }
  focal_dev_port.disabled = 1;

  return 0;
}

static int get_port_status() { return focal_dev_port.disabled; }

static int reset_port_status() {

  focal_dev_port.disabled = 0;
  return 0;
}

static int focal_usb_device_port_method_init(focal_usb_device_port_method_t *method) {
  FF_LOGD("%s enter\n", __func__);

  if (method == NULL) {

    return -1;
  }

  libusb_device **devs;
  int r;
  ssize_t cnt;

  memset(method, 0, sizeof(focal_usb_device_port_method_t));

  r = libusb_init(NULL);
  if (r < 0)
    return r;

  cnt = libusb_get_device_list(NULL, &devs);
  if (cnt < 0) {
    libusb_exit(NULL);
    return (int)cnt;
  }

  print_devs(devs);
  libusb_free_device_list(devs, 1);

  libusb_exit(NULL);

  method->reset = focal_dev_reset;
  method->disable = focal_dev_disable;

  method->init = V_INIT_SUCCESS;
  method->get_port_status = get_port_status;
  method->reset_port_status = reset_port_status;
  focal_dev_port.disabled = 0;
  return 0;
}

int focal_device_disable() {

  int ret = 0;
  focal_usb_device_port_method_t method;
  memset(&method, 0, sizeof(focal_usb_device_port_method_t));

  focal_usb_device_port_method_init(&method);

  if (method.init == V_INIT_SUCCESS) {
    if (method.disable) {
      ret = method.disable();
    }
  }

  return ret;
}
