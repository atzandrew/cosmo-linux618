/* SPDX-License-Identifier: GPL-2.0 */
/* Vendor "4GB mode" (DRAM remap) is not used on mainline: drivers take their
 * general path, which programs the upper physical-address bits directly. */
#ifndef _WCN_SHIM_MTK_LPAE_H
#define _WCN_SHIM_MTK_LPAE_H
static inline int enable_4G(void) { return 0; }
#endif
