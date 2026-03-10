#include <fcntl.h>
#include <scsi/sg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include <memory.h>
#include <string.h>
#include <unistd.h>

#include "ff_common.h"
#include "ff_log.h"
#include "ff_sg_io.h"

#define WRITE_DATA_TO_USB DIRECTION_OUT // SG_DXFER_TO_DEV
#define READ_DATA_FROM_USB DIRECTION_IN // SG_DXFER_FROM_DEV

#define CMD_LENGTH 0x10

#define U32_SET(buf, u)                                                                                                \
    (buf)[3] = (unsigned char)((u) >> 24);                                                                             \
    (buf)[2] = (unsigned char)((u) >> 16);                                                                             \
    (buf)[1] = (unsigned char)((u) >> 8);                                                                              \
    (buf)[0] = (unsigned char)(u)

struct ft_config_page
{
    uint32_t code_valid_control_word; /* 0x3AEC3721 for valid code */
    uint8_t reserved_04_18[0x14];
    uint32_t config_page_verification_enable; /* 0x904E3C21 for valid code */
    uint8_t reserved_1B_20[0x4];
    uint32_t code_start_address;
    uint32_t code_length;
    uint8_t reserved_28_40[0x18];
    uint8_t code_sig[256];          /* RSA2048 code signature */
    uint8_t config_page_hash[32];   /* SHA256 of the config page */
    uint8_t reserved_160_1F4[0x94]; /* RSA2048 code signature */
    uint32_t config_page_location;  /* 0x301821EF for OTP */
    uint8_t reserved_1F8_200[0x8];
} __attribute__((packed));

/*
 * calculate the length of file
 */
static int calc_file_len(FILE *file)
{
    int file_len;

    fseek(file, 0, SEEK_END);
    file_len = ftell(file);
    fseek(file, 0, SEEK_SET);

    return file_len;
}

static int usb_init(uint16_t vid, uint16_t pid)
{
    int ret = 0;
    ft_sg_init();
    ret = ft_sg_open(vid, pid);

    return ret;
}
/*
 * send the data to the usb device
 * return value : 0 - success ; other - fail
 */

static int usb_write(unsigned char *cmd, int cmd_len, unsigned char *data, int data_len, int flag)
{
    struct sg_io_hdr io_hdr;

    unsigned char sense[32];

    int ret = 0;

    memset(&io_hdr, 0, sizeof(io_hdr));
    io_hdr.interface_id = 'S';

    io_hdr.cmdp = cmd;
    io_hdr.cmd_len = cmd_len;

    io_hdr.dxferp = data;
    io_hdr.dxfer_len = data_len;
    io_hdr.dxfer_direction = flag;

    io_hdr.sbp = sense;
    io_hdr.mx_sb_len = 32; // sense length
    io_hdr.timeout = 6000;//it will take 4.4s in bootloader erease page 

    ret = ft_sg_io(&io_hdr);

    return ret;
}

static int Erase_Page(int length, int addr)
{
    unsigned char transfer_cmd[] = {0xdc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0, 0};
    int count = 0, ret_value;
    FF_LOGD("Erase_Page len=0x%x,addr=0x%x",length,addr);

    count = (length + FOCAL_CONFIG_SIZE - 1) / FOCAL_CONFIG_SIZE;

    U32_SET(&transfer_cmd[1], addr);
    U32_SET(&transfer_cmd[5], count);

    ret_value = usb_write(transfer_cmd, CMD_LENGTH, NULL, 0, WRITE_DATA_TO_USB);

    return ret_value;
}

static int Get_boot_version(char *version)
{
    unsigned char transfer_cmd[] = {0xdb, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0};
    unsigned char databuffer[36] = {0};
    int ret_value = 0;
    int data_len = 36;

    ret_value = usb_write(transfer_cmd, CMD_LENGTH, databuffer, data_len, READ_DATA_FROM_USB);

    FF_LOGI("get boot vesion:%s", databuffer);

    if (version)
    {
        memcpy(version, databuffer, sizeof(databuffer));
    }

    return ret_value;
}

static int download_page(uint32_t downaddr, int per_len, unsigned char *p)
{
    int ret_value = 0;
    int tmpaddr = 0x20002000;
    unsigned char write_bin_cmd[] = {0xdc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x03, 0, 0};
    unsigned char code_bin_cmd[] = {0xdc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0, 0};

    U32_SET(&write_bin_cmd[1], tmpaddr);
    U32_SET(&write_bin_cmd[5], per_len);

    ret_value = usb_write(write_bin_cmd, CMD_LENGTH, p, per_len, WRITE_DATA_TO_USB);
    if (ret_value)
    {
        FF_LOGE("Write bin error!");
        goto exit;
    }

    FF_LOGV("downaddr:%x", downaddr);
    U32_SET(&code_bin_cmd[1], downaddr);
    U32_SET(&code_bin_cmd[5], per_len);
    U32_SET(&code_bin_cmd[9], tmpaddr);
    ret_value = usb_write(code_bin_cmd, CMD_LENGTH, NULL, 0, WRITE_DATA_TO_USB);
    if (ret_value)
    {
        FF_LOGE("code bin error!");
        goto exit;
    }

exit:
    return ret_value;
}

static int downloadbin(char *name, int addr)
{
    FILE *file = NULL;
    unsigned int file_len;
    unsigned char *buffer = NULL;
    unsigned char *p = NULL;
    int ret_value = 0, cnt;

    int downaddr = addr;

    int len = 0, per_len = FOCAL_CONFIG_SIZE;

    int total_frame = 0, frame = 0;

    file = fopen(name, "rb");
    if (!file)
    {
        fprintf(stderr, "can not open the %s file!", name);
        ret_value = -1;
        goto exit;
    }

    file_len = calc_file_len(file);
    total_frame = file_len / FOCAL_CONFIG_SIZE;
    if (file_len % FOCAL_CONFIG_SIZE)
        total_frame++;

    if (addr != FOCAL_BOOT_CFG_ADDR)
    {
        ret_value = Erase_Page(FOCAL_CONFIG_SIZE, FOCAL_ROM_CFG_ADDR);
        if (ret_value)
        {
            FF_LOGE("Erase_Page error0!");
            goto exit;
        }
    }

    ret_value = Erase_Page(file_len, addr);
    if (ret_value)
    {
        FF_LOGE("Erase_Page error!");
        goto exit;
    }

    len = total_frame * FOCAL_CONFIG_SIZE; // file_len;

    buffer = (unsigned char *)malloc(len + 16); // malloc(len+16);

    if (!buffer)
    {
        FF_LOGE("Memory error!");
        goto exit;
    }

    memset(buffer, 0xff, len); // 0xff means flash is not write

    cnt = fread(buffer, 1, file_len, file);
    if (cnt != file_len)
    {
        FF_LOGE("read bin failed");
        ret_value = -1;
        goto exit;
    }

    p = buffer;

    if (addr == FOCAL_BOOT_CFG_ADDR || addr == FOCAL_ROM_CFG_ADDR)
    {
        FF_LOGI("should write cfg page last");
        downaddr += FOCAL_CONFIG_SIZE;
        p += FOCAL_CONFIG_SIZE;
        len -= per_len;
    }

    while (len >= per_len)
    {

        ret_value = download_page(downaddr, per_len, p);

        len -= per_len;
        p += per_len;
        downaddr += per_len;

        if (ret_value)
        {
            goto exit;
        }

        FF_LOGD("===download %d/%d ===", ++frame, total_frame);
    }

    if (len)
    {
        if (len % 4)
        {
            len = (len + 3) / 4 * 4;
        }

        ret_value = download_page(downaddr, len, p);
        if (ret_value)
        {
            FF_LOGE("code bin error!");
            goto exit;
        }
    }

    if (addr == FOCAL_BOOT_CFG_ADDR || addr == FOCAL_ROM_CFG_ADDR)
    {
        FF_LOGI("write cfg page start");

        ret_value = download_page(addr, FOCAL_CONFIG_SIZE, buffer);
        if (ret_value)
        {
            FF_LOGE("code bin error!");
            goto exit;
        }

        FF_LOGD("===download %d/%d ===", ++frame, total_frame);
    }

exit:
    fclose(file);
    if (buffer)
    {
        free(buffer);
        buffer = NULL;
    }

    return ret_value;
}

static int VerifyData(char *name, int addr)
{
    FILE *file = NULL;
    int file_len, cnt;
    unsigned char *buffer = NULL;
    int ret_value = 0;
    int downaddr = addr;
    unsigned char veriry_bin_cmd[] = {0xdc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xbb, 0, 0};
    unsigned char read_hash[32] = {0};
    unsigned char pHash[32] = {0};
    int readhashlen = 32;
    file = fopen(name, "rb");
    if (!file)
    {
        FF_LOGE("can not open the %s file!", name);
        ret_value = -1;
        goto exit;
    }

    file_len = calc_file_len(file);

    buffer = (unsigned char *)malloc(file_len + 16);

    if (!buffer)
    {
        FF_LOGE("Memory error!");
        goto exit;
    }
    FF_LOGD("file len=%d", file_len);
    cnt = fread(buffer, 1, file_len, file);
    if (cnt != file_len)
    {
        FF_LOGE("read failed");
        ret_value = -1;
        goto exit;
    }

    U32_SET(&veriry_bin_cmd[1], downaddr);
    U32_SET(&veriry_bin_cmd[5], file_len);
    ret_value = usb_write(veriry_bin_cmd, CMD_LENGTH, read_hash, readhashlen, READ_DATA_FROM_USB);
    // LOG_DATA(read_hash, readhashlen);
    if (ret_value)
    {
        FF_LOGE("veriry_bin_cmd bin error!");
        goto exit;
    }

    openssl_get_hash(buffer, file_len, pHash);
    // LOG_DATA(pHash, 32);

    if (memcmp(pHash, read_hash, 32))
    {
        FF_LOGE("verify error!");

        FF_LOGD("pHash=%x,%x,%x,%x", pHash[0], pHash[1], pHash[2], pHash[3]);
        FF_LOGD("rHash=%x,%x,%x,%x", read_hash[0], read_hash[1], read_hash[2], read_hash[3]);

        ret_value = -1;
        goto exit;
    }

exit:
    fclose(file);
    if (buffer)
    {
        free(buffer);
        buffer = NULL;
    }

    return ret_value;
}

static int Disboot(unsigned int downaddr)
{
    int ret_value = 0;
    int tmpaddr = 0x20002000;
    int readaddr = 0x20003000;
    unsigned char readdata[1024] = {0};
    int readlen = 1024;
    unsigned char write_disbootdata_cmd[] = {0xdc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x03, 0, 0};
    unsigned char exec_disbootdata_cmd[] = {0xdc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xcc, 0, 0};
    unsigned char read_data_cmd[] = {0xdc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0};
    unsigned char soft_reset_cmd[] = {0xdc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xdd, 0x01, 0};
    unsigned char disbootdata[1024] = {
        0x2d, 0xe9, 0xf0, 0x41, 0x94, 0xb0, 0x2a, 0x48, 0x05, 0x90, 0x2a, 0x48, 0x06, 0x90, 0x00, 0x14, 0x29, 0x49,
        0x4f, 0xf0, 0x80, 0x58, 0xcd, 0xe9, 0x08, 0x01, 0x4f, 0xf0, 0x10, 0x21, 0x0a, 0x91, 0x26, 0x49, 0x07, 0x90,
        0x0e, 0x90, 0x0b, 0x91, 0x0f, 0x90, 0x44, 0xf6, 0x19, 0x26, 0x42, 0xf6, 0x61, 0x71, 0x42, 0xf6, 0x05, 0x75,
        0x44, 0x46, 0x40, 0x46, 0x88, 0x47, 0x0a, 0xaa, 0x01, 0x21, 0x1f, 0x48, 0xa8, 0x47, 0x05, 0xaa, 0x05, 0x21,
        0x1e, 0x48, 0xa8, 0x47, 0x20, 0x46, 0xb0, 0x47, 0xd4, 0xf8, 0x60, 0x01, 0x0c, 0x90, 0xd4, 0xf8, 0x64, 0x01,
        0x0d, 0x90, 0xd4, 0xf8, 0x70, 0x01, 0x10, 0x90, 0x20, 0x6a, 0x11, 0x90, 0x18, 0x4f, 0x18, 0x4c, 0x00, 0x20,
        0x05, 0xa9, 0x0c, 0xaa, 0x51, 0xf8, 0x20, 0xc0, 0x52, 0xf8, 0x20, 0x30, 0x63, 0x45, 0x01, 0xd0, 0x27, 0x60,
        0x12, 0xe0, 0x40, 0x1c, 0x06, 0x28, 0xf4, 0xd3, 0x01, 0xf1, 0x18, 0x02, 0x01, 0x21, 0x40, 0x46, 0xa8, 0x47,
        0x40, 0x46, 0xb0, 0x47, 0xd8, 0xf8, 0x00, 0x00, 0x12, 0x90, 0x12, 0x99, 0x0b, 0x98, 0x88, 0x42, 0xec, 0xd1,
        0x0b, 0x48, 0x20, 0x60, 0x14, 0xb0, 0x00, 0x20, 0xbd, 0xe8, 0xf0, 0x81, 0x00, 0x00, 0x66, 0x99, 0x38, 0xc0,
        0xeb, 0x0c, 0xff, 0xff, 0x02, 0x02, 0x21, 0x04, 0x21, 0x37, 0xec, 0x3a, 0x20, 0x00, 0x00, 0x10, 0x60, 0x01,
        0x00, 0x10, 0x78, 0x56, 0x34, 0x12, 0x00, 0x30, 0x00, 0x20, 0xaa, 0x55, 0xaa, 0x55,
    };

    if (downaddr == FOCAL_ROM_CFG_ADDR || downaddr == FOCAL_BOOT_CFG_ADDR)
    {

        FF_LOGD("already has config,rst mcu");
        ret_value = usb_write(soft_reset_cmd, CMD_LENGTH, NULL, 0, DIRECTION_NONE); // WRITE_DATA_TO_USB);
        FF_LOGD("ret value=%d", ret_value);
        return ret_value;
    }

    int disbootlen = sizeof(disbootdata);

    U32_SET(&write_disbootdata_cmd[1], tmpaddr);
    U32_SET(&write_disbootdata_cmd[5], disbootlen);
    ret_value = usb_write(write_disbootdata_cmd, CMD_LENGTH, disbootdata, disbootlen, WRITE_DATA_TO_USB);
    if (ret_value)
    {
        FF_LOGE("write disbootdata error!");
        goto exit;
    }

    U32_SET(&exec_disbootdata_cmd[1], tmpaddr);
    ret_value = usb_write(exec_disbootdata_cmd, CMD_LENGTH, NULL, 0, WRITE_DATA_TO_USB); // WRITE_DATA_TO_USB);
    if (ret_value)
    {
        FF_LOGE("exec disbootdata error!");
        goto exit;
    }

    U32_SET(&read_data_cmd[1], readaddr);
    U32_SET(&read_data_cmd[5], readlen);
    ret_value = usb_write(read_data_cmd, CMD_LENGTH, readdata, readlen, READ_DATA_FROM_USB); // DIRECTION_NONE
    // LOG_DATA(readdata, readlen);
    if (ret_value)
    {
        FF_LOGE("read disboot data error!");
        goto exit;
    }

    if ((readdata[0] != 0xaa) || (readdata[1] != 0x55) || (readdata[2] != 0xaa) || (readdata[3] != 0x55))
    {
        FF_LOGE("disboot error!");
        FF_LOGD("readdata=%x,%x,%x,%x", readdata[0], readdata[1], readdata[2], readdata[3]);
        ret_value = -1;

        goto exit;
    }
    else
    {
        U32_SET(&soft_reset_cmd[1], tmpaddr);
        ret_value = usb_write(soft_reset_cmd, CMD_LENGTH, NULL, 0, DIRECTION_NONE); // WRITE_DATA_TO_USB);
    }

exit:
    return ret_value;
}

static int is_ec_bin_has_confige_page(char *ec_bin, uint32_t *start_addr)
{
    FF_LOGV("%s start,ec_bin=%s", __func__, ec_bin);

    int ret = 1, cnt;
    unsigned char buff[FOCAL_CONFIG_SIZE];
    struct ft_config_page *config = (struct ft_config_page *)buff;

    FILE *fp = fopen(ec_bin, "rb");
    if (!fp)
    {
        FF_LOGE("can't open %s", ec_bin);
        return 0;
    }

    memset(buff, 0, FOCAL_CONFIG_SIZE);
    cnt = fread(buff, 1, FOCAL_CONFIG_SIZE, fp);
    if (cnt != FOCAL_CONFIG_SIZE)
    {
        FF_LOGE("read config failed");
        ret = 0;
        goto exit;
    }

    if (config->code_valid_control_word != FOCAL_CODE_VALID_MAGIC)
    {
        ret = 0;
    }
    else
    {
        *start_addr = config->code_start_address;
    }

    FF_LOGD("code_valid_control_word=%x", config->code_valid_control_word);
    FF_LOGD("config_page_verification_enable=%x", config->config_page_verification_enable);
    FF_LOGD("code_start_address=%x", config->code_start_address);
    FF_LOGD("code_length=%x", config->code_length);
    FF_LOGD("config_page_location=%x", config->config_page_location);
    FF_LOGD("config_page_hash=%x,%x,%x,%x", config->reserved_160_1F4[0], config->reserved_160_1F4[1],
            config->reserved_160_1F4[2], config->reserved_160_1F4[3]);

exit:
    fclose(fp);

    FF_LOGV("%s end", __func__);

    return ret;
}

static int ft_back_to_rom_boot()
{
    int ret = 0;
    Erase_Page(FOCAL_CONFIG_SIZE, FOCAL_ROM_CFG_ADDR);
    ret = Disboot(FOCAL_ROM_CFG_ADDR);
    return ret;
}

void update_fw(ft_device_info_t *info)
{
    int ret;
    char boot_version[64];

    int downaddr = FOCAL_ROM_CFG_ADDR; // 0x10000000;
    int has_configue = 0;
    uint32_t code_start_address = 0;

    char *ec_bin = info->ec_bin;

    FF_LOGI("begin_down binfile:%s", ec_bin);

    do
    {
        // step 1: open device
        if (info->current_mode == ROM_MODE)
        {
            usb_init(FOCAL_ROM_VID, FOCAL_ROM_PID);
            info->next_mode = EC_MODE;
        }
        else if (info->current_mode == BOOT_MODE)
        {
            usb_init(FOCAL_BOOT_VID, FOCAL_BOOT_PID);
        }

        // step 2: get version
        ret = Get_boot_version(boot_version);
        if (ret)
        {
            FF_LOGE("Get_boot_version error!");
            goto END;
        }

        if (info->next_mode == FORCE_UPDATE_MODE)
        {
            FF_LOGD("force back to rom boot inner");
            ft_back_to_rom_boot();
            ft_sg_close();
            usleep(2000 * 1000);
            info->current_mode = ROM_MODE;
            info->next_mode = EC_MODE;
            continue;
        }

        if (info->next_mode == ROM_MODE)
        {
            FF_LOGD("back to rom boot inner");
            ft_back_to_rom_boot();
            return;
        }

        break;

    } while (1);

    has_configue = is_ec_bin_has_confige_page(ec_bin, &code_start_address);

    if (info->current_mode == BOOT_MODE)
    {
        FF_LOGD("is in bootloader mode");
        downaddr = FOCAL_BOOT_CFG_ADDR;

        if (code_start_address != FOCAL_BOOT_RUN_ADDR)
        {
            FF_LOGE("ec not for bootloader,don't use this ec");
            return;
        }
    }
    else if (info->current_mode == ROM_MODE)
    {

        if (code_start_address == FOCAL_BOOT_RUN_ADDR)
        {
            FF_LOGE("ec not for rom,don't use this ec");
            return;
        }
        if (!has_configue)
        {
            downaddr += FOCAL_CONFIG_SIZE;
        }
        else
        {
            if (code_start_address != FOCAL_ROM_RUN_ADDR&&code_start_address !=(FOCAL_ROM_RUN_ADDR-FOCAL_CONFIG_SIZE))
            {
                FF_LOGE("ec not for rom,don't use this ec");
                return;
            }
        }
    }
    else
    {
        FF_LOGE("is not in boot mode,please check again");
        return;
    }

    // step 3: download bin
    ret = downloadbin(ec_bin, downaddr);
    if (ret)
    {
        FF_LOGE("downloadbin error!");
        goto END;
    }

    // step 4: verification
    ret = VerifyData(ec_bin, downaddr);
    if (ret)
    {
        FF_LOGE("VerifyData error!");
        goto END;
    }

    FF_LOGI("Down success!");

    // step 5: disboot
    ret = Disboot(downaddr);
    if (ret)
    {
        FF_LOGE("Disboot error!");
        goto END;
    }
    FF_LOGI("Disboot success!");

    return;

END:
    if (downaddr == FOCAL_BOOT_CFG_ADDR || downaddr == FOCAL_ROM_CFG_ADDR)
    {
        Erase_Page(FOCAL_CONFIG_SIZE, downaddr);
        FF_LOGI("stay in boot mode");
    }

    FF_LOGE("Down failed!");
    exit(-1);
    return;
}
