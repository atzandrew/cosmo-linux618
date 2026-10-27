/* SPDX-License-Identifier: GPL-2.0 */
/* RTC 32k export for external combo chips. The MT6771 CONSYS is on-die and
 * takes its clock from the SoC, so this is never needed here. */
#ifndef _WCN_SHIM_RTC_MISC_H
#define _WCN_SHIM_RTC_MISC_H
enum rtc_gpio_user_t { RTC_GPIO_USER_WIFI = 8, RTC_GPIO_USER_GPS = 9, RTC_GPIO_USER_BT = 10,
	RTC_GPIO_USER_FM = 11, RTC_GPIO_USER_PMIC = 12 };
static inline void rtc_gpio_enable_32k(enum rtc_gpio_user_t user) { }
static inline void rtc_gpio_disable_32k(enum rtc_gpio_user_t user) { }
static inline bool rtc_gpio_32k_status(void) { return true; }
#endif
