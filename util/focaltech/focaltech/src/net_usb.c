#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ff_common.h"
#include "ff_log.h"
#include "ff_util.h"
#include "net_usb.h"

#define MAX_BUFFER_LEN (4 * 1024)
unsigned char g_no_device_flag = 0;

typedef struct
{
    uint8_t addr : 4;
    uint8_t unused : 3;
    uint8_t dir : 1;
} endpoint_addr_t;

typedef union {
    endpoint_addr_t s;
    uint8_t v;
} u_endpoint_addr_t;

typedef struct
{
    uint8_t transfer_type : 2;
    uint8_t iso_sync_type : 2;
} endpoint_attr_t;

typedef union {
    endpoint_attr_t s;
    uint8_t v;
} u_endpoint_attr_t;

static usb_context_t  _usb_ctx,*usb_ctx = &_usb_ctx;
static libusb_hotplug_callback_handle callback_handle=0;

int LIBUSB_CALL usb_remove_event_callback(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event, void *user_data)
{
    struct libusb_device_descriptor desc;
    int r;

    int *ret=(int *)user_data;

    FF_LOGD("%s enter",__func__);

    if(!ret){
        FF_LOGW("user data is null");
    }

    r = libusb_get_device_descriptor(dev, &desc);
    if (LIBUSB_SUCCESS != r)
    {
        FF_LOGE("error getting device descriptor.");
    }

    if (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT == event)
    {
        if(ret){
            *ret=0x55aa;
        }
        FF_LOGW("usb device removed: %04x:%04x", desc.idVendor, desc.idProduct);
        //do sth
        if(ret){
            *ret=0x55aa;
        }
    }

    return 0;

}

int LIBUSB_CALL usb_event_callback(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event, void *user_data)
{
    struct libusb_device_descriptor desc;
    int r;
    int *ret=(int *)user_data;

    if(!ret){
        FF_LOGW("user data is null");
    }

    FF_LOGD("usb hotplugin event. %d", event);

    r = libusb_get_device_descriptor(dev, &desc);
    if (LIBUSB_SUCCESS != r)
    {
        FF_LOGE("error getting device descriptor.");
    }

    if (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED == event)
    {
        FF_LOGD("usb device attached: %04x:%04x", desc.idVendor, desc.idProduct);

        if (usb_ctx->handle)
        {
            libusb_close(usb_ctx->handle);
            usb_ctx->handle = NULL;
        }

        r = libusb_open(dev, &usb_ctx->handle);
        if (LIBUSB_SUCCESS != r)
        {
            FF_LOGE("error opening device.");
        }
        else
        {
#if 0 
            r = libusb_reset_device(usb_ctx->handle);
            if (r < 0)
            {
                printf("failed to reset the device\r\n");
            }
#endif

            if (libusb_kernel_driver_active(usb_ctx->handle, 0) == 1)
            { // find out if kernel driver is attached
                FF_LOGD("kernel driver active");
                if (libusb_detach_kernel_driver(usb_ctx->handle, 0) == 0) // detach it
                    FF_LOGD("kernel driver detach");
            }
            int r =
                libusb_claim_interface(usb_ctx->handle, 0); // claim interface 0 (the first) of device (mine had jsut 1)
            if (r < 0)
            {
                FF_LOGE("libusb_claim_interface failed: %s", libusb_error_name(r));
                return -1;
            }
            g_no_device_flag = 1;
            FF_LOGD("usb device open success %p", usb_ctx->handle);
        }
    }
    else if (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT == event)
    {
        FF_LOGW("usb device removed: %04x:%04x", desc.idVendor, desc.idProduct);
        if(ret){
                *ret=0x55aa;
        }
        
        if (usb_ctx->handle)
        {
            if(ret){
                *ret=0x55aa;
            }

            libusb_free_device_list(usb_ctx->devs, 1);
            libusb_release_interface(usb_ctx->handle, 0);
            libusb_close(usb_ctx->handle);
            usb_ctx->handle = NULL;
            g_no_device_flag = 0;
       
        }
    }
    else
    {
        // todo
    }

    return 0;
}

int mbedtls_usb_init(const uint32_t pid, const uint32_t vid)
{
    int ret = 0;
    libusb_hotplug_callback_handle hp[2];
    usb_ctx->devs = NULL;
    usb_ctx->handle = NULL;

    ret = libusb_init(&usb_ctx->ctx);
    if (ret < 0)
    {
        FF_LOGE("failed to initialise libusb: %s", libusb_error_name(ret));
        return -1;
    }

    ret = libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG);
    if (ret == 0)
    {
        FF_LOGE("Hotplug capabilities are not supported on this platform: %s", libusb_error_name(ret));
        libusb_exit(usb_ctx->ctx);
        return -1;
    }

    ret = libusb_hotplug_register_callback(usb_ctx->ctx, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
                                           0, vid, pid, LIBUSB_HOTPLUG_MATCH_ANY, usb_event_callback, &callback_handle, &hp[0]);
    if (LIBUSB_SUCCESS != ret)
    {
        FF_LOGE("Error registering callback %s", libusb_error_name(ret));
        libusb_exit(usb_ctx->ctx);
        return -1;
    }
    return 0;
}

int mbedtls_usb_exist(uint16_t *pid, uint16_t *vid)
{
    libusb_device *dev = NULL;
    int ret = -1, i = 0;

    ret = libusb_init(&usb_ctx->ctx);
    if (ret < 0)
    {
        FF_LOGE("failed to initialise libusb: %s", libusb_error_name(ret));
        return -1;
    }

    ret = libusb_get_device_list(usb_ctx->ctx, &usb_ctx->devs);
    if (ret < 0)
    {
        FF_LOGE("no usb dev on bus");
        goto error;
    }

    while ((dev = usb_ctx->devs[i++]) != NULL)
    {
        struct libusb_device_descriptor desc;

        ret = libusb_get_device_descriptor(dev, &desc);

        if (ret < 0)
        {
            FF_LOGE("failed to get device descriptor");
            goto error;
        }

        if ((desc.idVendor == FOCAL_BOOT_VID) || (desc.idVendor == FOCAL_ROM_VID) || (desc.idVendor == FOCAL_EC_VID))
        {
            if(desc.idVendor == FOCAL_EC_VID){
				if((desc.idProduct!=FOCAL_EC_PID_TEST)&&(desc.idProduct!=FOCAL_EC_PID_9865)&&(desc.idProduct!=FOCAL_EC_PID_9869)){
					FF_LOGI("find google devive vid:%x:pid:%x,is not focal fingerprint,continue.",desc.idVendor,desc.idProduct);
					continue;
				}
	    	}
	    	ret = 0;
            *vid = desc.idVendor;
            *pid = desc.idProduct;
            FF_LOGI("find device pid %x:vid %x", desc.idProduct, desc.idVendor);
            break;
        }
    }

error:
    FF_LOGD("free device list");
    if(usb_ctx&&usb_ctx->devs){
        libusb_free_device_list(usb_ctx->devs, 1);
        usb_ctx->devs=NULL;
    }

    FF_LOGD("close the device");
    if(usb_ctx->handle){
        libusb_close(usb_ctx->handle);
        usb_ctx->handle=NULL;
    }

    if(usb_ctx->ctx){
        libusb_exit(usb_ctx->ctx);
        usb_ctx->ctx=NULL;
    }

    return ret;
}

int mbedtls_usb_connect(const uint32_t pid, const uint32_t vid)
{
    libusb_device *dev = NULL;
    libusb_context *context = NULL;
    int ret = 0, i = 0, j = 0, k = 0, l = 0;

    ret = libusb_get_device_list(usb_ctx->ctx, &usb_ctx->devs);
    if (ret < 0)
    {
        FF_LOGE("no usb dev on bus");
        goto error2;
    }

    while ((dev = usb_ctx->devs[i++]) != NULL)
    {
        struct libusb_device_descriptor desc;

        ret = libusb_get_device_descriptor(dev, &desc);

        if (ret < 0)
        {
            FF_LOGE("failed to get device descriptor");
            goto error2;
        }

        if ((desc.idVendor == vid) && (desc.idProduct == pid))
        {

            for (i = 0; i < desc.bNumConfigurations; i++)
            {
                struct libusb_config_descriptor *config;
                ret = libusb_get_config_descriptor(dev, i, &config);
                if (ret != LIBUSB_SUCCESS)
                {
                    FF_LOGE("couldn't retrieve descriptors.");
                    continue;
                }

                for (j = 0; j < config->bNumInterfaces; j++)
                {
                    const struct libusb_interface *interface = config->interface + j;
                    for (k = 0; k < interface->num_altsetting; k++)
                    {
                        const struct libusb_interface_descriptor *alt_interface = interface->altsetting + k;
                        for (l = 0; l < alt_interface->bNumEndpoints; l++)
                        {
                            const struct libusb_endpoint_descriptor *endpoint = alt_interface->endpoint + l;
                            u_endpoint_addr_t addr;
                            addr.v = endpoint->bEndpointAddress;

                            if (addr.s.dir == 1)
                            {
                                if (endpoint->bmAttributes == LIBUSB_TRANSFER_TYPE_BULK)
                                {
                                    usb_ctx->endpoint_in = endpoint->bEndpointAddress;
                                }
                            }
                            else
                            {
                                usb_ctx->endpoint_out = endpoint->bEndpointAddress;
                            }
                        }
                    }
                }

                libusb_free_config_descriptor(config);
            }

            break;
        }
    }

    usb_ctx->handle = libusb_open_device_with_vid_pid(context, vid, pid);

    if (usb_ctx->handle == NULL)
    {
        FF_LOGE("cant't open device");
        goto error2;
    }
    else
    {
        FF_LOGD("connect the device successfully.");
    }

    ret = libusb_reset_device(usb_ctx->handle);
    if (ret < 0)
    {
        FF_LOGE("failed to reset the device");
    }

    if (libusb_kernel_driver_active(usb_ctx->handle, 0) == 1)
    {
        FF_LOGD("kernel driver active, detach it ");

        if (libusb_detach_kernel_driver(usb_ctx->handle, 0) == 0)
        {
            FF_LOGD("detached kernel driver");
        }
        else
        {
            goto error1;
        }
    }

    ret = libusb_claim_interface(usb_ctx->handle, 0);

    if (ret < 0)
    {
        FF_LOGE("claim the device interface failed.");
        goto error3;
    }
    else
    {
        FF_LOGD("claim the device interface successfully.");
    }

    return 0;
error1:
    libusb_release_interface(usb_ctx->handle, 0);
error2:
    FF_LOGD("free device list.");
    libusb_free_device_list(usb_ctx->devs, 1);
error3:
    FF_LOGD("close the device.");
    libusb_close(usb_ctx->handle);

    return -1;
}

int mbedtls_usb_reconnect(const uint32_t pid, const uint32_t vid)
{
    int ret = 0;
    while (g_no_device_flag == 0)
    {
        ret = libusb_handle_events(NULL);
        if (ret < 0)
        {
            FF_LOGE("libusb_handle_events() failed: %s", libusb_error_name(ret));
        }
    }
    FF_LOGE("libusb_handle_events() g_no_device_flag: %d", g_no_device_flag);
    return ret;
}

int mbedtls_usb_send(const uint8_t *buf, uint32_t len)
{
    int ret = 0;
    int actual_len = 0;

    uint8_t edp2out = usb_ctx->endpoint_out;
    libusb_device_handle *handle = usb_ctx->handle;

    FF_LOGD("ep out=%x", edp2out);

    ff_util_hexdump(0, "send:", buf, len);

    ret = libusb_bulk_transfer(handle, edp2out, (uint8_t *)buf, len, &actual_len, 0);
    if (ret != LIBUSB_SUCCESS)
    {
        return ret; // MBEDTLS_ERR_NET_SEND_FAILED;
    }

    return (actual_len);
}

int mbedtls_usb_recv(uint8_t *buf, uint32_t len)
{
    int ret = 0;
    int actual_len = 0;
    uint8_t edp2in = usb_ctx->endpoint_in;
    libusb_device_handle *handle = usb_ctx->handle;

    ret = libusb_bulk_transfer(handle, edp2in, buf, len, &actual_len, 0);
    if (ret != LIBUSB_SUCCESS)
    {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    ff_util_hexdump(1, "recv:", buf, actual_len);

    return (actual_len);
}

int mbedtls_usb_recv_timeout(uint8_t *buf, uint32_t len, uint32_t timeout)
{
    int ret = 0;
    int actual_len = 0;

    uint8_t edp2in = usb_ctx->endpoint_in;
    FF_LOGD("ep in=%x", edp2in);

    libusb_device_handle *handle = usb_ctx->handle;

    ret = libusb_bulk_transfer(handle, edp2in, buf, len, &actual_len, timeout);

    if (ret != LIBUSB_SUCCESS)
    {
        return ret; // MBEDTLS_ERR_NET_RECV_FAILED;
    }
    FF_LOGV("recv:%d", actual_len);

    return (actual_len > len ? len : actual_len);
}

int mbedtls_usb_recv_timeout_with_ep(uint8_t *buf, uint32_t len, uint32_t timeout, uint8_t epin)
{
    int ret = 0;
    int actual_len = 0;

    FF_LOGD("ep in=%x", epin);

    libusb_device_handle *handle = usb_ctx->handle;

    ret = libusb_bulk_transfer(handle, epin, buf, len, &actual_len, timeout);

    if (ret != LIBUSB_SUCCESS)
    {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    ff_util_hexdump(0, "recv:", buf, actual_len);

    return (actual_len > len ? len : actual_len);
}

void mbedtls_usb_free(void)
{
    FF_LOGD("release the device. ");
    if(usb_ctx){

        if(callback_handle){
            FF_LOGD("deregister hot plug");
            libusb_hotplug_deregister_callback(usb_ctx->ctx,callback_handle);
            callback_handle=0;
        }

        if(usb_ctx->devs){
            libusb_free_device_list(usb_ctx->devs, 1);
        }

        if(usb_ctx->handle){
            libusb_release_interface(usb_ctx->handle, 0);
            libusb_close(usb_ctx->handle);
        }

        usb_ctx->devs=NULL;
        usb_ctx->handle=NULL;

        if(usb_ctx->ctx){
            libusb_exit(usb_ctx->ctx);
        }
        usb_ctx->ctx=NULL;
    }
}
