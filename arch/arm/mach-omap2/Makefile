#
# Makefile for the linux kernel.
#

# Common support
obj-y := irq.o id.o io.o sram-fn.o memory.o prcm.o clock.o mux.o devices.o serial.o

obj-$(CONFIG_OMAP_MPU_TIMER)		+= timer-gp.o

# Power Management
obj-$(CONFIG_PM) += pm.o sleep.o

# Specific board support
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_OMAP_H4)		+= board-h4.o
obj-$(CONFIG_MACH_ITOMAP2420)		+= board-itomap2420.o
obj-$(CONFIG_MACH_ITOMAP2430)		+= board-itomap2430.o

