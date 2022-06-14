/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RISCV_ASM_VDSO_DATA_H
#define __RISCV_ASM_VDSO_DATA_H

#include <linux/types.h>
#include <vdso/datapage.h>

struct arch_vdso_data {
	u64 mvendor;
	u64 march;
	u64 mimpl;
	struct list_head extension_head;
	char buffer[8192];
} __packed;

struct rvext {
	u64  spec_maj;
	u64  spec_min;
	char ext_name[100];
	struct list_head exts;
	};
#endif
