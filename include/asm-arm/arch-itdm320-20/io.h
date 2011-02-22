/*
 *  linux/include/asm-arm/arch-itdm320-20/io.h
 *
 *  Copyright (C) 1999 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include <asm/arch/hardware.h>
#include <asm/arch/io_registers.h>

#define IO_SPACE_LIMIT 0xffffffff

/* Typesafe definition of __io */
static inline void __iomem *__io(unsigned long addr) {
	return (void __iomem *)(IO_BASE | addr);
}

/* Satisfy constructs like "#ifdef __io" */
#define __io(a)			__io(a)

#define __mem_pci(a)	((void __iomem *)(a))
#define __mem_isa(a)	((void __iomem *)(a))

#endif