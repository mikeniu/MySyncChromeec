/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

#include <zephyr/drivers/gpio.h>

#ifdef CONFIG_KEYBOARD_DEBUG
static uint8_t keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ 'i', KLLI_L_SHT, KLLI_UNKNO, KLLI_UNKNO, KLLI_R_CTR, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO },
	{ ']', KLLI_ESC, KLLI_L_CTR, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_F14, KLLI_UNKNO },
	{ KLLI_F1, KLLI_UNKNO, KLLI_UNKNO, KLLI_F2, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_L_ALT, KLLI_UNKNO },
	{ '\\', KLLI_UNKNO, KLLI_F6, '/', '3', KLLI_SPACE, KLLI_UNKNO,
	  KLLI_UNKNO },
	{ 'n', 'p', KLLI_UNKNO, 'l', '\'', '7', '4', 'w' },
	{ KLLI_ESC, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, 'x',
	  KLLI_UNKNO, '6' },
	{ KLLI_UNKNO, KLLI_UNKNO, '8', KLLI_UNKNO, KLLI_UNKNO, 's', KLLI_UNKNO,
	  'r' },
	{ KLLI_F7, KLLI_UNKNO, KLLI_UNKNO, 't', 'a', 'u', KLLI_F13, '.' },
	{ KLLI_F9, KLLI_UNKNO, 'g', '-', KLLI_UNKNO, 'e', KLLI_UNKNO, '0' },
	{ KLLI_F5, KLLI_UNKNO, '2', '=', 'j', KLLI_UNKNO, ',', 'z' },
	{ KLLI_R_ALT, KLLI_UNKNO, '9', KLLI_UNKNO, KLLI_UNKNO, 'f', ';', 'v' },
	{ KLLI_F11, KLLI_UNKNO, KLLI_UP, KLLI_UNKNO, KLLI_UNKNO, 'h', '`',
	  KLLI_UNKNO },
	{ KLLI_F12, 'o', KLLI_UNKNO, KLLI_F8, KLLI_UNKNO, KLLI_UNKNO, 'q',
	  KLLI_UNKNO },
	{ 'y', KLLI_F4, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, 'm', '"', 'c' },
	{ 'd', KLLI_UNKNO, 'b', '1', KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO },
	{ KLLI_LEFT, KLLI_UNKNO, KLLI_UNKNO, KLLI_SEARC, KLLI_F10, KLLI_TAB,
	  KLLI_UNKNO, KLLI_F3 },
	{ KLLI_UNKNO, KLLI_UNKNO, '[', '5', KLLI_UNKNO, KLLI_DOWN, KLLI_UNKNO,
	  'k' },
	{ KLLI_UNKNO, KLLI_RIGHT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_R_SHT, KLLI_ENTER },
};

uint8_t get_keycap_label(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS) {
		return keycap_label[col][row];
	}
	return KLLI_UNKNO;
}

void set_keycap_label(uint8_t row, uint8_t col, uint8_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS) {
		keycap_label[col][row] = val;
	}
}
#endif
