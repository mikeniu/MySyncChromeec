/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* OTP memory module for Chrome EC */

#ifndef PLATFORM_EC_INCLUDE_OTP_H_
#define PLATFORM_EC_INCLUDE_OTP_H_

/*
 * OTP: One Time Programable memory is used for storing persistent data.
 */

/**
 * Set the serial number in OTP memory.
 *
 * @param serialno	ascii serial number string.
 *
 * @return success status.
 */
int otp_write_serial(const char *serialno);

/**
 * Get the serial number from flash.
 *
 * @return char * ascii serial number string.
 *     NULL if error.
 */
const char *otp_read_serial(void);

#endif /* PLATFORM_EC_INCLUDE_OTP_H_ */
