/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/atomic.h>

extern struct k_sem shell_cc_sem;
extern atomic_t shell_pending_opcode;
extern atomic_t shell_last_status;

static int shell_hci_send_cmd(const struct shell *sh, struct net_buf *cmd_buf, const char *info)
{
	struct bt_hci_cmd_hdr *hdr = (struct bt_hci_cmd_hdr *)(cmd_buf->data + 1);
	uint16_t opcode = sys_le16_to_cpu(hdr->opcode);
	int ret;

	while (k_sem_take(&shell_cc_sem, K_NO_WAIT) == 0) {}
	atomic_set(&shell_last_status, UINT8_MAX);
	atomic_set(&shell_pending_opcode, opcode);

	shell_print(sh, "-> %s (opcode=0x%04x)", info, opcode);

	ret = bt_send(cmd_buf);
	if (ret) {
		shell_error(sh, "bt_send failed: %d", ret);
		net_buf_unref(cmd_buf);
		atomic_set(&shell_pending_opcode, UINT16_MAX);
		return ret;
	}

	ret = k_sem_take(&shell_cc_sem, K_MSEC(2000));
	atomic_set(&shell_pending_opcode, UINT16_MAX);

	if (ret) {
		shell_error(sh, "Timed out waiting for Cmd Complete (%s)", info);
		return -ETIMEDOUT;
	}

	uint8_t status = atomic_get(&shell_last_status);
	if (status != 0) {
		shell_error(sh, "%s: status error 0x%02x", info, status);
		return -EIO;
	}

	shell_print(sh, "<- %s OK", info);
	return 0;
}

/* Reused Helpers from bt_bringup.c (ADV & SCAN) */

static int hci_le_set_ext_adv_param(const struct shell *sh)
{
	struct net_buf *buf = bt_buf_get_tx(BT_BUF_CMD, K_FOREVER, NULL, 0);
	struct bt_hci_cmd_hdr *hdr = (struct bt_hci_cmd_hdr *)net_buf_add(buf, sizeof(*hdr));
	struct bt_hci_cp_le_set_ext_adv_param *param =
		(struct bt_hci_cp_le_set_ext_adv_param *)net_buf_add(buf, sizeof(*param));

	hdr->opcode = sys_cpu_to_le16(BT_HCI_OP_LE_SET_EXT_ADV_PARAM);
	hdr->param_len = sizeof(*param);
	memset(param, 0, sizeof(*param));
	param->handle = 0x0;
	param->props = sys_cpu_to_le16(0x0000);
	sys_put_le24(160, param->prim_min_interval);
	sys_put_le24(160, param->prim_max_interval);
	param->prim_channel_map = 0x07;
	param->own_addr_type = BT_ADDR_LE_PUBLIC;
	param->filter_policy = BT_LE_ADV_FP_NO_FILTER;
	param->tx_power = BT_HCI_LE_ADV_TX_POWER_NO_PREF;
	param->prim_adv_phy = BT_HCI_LE_PHY_1M;
	param->sec_adv_phy = BT_HCI_LE_PHY_1M;
	param->sid = 0x0;

	return shell_hci_send_cmd(sh, buf, "Set_Extended_Adv_Parameters");
}

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xaa, 0xfe),
	BT_DATA_BYTES(BT_DATA_SVC_DATA16,
		0xaa, 0xfe, 0x10, 0x00, 0x00,
		'z','e','p','h','y','r','p','r','o','j','e','c','t', 0x08),
};

static int hci_le_set_ext_adv_data(const struct shell *sh)
{
	size_t ad_len = 0;
	for (size_t i = 0; i < ARRAY_SIZE(adv_data); i++) {
		ad_len += 1 + 1 + adv_data[i].data_len;
	}

	uint8_t param_len = sizeof(struct bt_hci_cp_le_set_ext_adv_data) + ad_len;
	struct net_buf *buf = bt_buf_get_tx(BT_BUF_CMD, K_FOREVER, NULL, 0);
	struct bt_hci_cmd_hdr *hdr = (struct bt_hci_cmd_hdr *)net_buf_add(buf, sizeof(*hdr));
	struct bt_hci_cp_le_set_ext_adv_data *param =
		(struct bt_hci_cp_le_set_ext_adv_data *)net_buf_add(buf, sizeof(*param));

	hdr->opcode = sys_cpu_to_le16(BT_HCI_OP_LE_SET_EXT_ADV_DATA);
	hdr->param_len = param_len;
	param->handle = 0x0;
	param->op = BT_HCI_LE_EXT_ADV_OP_COMPLETE_DATA;
	param->frag_pref = BT_HCI_LE_EXT_ADV_FRAG_DISABLED;
	param->len = ad_len;

	uint8_t *ad_buf = (uint8_t *)net_buf_add(buf, ad_len);
	for (size_t i = 0; i < ARRAY_SIZE(adv_data); i++) {
		*ad_buf++ = adv_data[i].data_len + 1;
		*ad_buf++ = adv_data[i].type;
		memcpy(ad_buf, adv_data[i].data, adv_data[i].data_len);
		ad_buf += adv_data[i].data_len;
	}

	return shell_hci_send_cmd(sh, buf, "Set_Extended_Adv_Data");
}

static int hci_le_set_ext_adv_enable(const struct shell *sh, bool enable)
{
	uint8_t param_len = sizeof(struct bt_hci_cp_le_set_ext_adv_enable) + sizeof(struct bt_hci_ext_adv_set);
	struct net_buf *buf = bt_buf_get_tx(BT_BUF_CMD, K_FOREVER, NULL, 0);
	struct bt_hci_cmd_hdr *hdr = (struct bt_hci_cmd_hdr *)net_buf_add(buf, sizeof(*hdr));
	struct bt_hci_cp_le_set_ext_adv_enable *param =
		(struct bt_hci_cp_le_set_ext_adv_enable *)net_buf_add(buf, param_len);

	hdr->opcode = sys_cpu_to_le16(BT_HCI_OP_LE_SET_EXT_ADV_ENABLE);
	hdr->param_len = param_len;
	param->enable = enable ? BT_HCI_LE_ADV_ENABLE : BT_HCI_LE_ADV_DISABLE;
	param->set_num = 1;
	param->s[0].handle = 0x0;
	param->s[0].duration = 0x0;
	param->s[0].max_ext_adv_evts = 0x0;

	return shell_hci_send_cmd(sh, buf, enable ? "Enable_Ext_Adv (ON)" : "Enable_Ext_Adv (OFF)");
}

static int hci_le_set_ext_scan_param(const struct shell *sh)
{
	uint8_t param_len = sizeof(struct bt_hci_cp_le_set_ext_scan_param) +
			    sizeof(struct bt_hci_ext_scan_phy);
	struct net_buf *buf = bt_buf_get_tx(BT_BUF_CMD, K_FOREVER, NULL, 0);
	struct bt_hci_cmd_hdr *hdr = (struct bt_hci_cmd_hdr *)net_buf_add(buf, sizeof(*hdr));
	struct bt_hci_cp_le_set_ext_scan_param *param =
		(struct bt_hci_cp_le_set_ext_scan_param *)net_buf_add(buf, param_len);

	hdr->opcode = sys_cpu_to_le16(BT_HCI_OP_LE_SET_EXT_SCAN_PARAM);
	hdr->param_len = param_len;
	param->own_addr_type = BT_HCI_OWN_ADDR_RPA_OR_PUBLIC;
	param->filter_policy = BT_HCI_LE_SCAN_FP_BASIC_NO_FILTER;
	param->phys = BT_HCI_LE_EXT_SCAN_PHY_1M;
	param->p[0].type = BT_HCI_LE_SCAN_PASSIVE;
	param->p[0].interval = sys_cpu_to_le16(BT_GAP_SCAN_FAST_INTERVAL);
	param->p[0].window = sys_cpu_to_le16(BT_GAP_SCAN_FAST_WINDOW);

	return shell_hci_send_cmd(sh, buf, "Set_Extended_Scan_Parameters");
}

static int hci_le_set_ext_scan_enable(const struct shell *sh, bool enable)
{
	struct net_buf *buf = bt_buf_get_tx(BT_BUF_CMD, K_FOREVER, NULL, 0);
	struct bt_hci_cmd_hdr *hdr = (struct bt_hci_cmd_hdr *)net_buf_add(buf, sizeof(*hdr));
	struct bt_hci_cp_le_set_ext_scan_enable *param =
		(struct bt_hci_cp_le_set_ext_scan_enable *)net_buf_add(buf, sizeof(*param));

	hdr->opcode = sys_cpu_to_le16(BT_HCI_OP_LE_SET_EXT_SCAN_ENABLE);
	hdr->param_len = sizeof(*param);
	param->enable = enable ? BT_HCI_LE_SCAN_ENABLE : BT_HCI_LE_SCAN_DISABLE;
	param->filter_dup = BT_HCI_LE_SCAN_FILTER_DUP_DISABLE;
	param->duration = 0;
	param->period = 0;

	return shell_hci_send_cmd(sh, buf, enable ? "Enable_Ext_Scan (ON)" : "Enable_Ext_Scan (OFF)");
}

/* Interactive Shell Commands */

static int cmd_bt_adv_start(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Configuring and Starting Advertising...");
	hci_le_set_ext_adv_enable(sh, false);

	if (hci_le_set_ext_adv_param(sh) != 0) return -1;
	if (hci_le_set_ext_adv_data(sh) != 0) return -1;
	if (hci_le_set_ext_adv_enable(sh, true) != 0) return -1;

	shell_print(sh, "Advertising successfully started!");
	return 0;
}

static int cmd_bt_adv_stop(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Stopping Advertising...");
	if (hci_le_set_ext_adv_enable(sh, false) != 0) return -1;
	return 0;
}

static int cmd_bt_scan_start(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Configuring and Starting Scanning...");
	hci_le_set_ext_scan_enable(sh, false);

	if (hci_le_set_ext_scan_param(sh) != 0) return -1;
	if (hci_le_set_ext_scan_enable(sh, true) != 0) return -1;

	shell_print(sh, "Scanning successfully started!");
	return 0;
}

static int cmd_bt_scan_stop(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Stopping Scanning...");
	if (hci_le_set_ext_scan_enable(sh, false) != 0) return -1;
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bt_test,
	SHELL_CMD(adv_start,  NULL, "Start Eddystone Advertising", cmd_bt_adv_start),
	SHELL_CMD(adv_stop,   NULL, "Stop Advertising",            cmd_bt_adv_stop),
	SHELL_CMD(scan_start, NULL, "Start Passive Scanning",      cmd_bt_scan_start),
	SHELL_CMD(scan_stop,  NULL, "Stop Scanning",               cmd_bt_scan_stop),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(bt_test, &sub_bt_test, "BT Passthrough HW Diagnostics", NULL);
