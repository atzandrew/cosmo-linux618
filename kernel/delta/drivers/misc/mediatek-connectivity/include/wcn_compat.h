/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Compat shims for the stock 4.4 MediaTek connectivity tree (MT6771 CONSYS,
 * wlan gen3) on mainline 6.18. Force-included (-include) by the Kbuild so the
 * vendor files keep their 4.4 idioms. Pattern from the Gemini PDA 6.6 port.
 */
#ifndef _WCN_COMPAT_H
#define _WCN_COMPAT_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/timer.h>
#include <linux/rtc.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/sched/clock.h>
#include <linux/netdevice.h>
#include <net/if_inet6.h>
#include <net/addrconf.h>

/* y2038: struct timeval / struct timespec are gone from kernel code. Same
 * layouts as the 64-bit uapi originals. */
struct timeval {
	__kernel_long_t	tv_sec;
	__kernel_long_t	tv_usec;
};

struct timespec {
	__kernel_old_time_t	tv_sec;
	long			tv_nsec;
};

static inline void wcn_do_gettimeofday(struct timeval *tv)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / NSEC_PER_USEC;
}
#define do_gettimeofday(tv) wcn_do_gettimeofday(tv)

static inline void wcn_get_monotonic_boottime(struct timespec *t)
{
	struct timespec64 ts;

	ktime_get_boottime_ts64(&ts);
	t->tv_sec = ts.tv_sec;
	t->tv_nsec = ts.tv_nsec;
}
#define get_monotonic_boottime(t) wcn_get_monotonic_boottime(t)

static inline struct timespec wcn_ktime_to_timespec(ktime_t kt)
{
	struct timespec64 ts64 = ktime_to_timespec64(kt);
	struct timespec ts = { .tv_sec = ts64.tv_sec, .tv_nsec = ts64.tv_nsec };

	return ts;
}
#define ktime_to_timespec(kt) wcn_ktime_to_timespec(kt)

/* dma_alloc_coherent() always zeroes (5.0) */
#define dma_zalloc_coherent(dev, size, handle, gfp) dma_alloc_coherent(dev, size, handle, gfp)

#define rtc_time_to_tm(secs, tm)	rtc_time64_to_tm((time64_t)(secs), tm)

/* Renamed or removed APIs, same semantics. */
#define ioremap_nocache(addr, size)	ioremap(addr, size)
#define netif_rx_ni(skb)		netif_rx(skb)
#ifndef del_timer_sync
#define del_timer_sync(t)		timer_delete_sync(t)
#endif
#ifndef del_timer
#define del_timer(t)			timer_delete(t)
#endif

/* sched_setscheduler() is no longer exported to modules;
 * sched_setattr_nocheck() is the exported equivalent for kernel threads. */
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
static inline int wcn_sched_setscheduler(struct task_struct *p, int policy,
					 const struct sched_param *param)
{
	struct sched_attr attr = {
		.size = sizeof(attr),
		.sched_policy = policy,
		.sched_priority = param->sched_priority,
	};

	return sched_setattr_nocheck(p, &attr);
}
#define sched_setscheduler(p, policy, param) wcn_sched_setscheduler(p, policy, param)

#endif /* _WCN_COMPAT_H */
