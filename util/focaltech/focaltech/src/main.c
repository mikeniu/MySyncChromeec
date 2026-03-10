#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <limits.h>

#include "ff_common.h"
#include "ff_log.h"
#include "ff_util.h"
#include "libusb.h"
#include "net_usb.h"

static int ft_switch_to_rom()
{
    int ret = 0;
    uint16_t tx_len = 8;
    uint8_t tx_buffer[64] = {
        0x03, 0x00, 0xce, 0xfa, 0x00, 0xaa, 0x06,
    };
    uint8_t rx_buffer[64] = {0};

    int retry = 3;

    while (retry--)
    {
        ret = mbedtls_usb_send((uint8_t *)tx_buffer, tx_len);
        FF_LOGD("send ret=%x", ret);
        if (ret != tx_len && (ret == LIBUSB_ERROR_IO || ret == LIBUSB_ERROR_NO_DEVICE))
        {
            FF_LOGW("mcu reset to rom ,can't send data to device");
            break;
        }

        ret = mbedtls_usb_recv_timeout((uint8_t *)rx_buffer, 4, 200);
        FF_LOGD("ret=%x,recv %x,%x,%x,%x", ret, rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3]);
        if (ret > 0 || (ret == LIBUSB_ERROR_IO || ret == LIBUSB_ERROR_NO_DEVICE))
        {
            FF_LOGW("mcu reset ,can't recv data from usb device");
            break;
        }

        usleep(10 * 1000);
    }

    return ret;
}

static int ft_switch_to_boot()
{
    int ret = 0;
    uint16_t tx_len = 8;
    uint8_t tx_buffer[64] = {
        0x03,
        0x1b,
        0xe2,
    };
    uint8_t rx_buffer[64] = {0};
    FF_LOGD("%s enter", __func__);

    int retry = 3;

    while (retry--)
    {
        ret = mbedtls_usb_send((uint8_t *)tx_buffer, tx_len);
        FF_LOGD("send ret=%x", ret);
        if (ret != tx_len && (ret == LIBUSB_ERROR_IO || ret == LIBUSB_ERROR_NO_DEVICE))
        {
            FF_LOGW("mcu reset to bootloader,can't send data to device");
            break;
        }

        ret = mbedtls_usb_recv_timeout((uint8_t *)rx_buffer, 4, 200);
        FF_LOGD("ret=%x,recv %x,%x,%x,%x", ret, rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3]);
        if (ret > 0 || (ret == LIBUSB_ERROR_IO || ret == LIBUSB_ERROR_NO_DEVICE))
        {
            FF_LOGW("mcu reset ,can't recv data from usb device");
            break;
        }

        usleep(10 * 1000);
    }

    return ret;
}
static int ft_get_ec_build_version()
{
#define MAX_RECV_LEN 0x400

    int ret = 0;
    int retry = 3;

    uint8_t tx_buffer[128] = {
        0x03,
        0xf9,
        0x04,
    };
    uint8_t rx_buffer[MAX_RECV_LEN] = {0};
    uint16_t tx_len = 8, rx_len = sizeof(rx_buffer);
    char info[MAX_RECV_LEN];
    FF_LOGV("%s enter", __func__);

    memset(info, 0, sizeof(info));

    while (retry--)
    {
        mbedtls_usb_send((uint8_t *)tx_buffer, tx_len);
        ret = mbedtls_usb_recv_timeout((uint8_t *)rx_buffer, rx_len, 200);
        if (ret > 0)
        {
            break;
        }
        usleep(10 * 1000);
    }

    memcpy(info, rx_buffer + 8, sizeof(info) - 9);
    FF_LOGV("rx_buffer =%x,%x,%x,%x", rx_buffer[0], rx_buffer[1], rx_buffer[8], rx_buffer[9]);

    FF_LOGI("ec build version: %s", info);

    FF_LOGV("ret=%x", ret);

    if (strstr(info, FT_PROJECT_NAME_TEST) || strstr(info, FT_PROJECT_NAME_9865) || strstr(info, FT_PROJECT_NAME_9869) || strstr(info, FT_PROJECT_NAME_9849))
    {
        ret = 0;
    }
    else
    {
        ret = -2;
    }

    return ret;
}

static int ft_device_init(ft_device_info_t *info)
{
    uint16_t pid, vid;
    int ret = 0;

    ret = mbedtls_usb_exist(&pid, &vid);

    if (ret)
    {
        FF_LOGE("can't find focal device !");
        return -1;
    }

    if (info)
    {

        memset(info, 0, sizeof(ft_device_info_t));

        switch (vid)
        {

        case FOCAL_ROM_VID:
            info->current_mode = ROM_MODE;
            break;
        case FOCAL_BOOT_VID:
            info->current_mode = BOOT_MODE;
            break;
        case FOCAL_EC_VID:
            if (pid == FOCAL_EC_PID_TEST)
            {
                info->current_mode = TEST_MODE;
            }
            else
            {
                info->current_mode = EC_MODE;
            }
            break;
        default:
            info->current_mode = UNKNOWN_MODE;
            break;
        }
    }

    if (!info->current_mode)
    {
        FF_LOGE("can't find focal device,please check again");
        return -1;
    }

    mbedtls_usb_init(pid, vid);
    ret = mbedtls_usb_connect(pid, vid);
    if (ret < 0)
    {
        FF_LOGE("open vid(%x) pid(%x) failed\n", vid, pid);
        return ret;
    }

    return 0;
}

int main(int argc, char **argv)
{
    int ret = 0;
    ft_device_info_t info;

    char tmp[PATH_MAX];

    if (argc < 2 || argc > 3)
    {
        FF_LOGE("para err");
        return -1;
    }
    else
    {

        ret = ft_device_init(&info);
        if (ret < 0)
        {
            return -2;
        }

        if (info.current_mode != ROM_MODE && info.current_mode != BOOT_MODE)
        {
            if (ft_get_ec_build_version())
            {
                FF_LOGE("This module is not focal device,please check again!");
                return -2;
            }
        }

        if (argc == 3)
        {
            memset(tmp, 0, sizeof(tmp));
            strcpy(tmp, argv[2]);
            if (!memcmp(tmp, "force_update_full_ec_bin", sizeof("force_update_full_ec_bin")))
            {
                FF_LOGD("force update ec bin");
                info.next_mode = FORCE_UPDATE_MODE;
            }
            else
            {
                FF_LOGE("para err");
                return -1;
            }
        }

        memset(tmp, 0, sizeof(tmp));
        info.ec_bin = argv[1];

        strcpy(tmp, argv[1]);

        if (!memcmp(tmp, "back_to_second_boot", sizeof("back_to_second_boot")))
        {
            FF_LOGD("back to second boot");
            info.next_mode = BOOT_MODE;

            ft_switch_to_boot();
            mbedtls_usb_free();
            return 0;
        }

        if (!memcmp(tmp, "back_to_bootloader", sizeof("back_to_bootloader")))
        {
            FF_LOGD("back to  bootloader");
            info.next_mode = BOOT_MODE;

            ft_switch_to_boot();
            mbedtls_usb_free();
            return 0;
        }

        if (!memcmp(tmp, "back_to_rom_boot", sizeof("back_to_rom_boot")))
        {
            FF_LOGD("back to rom boot");
            info.next_mode = ROM_MODE;
            mbedtls_usb_free();
            update_fw(&info);
            return 0;
        }
		
		if (!memcmp(tmp, "enter_usb_suspend", sizeof("enter_usb_suspend")))
        {
            mbedtls_usb_free();
            focal_device_disable();
            return 0;
        }
        
        if (!memcmp(tmp, "enter_usb_resume", sizeof("enter_usb_resume")))
        {
            mbedtls_usb_free();
            return 0;
        }

        if (info.next_mode != FORCE_UPDATE_MODE)
        {
            info.next_mode = EC_MODE;
        }
    }
    
    if (!memcmp(tmp, "get_ec_version", sizeof("get_ec_version")))
    {
            //already show fw version before.
            return 0;
    }

    if (access(info.ec_bin, F_OK))
    {
        FF_LOGE("can't find %s", info.ec_bin);
        return -1;
    }

    if (info.current_mode == TEST_MODE)
    {
        ft_switch_to_rom();
        mbedtls_usb_free();
        usleep(2000 * 1000);
        info.current_mode = ROM_MODE;
    }

    if (info.current_mode == EC_MODE && info.next_mode == FORCE_UPDATE_MODE)
    {
        ft_switch_to_boot();
        mbedtls_usb_free();
        usleep(2000 * 1000);
        info.current_mode = BOOT_MODE;
    }

    if (info.current_mode == ROM_MODE)
    {
        info.next_mode = EC_MODE;
    }

    mbedtls_usb_free();
    update_fw(&info);

    return 0;
}
