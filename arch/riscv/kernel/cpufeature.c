// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */

#include <linux/bitmap.h>
#include <linux/ctype.h>
#include <linux/of.h>
#include <asm/processor.h>
#include <asm/hwcap.h>
#include <asm/smp.h>
#include <asm/switch_to.h>

#include <linux/syscalls.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>
#include <vdso/vsyscall.h>
#include <asm/vdso/data.h>
#include <asm/sbi.h>

#define NUM_ALPHA_EXTS ('z' - 'a' + 1)

unsigned long elf_hwcap __read_mostly;

/* Host ISA bitmap */
static DECLARE_BITMAP(riscv_isa, RISCV_ISA_EXT_MAX) __read_mostly;

#ifdef CONFIG_FPU
__ro_after_init DEFINE_STATIC_KEY_FALSE(cpu_hwcap_fpu);
#endif

/**
 * riscv_isa_extension_base() - Get base extension word
 *
 * @isa_bitmap: ISA bitmap to use
 * Return: base extension word as unsigned long value
 *
 * NOTE: If isa_bitmap is NULL then Host ISA bitmap will be used.
 */
unsigned long riscv_isa_extension_base(const unsigned long *isa_bitmap)
{
	if (!isa_bitmap)
		return riscv_isa[0];
	return isa_bitmap[0];
}
EXPORT_SYMBOL_GPL(riscv_isa_extension_base);

SYSCALL_DEFINE3(riscv_check_extension, const char __user *, extname, unsigned long __user*, major, unsigned long __user*, minor)
{
	int len;
	struct rvext *ext_ptr;
	struct vdso_data *vdata = __arch_get_k_vdso_data();
	struct list_head *ll_ptr;
	#define MAX_ARG_STRLEN (PAGE_SIZE * 32)
	char kern_extname[MAX_ARG_STRLEN];

	len = strnlen_user(extname, MAX_ARG_STRLEN);
	#undef MAX_ARG_STRLEN
	strncpy_from_user(kern_extname, extname, len);
	ext_ptr = (struct rvext *) &(vdata[0].arch_data.buffer);
	ll_ptr  = &(ext_ptr->exts);
	for (; ll_ptr != NULL; ext_ptr++) {
	if (strcmp(ext_ptr->ext_name, kern_extname) == 0) {
		copy_to_user(major, &(ext_ptr->spec_maj), sizeof(unsigned long));
		copy_to_user(minor, &(ext_ptr->spec_min), sizeof(unsigned long));
		return 0;
		}
	ll_ptr = ll_ptr->next;
	}
	return -1;
}

/**
 * __riscv_isa_extension_available() - Check whether given extension
 * is available or not
 *
 * @isa_bitmap: ISA bitmap to use
 * @bit: bit position of the desired extension
 * Return: true or false
 *
 * NOTE: If isa_bitmap is NULL then Host ISA bitmap will be used.
 */
bool __riscv_isa_extension_available(const unsigned long *isa_bitmap, int bit)
{
	const unsigned long *bmap = (isa_bitmap) ? isa_bitmap : riscv_isa;

	if (bit >= RISCV_ISA_EXT_MAX)
		return false;

	return test_bit(bit, bmap) ? true : false;
}
EXPORT_SYMBOL_GPL(__riscv_isa_extension_available);

static inline int _decimal_part_to_uint(const char *s, unsigned int *res)
{
	unsigned int value = 0, d;

	if (!isdigit(*s))
		return -EINVAL;
	do {
		d = *s - '0';
		if (value > (UINT_MAX - d) / 10)
			return -ERANGE;
		value = value * 10 + d;
	} while (isdigit(*++s));
	*res = value;
	return 0;
}

void __init riscv_fill_hwcap(void)
{
	struct vdso_data *vdata = __arch_get_k_vdso_data();
	struct rvext *craft;
	struct list_head *iter;
	#define MAX_HART 20
	unsigned int ext_offset_n[MAX_HART] = {0};
	struct device_node *node;
	const char *isa;
	char print_str[NUM_ALPHA_EXTS + 1];
	int i, j;
	static unsigned long isa2hwcap[256] = {0};

	isa2hwcap['i'] = isa2hwcap['I'] = COMPAT_HWCAP_ISA_I;
	isa2hwcap['m'] = isa2hwcap['M'] = COMPAT_HWCAP_ISA_M;
	isa2hwcap['a'] = isa2hwcap['A'] = COMPAT_HWCAP_ISA_A;
	isa2hwcap['f'] = isa2hwcap['F'] = COMPAT_HWCAP_ISA_F;
	isa2hwcap['d'] = isa2hwcap['D'] = COMPAT_HWCAP_ISA_D;
	isa2hwcap['c'] = isa2hwcap['C'] = COMPAT_HWCAP_ISA_C;

	elf_hwcap = 0;

	bitmap_zero(riscv_isa, RISCV_ISA_EXT_MAX);

	for_each_of_cpu_node(node) {
		unsigned long this_hwcap = 0;
		DECLARE_BITMAP(this_isa, RISCV_ISA_EXT_MAX);
		const char *temp;

		if (riscv_of_processor_hartid(node) < 0)
			continue;

		if (of_property_read_string(node, "riscv,isa", &isa)) {
			pr_warn("Unable to find \"riscv,isa\" devicetree entry\n");
			continue;
		}

		vdata[riscv_of_processor_hartid(node)].arch_data.mvendor = sbi_get_mvendorid();
		vdata[riscv_of_processor_hartid(node)].arch_data.march   = sbi_get_marchid();
		vdata[riscv_of_processor_hartid(node)].arch_data.mimpl   = sbi_get_mimpid();
		vdata[riscv_of_processor_hartid(node)].arch_data.extension_head.prev = NULL;
		vdata[riscv_of_processor_hartid(node)].arch_data.extension_head.next = NULL;

		temp = isa;
#if IS_ENABLED(CONFIG_32BIT)
		if (!strncmp(isa, "rv32", 4))
			isa += 4;
#elif IS_ENABLED(CONFIG_64BIT)
		if (!strncmp(isa, "rv64", 4))
			isa += 4;
#endif
		/* The riscv,isa DT property must start with rv64 or rv32 */
		if (temp == isa)
			continue;
		bitmap_zero(this_isa, RISCV_ISA_EXT_MAX);
		for (; *isa; ++isa) {
			const char *ext = isa++;
			const char *ext_end = isa;
			unsigned int ext_major = UINT_MAX; /* default */
			unsigned int ext_minor = 0;
			bool ext_long, ext_vpair,
			     ext_err = false, ext_err_ver = false;

			switch (*ext) {
			case 's':
				/**
				 * Workaround for invalid single-letter 's' & 'u'(QEMU).
				 * No need to set the bit in riscv_isa as 's' & 'u' are
				 * not valid ISA extensions. It works until multi-letter
				 * extension starting with "Su" appears.
				 */
				if (ext[-1] != '_' && ext[1] == 'u') {
					++isa;
					ext_err = true;
					break;
				}
				fallthrough;
			case 'x':
			case 'z':
				ext_long = true;
				/* Multi-letter extension must be delimited */
				for (; *isa && *isa != '_'; ++isa)
					if (unlikely(!islower(*isa)
						     && !isdigit(*isa)))
						ext_err = true;
				/* Parse backwards */
				ext_end = isa;
				if (unlikely(ext_err))
					break;
				if (!isdigit(ext_end[-1]))
					break;
				while (isdigit(*--ext_end))
					;
				ext_vpair = (ext_end[0] == 'p')
					    && isdigit(ext_end[-1]);
				if (_decimal_part_to_uint(ext_end + 1,
							  &ext_major))
					ext_err_ver = true;
				if (!ext_vpair) {
					++ext_end;
					break;
				}
				ext_minor = ext_major;
				while (isdigit(*--ext_end))
					;
				if (_decimal_part_to_uint(++ext_end, &ext_major)
				    || ext_major == UINT_MAX)
					ext_err_ver = true;
				break;
			default:
				ext_long = false;
				if (unlikely(!islower(*ext))) {
					ext_err = true;
					break;
				}
				/* Parse forwards finding next extension */
				if (!isdigit(*isa))
					break;
				_decimal_part_to_uint(isa, &ext_major);
				if (ext_major == UINT_MAX)
					ext_err_ver = true;
				while (isdigit(*++isa))
					;
				if (*isa != 'p')
					break;
				if (!isdigit(*++isa)) {
					--isa;
					break;
				}
				if (_decimal_part_to_uint(isa, &ext_minor))
					ext_err_ver = true;
				while (isdigit(*++isa))
					;
				break;
			}
			if (*isa != '_')
				--isa;

#define SET_ISA_EXT_MAP(name, bit)						\
			do {							\
				if ((ext_end - ext == sizeof(name) - 1) &&	\
				     !memcmp(ext, name, sizeof(name) - 1)) {	\
					set_bit(bit, this_isa);			\
					craft = (struct rvext *) \
					&(vdata[riscv_of_processor_hartid(node)].arch_data.buffer[\
					sizeof(struct rvext) * ext_offset_n[riscv_of_processor_hartid(node)]]); \
					craft->spec_maj = ext_major; \
					craft->spec_min = ext_minor; \
					strncpy(craft->ext_name, tmp, ext_end-ext); \
					iter = &(vdata[riscv_of_processor_hartid(node)].arch_data.extension_head);\
					while (iter->next != NULL) { \
						iter = iter->next; \
					} \
					craft->exts.prev = iter; \
					craft->exts.next = NULL; \
					iter->next = &(craft->exts); \
					++ext_offset_n[riscv_of_processor_hartid(node)]; \
					} \
			} while (false)						\

			if (unlikely(ext_err))
				continue;
			if (!ext_long) {
				this_hwcap |= isa2hwcap[(unsigned char)(*ext)];
				set_bit(*ext - 'a', this_isa);
			} else {
				char tmp[256] = {'\0'};

				strncpy(tmp, ext, ext_end-ext);
				SET_ISA_EXT_MAP("sscofpmf", RISCV_ISA_EXT_SSCOFPMF);
				SET_ISA_EXT_MAP("zba", RISCV_ISA_EXT_zba);
				SET_ISA_EXT_MAP("zbb", RISCV_ISA_EXT_zbb);
				SET_ISA_EXT_MAP("zfh", RISCV_ISA_EXT_zfh);
			}
#undef SET_ISA_EXT_MAP
		}

		/*
		 * All "okay" hart should have same isa. Set HWCAP based on
		 * common capabilities of every "okay" hart, in case they don't
		 * have.
		 */
		if (elf_hwcap)
			elf_hwcap &= this_hwcap;
		else
			elf_hwcap = this_hwcap;

		if (bitmap_weight(riscv_isa, RISCV_ISA_EXT_MAX))
			bitmap_and(riscv_isa, riscv_isa, this_isa, RISCV_ISA_EXT_MAX);
		else
			bitmap_copy(riscv_isa, this_isa, RISCV_ISA_EXT_MAX);

	}

	/* We don't support systems with F but without D, so mask those out
	 * here. */
	if ((elf_hwcap & COMPAT_HWCAP_ISA_F) && !(elf_hwcap & COMPAT_HWCAP_ISA_D)) {
		pr_info("This kernel does not support systems with F but not D\n");
		elf_hwcap &= ~COMPAT_HWCAP_ISA_F;
	}

	memset(print_str, 0, sizeof(print_str));
	for (i = 0, j = 0; i < NUM_ALPHA_EXTS; i++)
		if (riscv_isa[0] & BIT_MASK(i))
			print_str[j++] = (char)('a' + i);
	pr_info("riscv: base ISA extensions %s\n", print_str);

	memset(print_str, 0, sizeof(print_str));
	for (i = 0, j = 0; i < NUM_ALPHA_EXTS; i++)
		if (elf_hwcap & BIT_MASK(i))
			print_str[j++] = (char)('a' + i);
	pr_info("riscv: ELF capabilities %s\n", print_str);

#ifdef CONFIG_FPU
	if (elf_hwcap & (COMPAT_HWCAP_ISA_F | COMPAT_HWCAP_ISA_D))
		static_branch_enable(&cpu_hwcap_fpu);
#endif
}
