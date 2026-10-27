// SPDX-License-Identifier: GPL-2.0
/*
 * In-kernel stand-in for the stock Android wmt_loader + wmt_launcher.
 *
 * On stock the WMT driver is driven by two vendor daemons:
 *   wmt_loader:   /dev/wmtdetect COMBO_IOCTL_GET_SOC_CHIP_ID, then
 *                 COMBO_IOCTL_SET_CHIP_ID with that ID (no external combo
 *                 chip), then COMBO_IOCTL_DO_MODULE_INIT, which in
 *                 kernel-object mode runs mtk_wcn_common_drv_init().
 *   wmt_launcher: /dev/stpwmt WMT_IOCTL_SET_STP_MODE (BTIF full mode, FM over
 *                 the common interface), then answers the WMT core's
 *                 "srh_patch" request with WMT_IOCTL_SET_PATCH_NUM /
 *                 WMT_IOCTL_SET_PATCH_INFO and writes "ok".
 * This file does the same from the WMT module itself. The MT6771 core issues
 * only "srh_patch" (srh_rom_patch is MT6765/MT6761 only; open_stp/close_stp
 * and baud_* are UART-only).
 *
 * Patch list: the stock vendor/firmware ROMv4 patches for chip 0x0788.
 * Download order and patch count come from header byte 24
 * ((count << 4) | seq); the RAM address bytes are 0 + header bytes 25..27
 * (the launcher's rule as seen on the MT6797 trace, same header layout).
 * The WMT core loads each file by this name with request_firmware().
 */

#include <linux/firmware.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "osal_typedef.h"
#include "osal.h"
#include "wmt_dev.h"
#include "wmt_core.h"
#include "wmt_lib.h"
#include "wcn_launcher.h"
#include "wmt_detect.h"
#include "mtk_wcn_cmb_stub.h"

extern UINT32 hif_info;	/* wmt_dev.c, set once the STP mode is configured */

static const char *const wcn_patches[] = {
	"ROMv4_be_patch_1_0_hdr.bin",
	"ROMv4_be_patch_1_1_hdr.bin",
};

static P_WMT_PATCH_INFO wcn_patch_info;
static struct task_struct *wcn_launcher_task;

/* Body of the stock WMT_IOCTL_SET_STP_MODE handler (wmt_dev.c). */
static INT32 wcn_set_stp_mode(ULONG arg)
{
	P_OSAL_OP pOp;
	P_WMT_HIF_CONF pHif;
	INT32 iRet;

	if (hif_info == 1)
		return 0;

	iRet = wmt_lib_set_hif(arg);
	if (iRet) {
		pr_err("wcn-launcher: wmt_lib_set_hif(0x%lx) fail %d\n", arg, iRet);
		return iRet;
	}
	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		pr_err("wcn-launcher: no free op\n");
		return -ENOMEM;
	}
	pOp->op.opId = WMT_OPID_HIF_CONF;
	pHif = wmt_lib_get_hif();
	osal_memcpy(&pOp->op.au4OpData[0], pHif, sizeof(WMT_HIF_CONF));
	pOp->op.u4InfoBit = WMT_OP_HIF_BIT;
	pOp->signal.timeoutValue = 0;
	if (wmt_lib_put_act_op(pOp) == MTK_WCN_BOOL_FALSE) {
		pr_err("wcn-launcher: WMT_OPID_HIF_CONF failed\n");
		return -EFAULT;
	}
	hif_info = 1;
	pr_info("wcn-launcher: STP mode 0x%lx set (BTIF full, FM common)\n", arg);
	return 0;
}

static INT32 wcn_srh_patch(VOID)
{
	const UINT32 n = ARRAY_SIZE(wcn_patches);
	P_WMT_PATCH_INFO info;
	const struct firmware *fw;
	UINT32 i, cnt, seq;
	INT32 ret;

	info = kcalloc(n, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		ret = request_firmware(&fw, wcn_patches[i], NULL);
		if (ret) {
			pr_err("wcn-launcher: %s: request_firmware %d\n", wcn_patches[i], ret);
			goto fail;
		}
		if (fw->size < sizeof(WMT_PATCH)) {
			pr_err("wcn-launcher: %s too small (%zu)\n", wcn_patches[i], fw->size);
			release_firmware(fw);
			ret = -EINVAL;
			goto fail;
		}
		cnt = fw->data[24] >> 4;
		seq = fw->data[24] & 0xf;
		if (cnt != n || seq == 0 || seq > n || info[seq - 1].dowloadSeq) {
			pr_err("wcn-launcher: %s: header byte 24 = 0x%02x (count %u seq %u), expected %u patches\n",
			       wcn_patches[i], fw->data[24], cnt, seq, n);
			release_firmware(fw);
			ret = -EINVAL;
			goto fail;
		}
		info[seq - 1].dowloadSeq = seq;
		info[seq - 1].addRess[0] = 0;
		info[seq - 1].addRess[1] = fw->data[25];
		info[seq - 1].addRess[2] = fw->data[26];
		info[seq - 1].addRess[3] = fw->data[27];
		strscpy(info[seq - 1].patchName, wcn_patches[i], sizeof(info[seq - 1].patchName));
		pr_info("wcn-launcher: %s seq %u address %02x %02x %02x %02x (%zu bytes)\n",
			wcn_patches[i], seq, info[seq - 1].addRess[0], info[seq - 1].addRess[1],
			info[seq - 1].addRess[2], info[seq - 1].addRess[3], fw->size);
		release_firmware(fw);
	}

	kfree(wcn_patch_info);
	wcn_patch_info = info;
	wmt_lib_set_patch_num(n);		/* WMT_IOCTL_SET_PATCH_NUM */
	wmt_lib_set_patch_info(wcn_patch_info);	/* WMT_IOCTL_SET_PATCH_INFO, all set */
	return 0;
fail:
	kfree(info);
	return ret;
}

static int wcn_launcher_thread(void *unused)
{
	P_OSAL_EVENT ev = wmt_lib_get_cmd_event();

	while (!kthread_should_stop()) {
		PUINT8 cmd;
		INT32 ret;

		ret = wait_event_interruptible(ev->waitQueue,
					       kthread_should_stop() ||
					       wmt_lib_get_cmd_status() == MTK_WCN_BOOL_TRUE);
		if (kthread_should_stop())
			break;
		if (ret < 0)
			continue;

		cmd = wmt_lib_get_cmd();	/* takes the command, as WMT_read() does */
		if (!cmd)
			continue;
		if (!strcmp(cmd, "srh_patch")) {
			ret = wcn_srh_patch();
		} else {
			pr_warn("wcn-launcher: unsupported request '%s'\n", cmd);
			ret = -EINVAL;
		}
		/* as WMT_write(): "ok" -> 0, anything else -> -1 */
		wmt_lib_trigger_cmd_signal(ret ? -1 : 0);
	}
	return 0;
}

INT32 wcn_launcher_start(VOID)
{
	UINT32 chipid;
	INT32 ret;

	/* wmt_loader: COMBO_IOCTL_GET_SOC_CHIP_ID + COMBO_IOCTL_SET_CHIP_ID */
	chipid = wmt_plat_get_soc_chipid();
	mtk_wcn_wmt_set_chipid(chipid);
	wmt_detect_set_chip_type(chipid);

	/* wmt_loader: COMBO_IOCTL_DO_MODULE_INIT (kernel-object mode) */
	ret = mtk_wcn_common_drv_init();
	if (ret) {
		pr_err("wcn-launcher: common driver init failed %d\n", ret);
		return ret;
	}

	wcn_launcher_task = kthread_run(wcn_launcher_thread, NULL, "wcn_launcher");
	if (IS_ERR(wcn_launcher_task)) {
		ret = PTR_ERR(wcn_launcher_task);
		wcn_launcher_task = NULL;
		pr_err("wcn-launcher: thread failed %d\n", ret);
		return ret;
	}

	/* wmt_launcher: WMT_IOCTL_SET_STP_MODE */
	return wcn_set_stp_mode((WMT_FM_COMM << 4) | STP_BTIF_FULL);
}

VOID wcn_launcher_stop(VOID)
{
	if (wcn_launcher_task) {
		kthread_stop(wcn_launcher_task);
		wcn_launcher_task = NULL;
	}
	kfree(wcn_patch_info);
	wcn_patch_info = NULL;
}
