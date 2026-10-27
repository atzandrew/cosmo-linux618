// SPDX-License-Identifier: GPL-2.0
/*
 * Mainline stand-in for the stock connectivity_build_in_adapter.c (the
 * built-in part of the vendor WMT driver) and for code paths the on-die
 * MT6771 CONSYS never uses.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/debug.h>
#include "osal_typedef.h"
#include "wmt_dev.h"
#include <connectivity_build_in_adapter.h>

/* Shared-memory window, set by consys_read_reg_from_dts() from memory-region
 * (stock: RESERVEDMEM_OF_DECLARE in the built-in adapter). */
phys_addr_t gConEmiPhyBase;
EXPORT_SYMBOL(gConEmiPhyBase);
unsigned long long gConEmiSize;
EXPORT_SYMBOL(gConEmiSize);

void connectivity_export_show_stack(struct task_struct *tsk, unsigned long *sp)
{
	sched_show_task(tsk);
}
EXPORT_SYMBOL(connectivity_export_show_stack);

void connectivity_export_dump_thread_state(const char *name)
{
	pr_info("wcn: thread state dump for %s not supported\n", name);
}
EXPORT_SYMBOL(connectivity_export_dump_thread_state);

void connectivity_export_dump_gpio_info(int start, int end)
{
	/* vendor GPIO register dump (debug only) */
}
EXPORT_SYMBOL(connectivity_export_dump_gpio_info);

/*
 * stp_uart: the STP line discipline for external combo chips over a UART.
 * The on-die CONSYS talks over BTIF only, and the tty ldisc API it uses no
 * longer exists, so it is not built.
 */
int mtk_wcn_stp_uart_drv_init(void)
{
	return 0;
}

int mtk_wcn_stp_uart_drv_exit(void)
{
	return 0;
}

/* SDIO combo chips only (stp_sdio); the on-die CONSYS has no SDIO card. */
int connectivity_export_mmc_io_rw_direct(struct mmc_card *card, int write, unsigned int fn,
					 unsigned int addr, u8 in, u8 *out)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(connectivity_export_mmc_io_rw_direct);
