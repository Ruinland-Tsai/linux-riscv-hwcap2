// SPDX-License-Identifier: GPL-2.0
/*
 * Copied from arch/arm64/kernel/vdso/vgettimeofday.c
 *
 * Copyright (C) 2018 ARM Ltd.
 * Copyright (C) 2020 SiFive
 */

#include <linux/time.h>
#include <linux/types.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>

static inline int strcmp(const char *cs, const char *ct)
{
	unsigned char c1, c2;

	while (1) {
	c1 = *cs++;
	c2 = *ct++;
	if (c1 != c2)
		return c1 < c2 ? -1 : 1;
	if (!c1)
		break;
	}
	return 0;
}

extern
int __vdso_riscv_check_extension(const char *, unsigned long *, unsigned long *);
int __vdso_riscv_check_extension(const char *query, unsigned long *maj, unsigned long *min)
{

	struct arch_vdso_data *cur;
	struct list_head *iter;
	struct rvext *ext_ptr;

	cur  = &(_vdso_data[0].arch_data);
	iter = &(cur->extension_head);
	for (ext_ptr = (struct rvext *) cur->buffer; iter != NULL; ext_ptr++) {
		if (strcmp(query, ext_ptr->ext_name) == 0) {
			*maj = ext_ptr->spec_maj;
			*min = ext_ptr->spec_min;
			return 0;
			}
		iter = ext_ptr->exts.next;
		}
	return -1;
}
