// SPDX-License-Identifier: GPL-2.0
/* Read-only: look for ATF / boot log text in reserved memory regions
 * (LK-reserved, from the boot log) and report where it is. */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/string.h>

static const struct { phys_addr_t base; size_t size; const char *name; } r[] = {
	{ 0x54400000, 0x10000, "ram_console" },
	{ 0x54600000, 0x40000, "atf-reserved" },
	{ 0x7ff00000, 0xc0000, "SSPM-reserved" },
	{ 0x7ffc0000, 0x40000, "log_store" },
	{ 0x77ff0000, 0x10000, "SPM-reserved" },
	{ 0xedb00000, 0x2440000, "tee-reserved" },
};
static const char * const pat[] = { "NOTICE:", "INFO:", "[ATF]", "spm", "[PART]", "[LK" };

static uint dump_off, dump_len;
static ulong dump_base = 0x7ffc0000;
module_param(dump_base, ulong, 0444);
module_param(dump_off, uint, 0444);
module_param(dump_len, uint, 0444);

static int __init cosmo_logscan_init(void)
{
	if (dump_len) {
		const char *d = memremap(dump_base + dump_off, dump_len, MEMREMAP_WB);
		size_t o, s = 0;

		if (!d)
			return -ENOMEM;
		for (o = 0; o <= dump_len; o++)
			if (o == dump_len || d[o] == 10 || d[o] == 0) {
				if (o > s + 2)
					pr_info("cosmo_logscan: | %.*s\n", (int)(o - s), d + s);
				s = o + 1;
			}
		memunmap((void *)d);
		return -EAGAIN;
	}
	int i, p;

	for (i = 0; i < ARRAY_SIZE(r); i++) {
		const char *m = memremap(r[i].base, r[i].size, MEMREMAP_WB);
		size_t o;

		if (!m) {
			pr_info("cosmo_logscan: %s: memremap failed\n", r[i].name);
			continue;
		}
		for (p = 0; p < ARRAY_SIZE(pat); p++) {
			size_t l = strlen(pat[p]), first = 0, last = 0, n = 0;

			for (o = 0; o + l <= r[i].size; o++)
				if (m[o] == pat[p][0] && !memcmp(m + o, pat[p], l)) {
					if (!n)
						first = o;
					last = o;
					n++;
				}
			if (n)
				pr_info("cosmo_logscan: %s 0x%llx: \"%s\" x%zu first +0x%zx last +0x%zx\n",
					r[i].name, (u64)r[i].base, pat[p], n, first, last);
		}
		memunmap((void *)m);
	}
	return -EAGAIN;
}
module_init(cosmo_logscan_init);
MODULE_LICENSE("GPL");
