// SPDX-License-Identifier: GPL-2.0
/*
 * hci_stp.c - MediaTek MT6771 CONSYS Bluetooth: BlueZ HCI driver over the
 * WMT/STP BT channel.
 *
 * Origin: the MediaTek 3.18 vendor BlueZ driver (drv_bt/linux/hci_stp.c,
 * CONFIG_MTK_COMBO_BT_HCI), as ported to 6.6 for the Gemini PDA
 * (atzandrew/gemini-nixos, kernel delta drv_bt/). The stock Cosmo 4.4 kernel
 * uses stp_chrdev_bt (/dev/stpbt) for the Android stack instead; both talk to
 * the controller the same way:
 *
 *   - power: mtk_wcn_wmt_func_on/off(WMTDRV_TYPE_BT) (WMT OPCODE_FUNC_CTRL);
 *   - TX: H4 frames -> mtk_wcn_stp_send_data(..., BT_TASK_INDX);
 *   - RX: in "bluez mode" the STP core hands each BT channel frame to the
 *     callback set with mtk_wcn_stp_register_if_rx(); an H4 parser turns the
 *     byte stream into HCI frames for hci_recv_frame().
 *
 * [port 6.18] changes against the Gemini 6.6 port:
 *   - no MT6630 vendor init script (radio/sleep/TX-power bytes are per chip;
 *     the MT6631 values live in the unit's nvdata BT_Addr record and are not
 *     sent yet): controller defaults;
 *   - BD address through hdev->set_bdaddr (MediaTek vendor command 0xfc1a,
 *     as mainline btmtk_set_bdaddr), so userspace can set the address from
 *     nvdata with "btmgmt public-addr";
 *   - no HCI_QUIRK_EXT_INIT_BEST_EFFORT (a Gemini-only net/bluetooth change);
 *   - per-packet logging at debug level;
 *   - le_cmds: the MT6631 firmware (HCI 4.2, rev 0x1731) lists no LE
 *     commands in Read Local Supported Commands (octets 25-28 = 0) although
 *     its LMP features say "LE supported" and the LE commands work. Mainline
 *     builds the LE event mask from those octets, so it enabled only the LTK
 *     request event and no LE advertising report ever arrived. The driver
 *     fills in the Bluetooth 4.0 core LE command set (mandatory for an LE
 *     controller) in that reply. Stock Android is not affected: its stack
 *     sends a fixed LE event mask.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "stp_exp.h"
#include "wmt_exp.h"

#define PFX "hci_stp: "

static int dbg;
module_param(dbg, int, 0644);
MODULE_PARM_DESC(dbg, "log every HCI frame (1 = on)");

static bool le_cmds = true;
module_param(le_cmds, bool, 0644);
MODULE_PARM_DESC(le_cmds, "fill in the LE 4.0 core commands the firmware omits from Read Local Supported Commands");

#define H4_W4_PACKET_TYPE	0
#define H4_W4_EVENT_HDR		1
#define H4_W4_ACL_HDR		2
#define H4_W4_SCO_HDR		3
#define H4_W4_DATA		4

struct hci_stp {
	struct hci_dev *hdev;
	struct sk_buff_head txq;
	struct task_struct *tx_thread;
	wait_queue_head_t tx_wq;
	bool tx_full;		/* STP BT queue full: wait for the TX event */

	/* H4 parser state; one HCI frame may span several STP callbacks.
	 * Only the STP RX context touches it while the device is open.
	 */
	struct sk_buff *rx_skb;
	unsigned int rx_count;
	int rx_state;
};

static struct hci_stp *g_hu;

/* ---- TX: one kthread drains txq into the STP BT channel ---- */

static void hci_stp_tx_kick(struct hci_stp *hu)
{
	wake_up_interruptible(&hu->tx_wq);
}

static bool hci_stp_tx_ready(struct hci_stp *hu)
{
	return kthread_should_stop() ||
	       (!skb_queue_empty(&hu->txq) && !READ_ONCE(hu->tx_full));
}

static int hci_stp_tx_thread(void *data)
{
	struct hci_stp *hu = data;
	struct sk_buff *skb;
	int len;

	while (!kthread_should_stop()) {
		if (READ_ONCE(hu->tx_full)) {
			/* the TX event normally clears tx_full; retry anyway */
			wait_event_interruptible_timeout(hu->tx_wq,
				hci_stp_tx_ready(hu), msecs_to_jiffies(20));
			WRITE_ONCE(hu->tx_full, false);
		} else {
			wait_event_interruptible(hu->tx_wq, hci_stp_tx_ready(hu));
		}

		while (!kthread_should_stop() && (skb = skb_dequeue(&hu->txq))) {
			if (dbg)
				pr_info(PFX "TX %u: %*ph\n", skb->len,
					min_t(int, skb->len, 16), skb->data);
			len = mtk_wcn_stp_send_data(skb->data, skb->len,
						    BT_TASK_INDX);
			if (len != skb->len) {
				skb_queue_head(&hu->txq, skb);
				WRITE_ONCE(hu->tx_full, true);
				break;
			}
			hu->hdev->stat.byte_tx += len;
			kfree_skb(skb);
		}
	}
	return 0;
}

/* STP TX event: the BT queue has room again */
static void hci_stp_tx_event_cb(void)
{
	if (g_hu) {
		WRITE_ONCE(g_hu->tx_full, false);
		hci_stp_tx_kick(g_hu);
	}
}

/* ---- RX: H4 parser over the STP BT channel ---- */

/* Command Complete for Read Local Supported Commands: event header (2),
 * ncmd (1), opcode (2), status (1), then the 64 command octets.
 */
static void hci_stp_fix_le_cmds(struct sk_buff *skb)
{
	/* octets 25-28: LE Set Event Mask .. LE Test End (4.0 core set) */
	static const u8 le40[4] = { 0xf7, 0xff, 0xff, 0x7f };
	u8 *c;
	int i;

	if (!le_cmds || hci_skb_pkt_type(skb) != HCI_EVENT_PKT ||
	    skb->len < 6 + 29 || skb->data[0] != HCI_EV_CMD_COMPLETE ||
	    get_unaligned_le16(skb->data + 3) != HCI_OP_READ_LOCAL_COMMANDS ||
	    skb->data[5])
		return;

	c = skb->data + 6;
	if (c[25] | c[26] | c[27] | c[28])
		return;
	for (i = 0; i < 4; i++)
		c[25 + i] = le40[i];
	pr_info_once(PFX "firmware lists no LE commands; LE 4.0 core set filled in\n");
}

static void hci_stp_rx_deliver(struct hci_stp *hu)
{
	hci_stp_fix_le_cmds(hu->rx_skb);
	hu->hdev->stat.byte_rx += hu->rx_skb->len;
	hci_recv_frame(hu->hdev, hu->rx_skb);
	hu->rx_skb = NULL;
	hu->rx_state = H4_W4_PACKET_TYPE;
}

static void hci_stp_rx_drop(struct hci_stp *hu, const char *what, int len)
{
	pr_err(PFX "%s too large (%d)\n", what, len);
	kfree_skb(hu->rx_skb);
	hu->rx_skb = NULL;
	hu->rx_state = H4_W4_PACKET_TYPE;
}

/* After a header is complete: deliver, drop, or wait for the payload. */
static void hci_stp_rx_header_done(struct hci_stp *hu)
{
	unsigned int plen;
	const char *what;

	switch (hu->rx_state) {
	case H4_W4_EVENT_HDR:
		plen = hci_event_hdr(hu->rx_skb)->plen;
		what = "event";
		break;
	case H4_W4_ACL_HDR:
		plen = __le16_to_cpu(hci_acl_hdr(hu->rx_skb)->dlen);
		what = "ACL";
		break;
	case H4_W4_SCO_HDR:
		plen = hci_sco_hdr(hu->rx_skb)->dlen;
		what = "SCO";
		break;
	default:	/* H4_W4_DATA */
		hci_stp_rx_deliver(hu);
		return;
	}

	if (!plen)
		hci_stp_rx_deliver(hu);
	else if (plen > skb_tailroom(hu->rx_skb))
		hci_stp_rx_drop(hu, what, plen);
	else {
		hu->rx_state = H4_W4_DATA;
		hu->rx_count = plen;
	}
}

static void hci_stp_rx_cb(unsigned char *data, int count)
{
	struct hci_stp *hu = g_hu;
	const unsigned char *ptr = data;
	unsigned int len;
	u8 type;

	if (!hu || !data || count <= 0)
		return;
	if (dbg)
		pr_info(PFX "RX %d: %*ph\n", count, min_t(int, count, 16), data);

	while (count > 0) {
		if (hu->rx_count) {
			len = min_t(unsigned int, hu->rx_count, count);
			skb_put_data(hu->rx_skb, ptr, len);
			hu->rx_count -= len;
			count -= len;
			ptr += len;
			if (!hu->rx_count)
				hci_stp_rx_header_done(hu);
			continue;
		}

		type = *ptr++;
		count--;
		switch (type) {
		case HCI_EVENT_PKT:
			hu->rx_state = H4_W4_EVENT_HDR;
			hu->rx_count = HCI_EVENT_HDR_SIZE;
			break;
		case HCI_ACLDATA_PKT:
			hu->rx_state = H4_W4_ACL_HDR;
			hu->rx_count = HCI_ACL_HDR_SIZE;
			break;
		case HCI_SCODATA_PKT:
			hu->rx_state = H4_W4_SCO_HDR;
			hu->rx_count = HCI_SCO_HDR_SIZE;
			break;
		default:
			pr_err(PFX "unknown H4 packet type 0x%02x\n", type);
			continue;
		}

		hu->rx_skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
		if (!hu->rx_skb) {
			hu->rx_state = H4_W4_PACKET_TYPE;
			hu->rx_count = 0;
			return;
		}
		hci_skb_pkt_type(hu->rx_skb) = type;
	}
}

/* ---- hci_dev callbacks ---- */

static void hci_stp_unhook(void)
{
	mtk_wcn_stp_register_if_rx(NULL);
	mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);
	mtk_wcn_stp_register_tx_event_cb(BT_TASK_INDX, NULL);
	mtk_wcn_stp_set_bluez(0);
}

static int hci_stp_open(struct hci_dev *hdev)
{
	struct hci_stp *hu = hci_get_drvdata(hdev);
	int tries = 0;

	skb_queue_purge(&hu->txq);
	WRITE_ONCE(hu->tx_full, false);
	kfree_skb(hu->rx_skb);
	hu->rx_skb = NULL;
	hu->rx_count = 0;
	hu->rx_state = H4_W4_PACKET_TYPE;

	/* WMT must already run (cosmo-wifi loads wmt_drv at boot). The vendor
	 * driver retries func-on for up to 60 s; 10 s is plenty here.
	 */
	while (mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT) == MTK_WCN_BOOL_FALSE) {
		if (++tries > 50) {
			bt_dev_err(hdev, "WMT BT function on failed");
			return -ENODEV;
		}
		msleep(200);
	}

	if (mtk_wcn_stp_is_ready() != MTK_WCN_BOOL_TRUE) {
		bt_dev_err(hdev, "STP not ready");
		mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT);
		return -ENODEV;
	}

	mtk_wcn_stp_register_if_rx(hci_stp_rx_cb);
	mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);
	mtk_wcn_stp_register_tx_event_cb(BT_TASK_INDX, hci_stp_tx_event_cb);
	mtk_wcn_stp_set_bluez(1);

	bt_dev_info(hdev, "WMT BT function on");
	return 0;
}

static int hci_stp_flush(struct hci_dev *hdev)
{
	struct hci_stp *hu = hci_get_drvdata(hdev);

	skb_queue_purge(&hu->txq);
	return 0;
}

static int hci_stp_close(struct hci_dev *hdev)
{
	/* No HCI_RUNNING guard: the core clears it before ->close(). */
	hci_stp_flush(hdev);
	hci_stp_unhook();

	if (mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT) == MTK_WCN_BOOL_FALSE)
		bt_dev_warn(hdev, "WMT BT function off failed");
	else
		bt_dev_info(hdev, "WMT BT function off");
	return 0;
}

static int hci_stp_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_stp *hu = hci_get_drvdata(hdev);

	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);
	skb_queue_tail(&hu->txq, skb);
	hci_stp_tx_kick(hu);
	return 0;
}

/* MediaTek vendor command: set public BD address (6 bytes, LSB first) */
static int hci_stp_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, 0xfc1a, 6, bdaddr, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "set BD address failed (%ld)", PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);
	return 0;
}

/* ---- module ---- */

static int __init hci_stp_init(void)
{
	struct hci_stp *hu;
	struct hci_dev *hdev;
	int ret;

	hu = kzalloc(sizeof(*hu), GFP_KERNEL);
	if (!hu)
		return -ENOMEM;
	skb_queue_head_init(&hu->txq);
	init_waitqueue_head(&hu->tx_wq);
	hu->rx_state = H4_W4_PACKET_TYPE;

	hdev = hci_alloc_dev();
	if (!hdev) {
		ret = -ENOMEM;
		goto err_hu;
	}
	hu->hdev = hdev;
	hdev->bus = HCI_UART;
	hci_set_drvdata(hdev, hu);
	hdev->open = hci_stp_open;
	hdev->close = hci_stp_close;
	hdev->flush = hci_stp_flush;
	hdev->send = hci_stp_send_frame;
	hdev->set_bdaddr = hci_stp_set_bdaddr;

	hu->tx_thread = kthread_run(hci_stp_tx_thread, hu, "hci_stpd");
	if (IS_ERR(hu->tx_thread)) {
		ret = PTR_ERR(hu->tx_thread);
		goto err_hdev;
	}

	g_hu = hu;
	ret = hci_register_dev(hdev);
	if (ret < 0) {
		g_hu = NULL;
		goto err_thread;
	}

	pr_info(PFX "registered %s\n", hdev->name);
	return 0;

err_thread:
	kthread_stop(hu->tx_thread);
err_hdev:
	hci_free_dev(hdev);
err_hu:
	kfree(hu);
	return ret;
}

static void __exit hci_stp_exit(void)
{
	struct hci_stp *hu = g_hu;

	hci_unregister_dev(hu->hdev);	/* closes the device if open */
	hci_stp_unhook();
	g_hu = NULL;
	kthread_stop(hu->tx_thread);
	skb_queue_purge(&hu->txq);
	kfree_skb(hu->rx_skb);
	hci_free_dev(hu->hdev);
	kfree(hu);
}

module_init(hci_stp_init);
module_exit(hci_stp_exit);

MODULE_AUTHOR("MediaTek Inc.");
MODULE_DESCRIPTION("MediaTek MT6771 CONSYS Bluetooth HCI driver over WMT/STP");
MODULE_LICENSE("GPL v2");
