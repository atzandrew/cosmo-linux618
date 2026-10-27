/* SPDX-License-Identifier: GPL-2.0 */
/* MediaTek AEE (exception engine) is not in mainline: log instead. */
#ifndef _WCN_SHIM_AEE_H
#define _WCN_SHIM_AEE_H
#include <linux/printk.h>
#define DB_OPT_DEFAULT			0
#define DB_OPT_FTRACE			0
#define DB_OPT_NE_JBT_TRACES		0
#define DB_OPT_PRINTK_TOO_MUCH		0
#define DB_OPT_DUMMY_DUMP		0
#define aee_kernel_exception(module, msg...)	pr_err("aee exception [%s]\n", module)
#define aee_kernel_warning(module, msg...)	pr_warn("aee warning [%s]\n", module)
#define aee_kernel_warning_api(file, line, opt, module, msg...) \
	pr_warn("aee warning [%s] %s:%d\n", module, file, line)
#define aee_kernel_reminding(module, msg...)	pr_info("aee reminding [%s]\n", module)
#define aee_kernel_dal_show(msg)		pr_warn("aee dal: %s", msg)
#define aed_combo_exception(...)	pr_err("aee combo exception (CONSYS core dump)\n")
#define aed_combo_exception_api(...)	pr_err("aee combo exception (CONSYS core dump)\n")
#endif
