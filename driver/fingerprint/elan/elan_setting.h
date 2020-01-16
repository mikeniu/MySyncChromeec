/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Private sensor interface */

#ifndef _ELAN_SETTING_H
#define _ELAN_SETTING_H

#include <stdint.h>

#define CONFIG_SPI_TX_BUF_SIZE		1024
#define CONFIG_SPI_RX_BUF_SIZE		1024
#define gucIOIRQ					0x08

#define WRITE_REG_HEAD				0x8
#define READ_REG_HEAD               0x40
#define READ_SERIER_REG_HEAD		0xC0
#define START_SCAN					0x01
#define START_READ_IMAGE			0x10
#define SRST						0x31
#define FP_Page0					0x00
#define FP_Page1					0x01
#define FUSE_LOAD					0x04

#define _imageWidth                 80
#define _imageHeight                80
#define FP_DUMMY_BYTE				2
#define raw_byts					2
#define _RawCaptue_TIME_OUT_THD 10000
#define _imageTotalPixel (_imageWidth*_imageHeight)

#define _CalibrationTarget                        3000
#define _LowBound_ComputeMean                     1000
#define _HighBound_ComputeMean                    10000
#define _BaseAverageTimes                         4
#define _FingerOnTHD                              750
#define _FingerDownTHD                            800
#define _isAlertEnrollingDuplicateEnable          1
#define _isSelfCheckEnrollingDuplicate            1
#define _SelfCheckEnrollingDuplicate_threshold    92
#define _isSameFingerRejectionEnable              1
#define _base_StdThreshold                        2100

#define _Rek_Times			3
#define _WOE_IDEL_TIME		0x19

#define LOGE_SA(format, args...) cprints(CC_FP, format, ## args)

int ElanFP_WOEMODE(void);

int ElanFP_SENSINGMODE(void);

int RawCapture(unsigned short *pShortRaw);

void algorithm_parameter_setting(void);

int ElanFP_ExcuteCalibration(void);

int elan_match(void *templ, uint32_t templ_count, uint8_t *image,
		    int32_t *match_index, uint32_t *update_bitmap);

int elan_enrollment_begin(void);

int elan_enroll(uint8_t *image, int *completion);

int elan_sensor_acquire_image_with_mode(uint8_t *image_data, int mode);

enum finger_state elan_sensor_finger_status(void);

int elan_enrollment_finish(void *templ);

#endif /* _ELAN_SETTING_H */
