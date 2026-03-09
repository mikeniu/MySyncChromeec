
//#include <libusb-1.0/libusb.h>
#include <../include/hidapi.h>



enum
{
  DEVSTA_NON,
  DEVSTA_INSERT,
  DEVSTA_EXISTED,
  DEVSTA_REMOVE,
};

typedef struct _DEVINFO
{
    hid_device *handle_ep1;
    hid_device *handle_ep2;
    hid_device *handle_ep3;
    hid_device *handle_ep4;

}DEVINFO;




