
#ifndef __FF_COMMON_H__
#define __FF_COMMON_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FT_BULK_EP_IN 0x81

#define FOCAL_CONFIG_SIZE 4096

#define FOCAL_BOOT_VID 0x2808
#define FOCAL_BOOT_PID 0x0001
#define FOCAL_BOOT_CFG_ADDR 0x10080000
#define FOCAL_BOOT_RUN_ADDR 0x10081000

#define FOCAL_ROM_VID 0x2fd0
#define FOCAL_ROM_PID 0x0000
#define FOCAL_ROM_CFG_ADDR 0x10000000
#define FOCAL_ROM_RUN_ADDR 0x10002000

#define FOCAL_EC_VID 0x18d1
#define FOCAL_EC_PID_TEST 0xface
#define FOCAL_EC_PID_9865 0x5403
#define FOCAL_EC_PID_9869 0x5404

#define FT_PROJECT_NAME_TEST "ft9001"
#define FT_PROJECT_NAME_9865 "chojnik"
#define FT_PROJECT_NAME_9869 "chudow"
#define FT_PROJECT_NAME_9849 "chobienia"

#define FOCAL_CODE_VALID_MAGIC 0x3AEC3721

    typedef enum ft_device_mode_e
    {
        UNKNOWN_MODE,
        ROM_MODE,
        BOOT_MODE,
        TEST_MODE,
        EC_MODE,
        RO_MODE,
        RW_MODE,
        FORCE_UPDATE_MODE,
    } ft_device_mode_t;

    typedef struct ft_device_info_st
    {

        ft_device_mode_t current_mode;
        ft_device_mode_t next_mode;
        uint32_t down_addr;

        char *version;
        char *ec_bin;

    } ft_device_info_t;

    /*get sm3 hash from openssl library*/

    unsigned char *openssl_get_hash(const unsigned char *src, size_t n, unsigned char *md);

    /*usb transfer interface via libusb */
    int mbedtls_usb_recv_timeout_with_ep(uint8_t *buf, uint32_t len, uint32_t timeout, uint8_t epin);

    void update_fw(ft_device_info_t *info);
	int focal_device_disable();

#ifdef __cplusplus
}
#endif

#endif
