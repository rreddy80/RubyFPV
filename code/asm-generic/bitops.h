/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_BITOPS_H
#define __ASM_GENERIC_BITOPS_H

/*
 * For the benefit of those who are trying to port Linux to another
 * architecture, here are some C-language equivalents.  They should
 * generate reasonable code, so take a look at what your compiler spits
 * out before rolling your own buggy implementation in assembly language.
 *
 * C language equivalents written by Theodore Ts'o, 9/26/92
 */

#include "../linux/irqflags.h>
#include "../linux/compiler.h>
#include "../asm/barrier.h>

#include "./bitops/__ffs.h>
#include "./bitops/ffz.h>
#include "./bitops/fls.h>
#include "./bitops/__fls.h>
#include "./bitops/fls64.h>

#ifndef _LINUX_BITOPS_H
#error only "../linux/bitops.h> can be included directly
#endif

#include "./bitops/sched.h>
#include "./bitops/ffs.h>
#include "./bitops/hweight.h>
#include "./bitops/lock.h>

#include "./bitops/atomic.h>
#include "./bitops/non-atomic.h>
#include "./bitops/le.h>
#include "./bitops/ext2-atomic.h>

#endif /* __ASM_GENERIC_BITOPS_H */
