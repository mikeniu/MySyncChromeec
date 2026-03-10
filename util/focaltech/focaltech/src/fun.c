/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ff_common.h"
#include "ff_log.h"
#include "ff_sg_io.h"
#include "ff_util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <openssl/sha.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define WRITE_DATA_TO_USB DIRECTION_OUT // SG_DXFER_TO_DEV
#define READ_DATA_FROM_USB DIRECTION_IN // SG_DXFER_FROM_DEV

struct ft_config_page {
	uint32_t code_valid_control_word; /* 0x3AEC3721 for valid code */
	uint8_t reserved_04_18[0x14];
	uint32_t config_page_verification_enable; /* 0x904E3C21 for valid code
						   */
	uint8_t reserved_1B_20[0x4];
	uint32_t code_start_address;
	uint32_t code_length;
	uint8_t reserved_28_40[0x18];
	uint8_t reserved_256[256];
	uint8_t config_page_hash[32];
	uint8_t reserved_160_1F4[0x94];
	uint32_t config_page_location;
	uint8_t reserved_1F8_200[0x8];
} __attribute__((packed));

/* Helper to construct a standard 16-byte FT command block */
static void build_ft_cmd(unsigned char *cmd_buf, uint8_t opcode, uint8_t subcmd,
			 uint32_t para2, uint32_t para3, uint32_t para4,
			 uint8_t sub_para)
{
	memset(cmd_buf, 0, FT_CMD_LENGTH);
	struct ft_cmd_format *cmd = (struct ft_cmd_format *)cmd_buf;
	cmd->opcode = opcode;
	cmd->subcmd = subcmd;
	cmd->sub_para = sub_para;
	cmd->para2 = para2;
	cmd->para3 = para3;
	cmd->para4 = para4;
}

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
static int usb_write(unsigned char *cmd, int cmd_len, unsigned char *data,
		     int data_len, int flag, uint32_t timeout)
{
	struct sg_io_hdr io_hdr = { 0 };
	unsigned char sense[32];
	int ret = 0;

	io_hdr.interface_id = 'S';

	io_hdr.cmdp = cmd;
	io_hdr.cmd_len = cmd_len;

	io_hdr.dxferp = data;
	io_hdr.dxfer_len = data_len;
	io_hdr.dxfer_direction = flag;

	io_hdr.sbp = sense;
	io_hdr.mx_sb_len = sizeof(sense); // sense length
	io_hdr.timeout = timeout; //

	ret = ft_sg_io(&io_hdr);

	return ret;
}

static int ft_erase_page(int length, int addr)
{
	unsigned char transfer_cmd[FT_CMD_LENGTH] = { 0 };
	int count = 0, ret_value;
	FF_LOGD("ft_erase_page len=0x%x,addr=0x%x", length, addr);

	count = ft_div_round_up(length, FOCAL_CONFIG_SIZE);

	build_ft_cmd(transfer_cmd, FT_CMD_ACTION, FT_SUBCMD_ERASE_PAGE, addr,
		     count, FT_PARA_UNSET, FT_PARA_UNSET);

	/*it will take 4.4s in bootloader erase page*/
	ret_value = usb_write(transfer_cmd, FT_CMD_LENGTH, NULL, 0,
			      WRITE_DATA_TO_USB, 6000);

	return ret_value;
}

static int ft_get_boot_version(char *version)
{
	unsigned char transfer_cmd[FT_CMD_LENGTH] = { 0 };
	unsigned char databuffer[FT_BOOT_VERSION_MAX] = { 0 };
	int ret_value = FT_SUCCESS;
	int data_len = sizeof(databuffer);

	build_ft_cmd(transfer_cmd, FT_CMD_READ_INFO, FT_SUBCMD_READ_DATA,
		     FT_PARA_UNSET, FT_PARA_UNSET, FT_PARA_UNSET,
		     FT_PARA_UNSET);

	ret_value = usb_write(transfer_cmd, FT_CMD_LENGTH, databuffer, data_len,
			      READ_DATA_FROM_USB, 200);
	if (ret_value) {
		FF_LOGE("get boot version err");
		return ret_value;
	} else {
		databuffer[FT_BOOT_VERSION_MAX - 1] = 0;
		FF_LOGI("get boot vesion:%s", databuffer);
	}

	if (version) {
		memcpy(version, databuffer, sizeof(databuffer));
	}

	return ret_value;
}

static ft_flash_protect_area_t ft_get_flash_protect(void)
{
	ft_flash_protect_area_t wp_area = FLASH_WP_AREA_UNKNOWN;
	unsigned char flash_wp_cmd[FT_CMD_LENGTH] = { 0 };

	uint8_t data[512] = { 0 };
	int len = sizeof(data);

	build_ft_cmd(flash_wp_cmd, FT_CMD_ACTION, FT_SUBCMD_FLASH_WP_GET,
		     FT_PARA_UNSET, FT_PARA_UNSET, FT_PARA_UNSET,
		     FT_PARA_UNSET);

	int ret_value = usb_write(flash_wp_cmd, FT_CMD_LENGTH, data, len,
				  READ_DATA_FROM_USB, 200);

	if (ret_value) {
		FF_LOGE("ft_get_flash_protect err,ret=%d", ret_value);
	} else {
		FF_LOGD("wp data=%x/%x/%x/%x", data[0], data[1], data[2],
			data[3]);
		wp_area = data[0];
	}

	FF_LOGI("get flash write protect area=%d", wp_area);

	return wp_area;
}

static int ft_set_flash_protect(ft_flash_protect_area_t wp_area)
{
	int ret = FT_SUCCESS;
	unsigned char flash_wp_cmd[FT_CMD_LENGTH] = { 0 };

	FF_LOGD("set flash write protect area=%d", wp_area);

	if (wp_area <= FLASH_WP_AREA_UNKNOWN ||
	    wp_area >= FLASH_WP_AREA_OUT_RANGE) {
		FF_LOGE("Invalid wp_area");
		return FT_PARA_ERR;
	}

	uint8_t databuffer[512] = { 0 };
	int len = sizeof(databuffer);

	build_ft_cmd(flash_wp_cmd, FT_CMD_ACTION, FT_SUBCMD_FLASH_WP_SET,
		     wp_area, ~wp_area, FT_PARA_UNSET, FT_PARA_UNSET);

	ret = usb_write(flash_wp_cmd, FT_CMD_LENGTH, databuffer, len,
			READ_DATA_FROM_USB, 200);

	if (ret) {
		FF_LOGE("ft_set_flash_protect err");
		return ret;
	}

	if (ft_get_flash_protect() == wp_area) {
		FF_LOGI("set flash write protect success");
	} else {
		FF_LOGE("set flash write protect failed");
	}

	return ret;
}

static int ft_download_page(uint32_t downaddr, int per_len, unsigned char *p)
{
	int ret_value = FT_SUCCESS;
	unsigned char write_bin_cmd[FT_CMD_LENGTH] = { 0 };
	unsigned char code_bin_cmd[FT_CMD_LENGTH] = { 0 };

	build_ft_cmd(write_bin_cmd, FT_CMD_ACTION, FT_SUBCMD_WRITE_BIN,
		     FT_WRITE_SRAM_ADDR, per_len, FT_PARA_UNSET, FT_PARA_UNSET);

	build_ft_cmd(code_bin_cmd, FT_CMD_ACTION, FT_SUBCMD_CODE_BIN, downaddr,
		     per_len, FT_WRITE_SRAM_ADDR, FT_PARA_UNSET);

	ret_value = usb_write(write_bin_cmd, FT_CMD_LENGTH, p, per_len,
			      WRITE_DATA_TO_USB, 500);
	if (ret_value) {
		FF_LOGE("Write bin error!");
		goto exit;
	}

	FF_LOGV("downaddr:%x", downaddr);

	ret_value = usb_write(code_bin_cmd, FT_CMD_LENGTH, NULL, 0,
			      WRITE_DATA_TO_USB, 500);
	if (ret_value) {
		FF_LOGE("code bin error!");
	}

exit:
	return ret_value;
}

static int ft_download_bin(char *name, int addr)
{
	FILE *file = NULL;
	unsigned int file_len;
	unsigned char *buffer = NULL;
	unsigned char *p = NULL;
	int ret_value = FT_SUCCESS, cnt;

	int downaddr = addr;

	int len = 0, per_len = FOCAL_CONFIG_SIZE;

	int total_frame = 0, frame = 0;

	file = fopen(name, "rb");
	if (!file) {
		fprintf(stderr, "can not open the %s file!", name);
		ret_value = FT_NO_SUCH_FILE;
		goto exit;
	}

	file_len = calc_file_len(file);
	if (file_len > FT_EC_BIN_SIZE_MAX || file_len <= 0) {
		FF_LOGE("ec bin file size error");
		ret_value = FT_NO_SUCH_FILE;
		goto exit;
	}

	total_frame = ft_div_round_up(file_len, FOCAL_CONFIG_SIZE);

	if (addr != FOCAL_BOOT_CFG_ADDR) {
		ret_value =
			ft_erase_page(FOCAL_CONFIG_SIZE, FOCAL_ROM_CFG_ADDR);
		if (ret_value) {
			FF_LOGE("ft_erase_page error0!");
			goto exit;
		}
	}

	ret_value = ft_erase_page(file_len, addr);
	if (ret_value) {
		FF_LOGE("ft_erase_page error!");
		goto exit;
	}

	len = total_frame * FOCAL_CONFIG_SIZE;

	buffer = (unsigned char *)malloc(len);

	if (!buffer) {
		FF_LOGE("Memory error!");
		goto exit;
	}

	/*0xff means that no data has been written to the flash.*/
	memset(buffer, 0xff, len);

	cnt = fread(buffer, 1, file_len, file);
	if (cnt != file_len) {
		FF_LOGE("read bin failed");
		ret_value = FT_NO_SUCH_FILE;
		goto exit;
	}

	p = buffer;

	if (addr == FOCAL_BOOT_CFG_ADDR || addr == FOCAL_ROM_CFG_ADDR) {
		/* The config page is written to flash in the last frame;
		otherwise, the firmware is incomplete. However,
		if the config contains data and the MCU encounters an abnormal
		power failure, it will crash (run out of control).*/

		FF_LOGI("should write cfg page last");
		downaddr += FOCAL_CONFIG_SIZE;
		p += FOCAL_CONFIG_SIZE;
		len -= per_len;
	}

	while (len >= per_len) {
		ret_value = ft_download_page(downaddr, per_len, p);

		len -= per_len;
		p += per_len;
		downaddr += per_len;

		if (ret_value) {
			goto exit;
		}

		FF_LOGD("===download %d/%d ===", ++frame, total_frame);
	}

	if (len) {
		len = ft_div_round_up(len, 4);

		ret_value = ft_download_page(downaddr, len, p);
		if (ret_value) {
			FF_LOGE("code bin error!");
			goto exit;
		}
	}

	if (addr == FOCAL_BOOT_CFG_ADDR || addr == FOCAL_ROM_CFG_ADDR) {
		FF_LOGI("write cfg page start");

		ret_value = ft_download_page(addr, FOCAL_CONFIG_SIZE, buffer);
		if (ret_value) {
			FF_LOGE("code bin error!");
		}

		FF_LOGD("===download %d/%d ===", ++frame, total_frame);
	}

exit:
	if (file) {
		fclose(file);
	}

	if (buffer) {
		free(buffer);
		buffer = NULL;
	}

	return ret_value;
}

static int ft_verify_data(char *name, int addr)
{
	FILE *file = NULL;
	int file_len, cnt;
	unsigned char *buffer = NULL;
	int ret_value = FT_SUCCESS;
	int downaddr = addr;
	unsigned char verify_bin_cmd[FT_CMD_LENGTH] = { 0 };
	unsigned char read_hash[SHA256_DIGEST_LENGTH] = { 0 };

	unsigned char pHash[SHA256_DIGEST_LENGTH] = { 0 };

	int readhashlen = SHA256_DIGEST_LENGTH;
	uint8_t hash_mode = HASH_MODE_SHA256;

	if (addr == FOCAL_ROM_CFG_ADDR) {
		/*When the MCU is in ROM mode,
		this parameter differs and needs to be shifted.*/
		hash_mode = HASH_MODE_SHA256 << 4;
	}

	FF_LOGD("hash_mode=0x%x", hash_mode);

	file = fopen(name, "rb");
	if (!file) {
		FF_LOGE("can not open the %s file!", name);
		ret_value = FT_NO_SUCH_FILE;
		goto exit;
	}

	file_len = calc_file_len(file);
	if (file_len > FT_EC_BIN_SIZE_MAX || file_len <= 0) {
		FF_LOGE("ec bin file size error");
		ret_value = FT_NO_SUCH_FILE;
		goto exit;
	}

	buffer = (unsigned char *)malloc(file_len + 16);

	if (!buffer) {
		FF_LOGE("Memory alloc error!");
		goto exit;
	}
	FF_LOGD("file len=%d", file_len);
	cnt = fread(buffer, 1, file_len, file);
	if (cnt != file_len) {
		FF_LOGE("read failed");
		ret_value = FT_NO_SUCH_FILE;
		goto exit;
	}

	build_ft_cmd(verify_bin_cmd, FT_CMD_ACTION, FT_SUBCMD_VERIFY, downaddr,
		     file_len, FT_PARA_UNSET, hash_mode);

	ret_value = usb_write(verify_bin_cmd, FT_CMD_LENGTH, read_hash,
			      readhashlen, READ_DATA_FROM_USB, 1000);

	if (ret_value) {
		FF_LOGE("veriry_bin_cmd bin error!");
		goto exit;
	}

	SHA256(buffer, file_len, pHash);

	if (memcmp(pHash, read_hash, SHA256_DIGEST_LENGTH)) {
		FF_LOGE("verify fail!");

		FF_LOGD("pHash=%x,%x,%x,%x", pHash[0], pHash[1], pHash[2],
			pHash[3]);
		FF_LOGD("rHash=%x,%x,%x,%x", read_hash[0], read_hash[1],
			read_hash[2], read_hash[3]);

		ret_value = FT_RESULT_FAIL;

	} else {
		FF_LOGD("verify success!");
	}

exit:
	if (file) {
		fclose(file);
	}

	if (buffer) {
		free(buffer);
		buffer = NULL;
	}

	return ret_value;
}

static int ft_mcu_reset(unsigned int downaddr)
{
	int ret_value = FT_SUCCESS;

	unsigned char soft_reset_cmd[FT_CMD_LENGTH] = { 0 };

	build_ft_cmd(soft_reset_cmd, FT_CMD_ACTION, FT_SUBCMD_SOFT_RESET,
		     FT_PARA_UNSET, FT_PARA_UNSET, FT_PARA_UNSET, SOFT_POR);

	if (downaddr == FOCAL_ROM_CFG_ADDR || downaddr == FOCAL_BOOT_CFG_ADDR) {
		ret_value = usb_write(soft_reset_cmd, FT_CMD_LENGTH, NULL, 0,
				      DIRECTION_NONE, 200);
		FF_LOGD("reset value=%d", ret_value);
		return ret_value;
	}

	FF_LOGE("reset addr err");

	return FT_RESULT_FAIL;
}

static bool is_ec_bin_has_config_page(char *ec_bin, uint32_t *start_addr)
{
	FF_LOGV("%s start,ec_bin=%s", __func__, ec_bin);

	bool ret = true;
	int cnt;
	unsigned char buff[FOCAL_CONFIG_SIZE];
	struct ft_config_page *config = (struct ft_config_page *)buff;

	FILE *fp = fopen(ec_bin, "rb");
	if (!fp) {
		FF_LOGE("can't open %s", ec_bin);
		return false;
	}

	memset(buff, 0, FOCAL_CONFIG_SIZE);
	cnt = fread(buff, 1, FOCAL_CONFIG_SIZE, fp);
	if (cnt != FOCAL_CONFIG_SIZE) {
		FF_LOGE("read config failed");
		ret = false;
		goto exit;
	}

	if (config->code_valid_control_word != FOCAL_CODE_VALID_MAGIC) {
		ret = false;
	} else {
		*start_addr = config->code_start_address;
	}

	FF_LOGD("code_valid_control_word=%x", config->code_valid_control_word);
	FF_LOGD("config_page_verification_enable=%x",
		config->config_page_verification_enable);
	FF_LOGD("code_start_address=%x", config->code_start_address);
	FF_LOGD("code_length=%x", config->code_length);
	FF_LOGD("config_page_location=%x", config->config_page_location);

exit:
	fclose(fp);

	FF_LOGV("%s end", __func__);

	return ret;
}

static int ft_back_to_rom_boot(void)
{
	int ret = FT_SUCCESS;
	ft_erase_page(FOCAL_CONFIG_SIZE, FOCAL_ROM_CFG_ADDR);
	ret = ft_mcu_reset(FOCAL_ROM_CFG_ADDR);
	return ret;
}

int update_fw(ft_device_info_t *info)
{
	int ret = FT_SUCCESS;
	char boot_version[FT_BOOT_VERSION_MAX];

	int downaddr = FOCAL_ROM_CFG_ADDR; // 0x10000000;
	bool has_configue = false;
	uint32_t code_start_address = 0;

	char *ec_bin = info->ec_bin;

	FF_LOGI("begin_down binfile:%s", ec_bin);

	do {
		// step 1: open device
		if (info->current_mode == ROM_MODE) {
			usb_init(FOCAL_ROM_VID, FOCAL_ROM_PID);
			info->next_mode = EC_MODE;
		} else if (info->current_mode == BOOT_MODE) {
			usb_init(FOCAL_BOOT_VID, FOCAL_BOOT_PID);
		}

		// step 2: get version
		ret = ft_get_boot_version(boot_version);
		if (ret) {
			FF_LOGE("Get_boot_version error!");
			goto exit;
		}

		if (info->next_mode == FORCE_UPDATE_MODE) {
			FF_LOGD("force back to rom boot inner");
			ft_back_to_rom_boot();
			ft_sg_close();
			usleep(2000 * 1000);
			info->current_mode = ROM_MODE;
			info->next_mode = EC_MODE;
			continue;
		}

		if (info->next_mode == ROM_MODE) {
			FF_LOGD("back to rom boot inner");
			ft_back_to_rom_boot();
			ft_sg_close();
			return ret;
		}

		break;

	} while (1);

	if (info->action == FT_ACTION_WP_GET) {
		if (info->current_mode != BOOT_MODE) {
			FF_LOGE("should get flash wp in boot mode");
			ft_sg_close();
			return FT_RESULT_FAIL;
		}

		ret = ft_get_flash_protect();
		return ret;
	}

	if (info->action == FT_ACTION_WP_SET) {
		if (info->current_mode != BOOT_MODE) {
			FF_LOGE("should set flash wp in boot mode");
			ft_sg_close();
			return FT_RESULT_FAIL;
		}

		ret = ft_set_flash_protect(
			(ft_flash_protect_area_t)info->action_para);

		ft_sg_close();
		return ret;
	}

	has_configue = is_ec_bin_has_config_page(ec_bin, &code_start_address);
	if (!has_configue) {
		FF_LOGE("This firmware does not support upgrade. Please use the firmware with a config page.");
		ft_sg_close();
		return FT_RESULT_FAIL;
	}

	if (info->current_mode == BOOT_MODE) {
		FF_LOGD("is in bootloader mode");
		downaddr = FOCAL_BOOT_CFG_ADDR;

		if (code_start_address != FOCAL_BOOT_RUN_ADDR) {
			FF_LOGE("ec not for bootloader,don't use this ec");
			ft_sg_close();
			return FT_RESULT_FAIL;
		}
	} else if (info->current_mode == ROM_MODE) {
		if (code_start_address == FOCAL_BOOT_RUN_ADDR) {
			FF_LOGE("ec not for rom,don't use this ec");
			ft_sg_close();
			return FT_RESULT_FAIL;
		}

		if (code_start_address != FOCAL_ROM_RUN_ADDR &&
		    code_start_address !=
			    (FOCAL_ROM_RUN_ADDR - FOCAL_CONFIG_SIZE)) {
			FF_LOGE("ec not for rom,don't use this ec");
			ft_sg_close();
			return FT_RESULT_FAIL;
		}

	} else {
		FF_LOGE("is not in boot mode,please check again");
		ft_sg_close();
		return FT_RESULT_FAIL;
	}

	// step 3: download bin
	ret = ft_download_bin(ec_bin, downaddr);
	if (ret) {
		FF_LOGE("downloadbin error!");
		goto exit;
	}

	// step 4: verification
	ret = ft_verify_data(ec_bin, downaddr);
	if (ret) {
		FF_LOGE("VerifyData error!");
		goto exit;
	}

	FF_LOGI("Down success!");

	// step 5: reset mcu
	ret = ft_mcu_reset(downaddr);
	if (ret) {
		FF_LOGE("mcu reset error!");
		goto exit;
	}
	FF_LOGI("mcu reset success!");
	ft_sg_close();
	return ret;

exit:
	if (downaddr == FOCAL_BOOT_CFG_ADDR || downaddr == FOCAL_ROM_CFG_ADDR) {
		ft_erase_page(FOCAL_CONFIG_SIZE, downaddr);
		FF_LOGW("stay in %s mode",
			downaddr == FOCAL_BOOT_CFG_ADDR ? "boot" : "rom");
	}

	FF_LOGE("Down failed!");
	ft_sg_close();
	return FT_RESULT_FAIL;
}
