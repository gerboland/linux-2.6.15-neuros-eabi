#ifndef __ASM_ARCH_OMAP24XX_H
#define __ASM_ARCH_OMAP24XX_H

/*
 * Please place only base defines here and put the rest in device
 * specific headers. Note also that some of these defines are needed
 * for omap1 to compile without adding ifdefs.
 */

#define L4_24XX_BASE            0x48000000
#define L3_24XX_BASE            0x68000000

/* interrupt controller */
#define OMAP24XX_IC_BASE        (L4_24XX_BASE + 0xfe000)
#define VA_IC_BASE              IO_ADDRESS(OMAP24XX_IC_BASE)
#define OMAP24XX_IVA_INTC_BASE  0x40000000
#define IRQ_SIR_IRQ             0x0040

#define OMAP24XX_DMA_BASE       (L4_24XX_BASE + 0x00056000)
#define OMAP24XX_DISP_BASE      (L4_24XX_BASE + 0x00050000)
#define OMAP24XX_SPI1_BASE      (L4_24XX_BASE + 0x00098000)
#define OMAP24XX_SPI2_BASE      (L4_24XX_BASE + 0x0009a000)
#define OMAP24XX_SPI3_BASE      (L4_24XX_BASE + 0x000b8000)
#define OMAP24XX_UART1_BASE     (L4_24XX_BASE + 0x0006a000)
#define OMAP24XX_UART2_BASE     (L4_24XX_BASE + 0x0006c000)
#define OMAP24XX_UART3_BASE     (L4_24XX_BASE + 0x0006e000)
#define OMAP24XX_GPT2           (L4_24XX_BASE + 0x0002A000)
#define OMAP24XX_GPT3           (L4_24XX_BASE + 0x00078000)
#define OMAP24XX_GPT4           (L4_24XX_BASE + 0x0007A000)
#define OMAP24XX_GPT5           (L4_24XX_BASE + 0x0007C000)
#define OMAP24XX_GPT6           (L4_24XX_BASE + 0x0007E000)
#define OMAP24XX_GPT7           (L4_24XX_BASE + 0x00080000)
#define OMAP24XX_GPT8           (L4_24XX_BASE + 0x00082000)
#define OMAP24XX_GPT9           (L4_24XX_BASE + 0x00084000)
#define OMAP24XX_GPT10          (L4_24XX_BASE + 0x00086000)
#define OMAP24XX_GPT11          (L4_24XX_BASE + 0x00088000)
#define OMAP24XX_GPT12          (L4_24XX_BASE + 0x0008A000)
#define OMAP24XX_MAILBOX        (L4_24XX_BASE + 0x00094000)
#define OMAP24XX_MMC_SD_1_BASE  (L4_24XX_BASE + 0x0009C000)
#define OMAP24XX_MMC_SD_2_BASE  (L4_24XX_BASE + 0x000B4000)

#if defined(CONFIG_MACH_ITOMAP2430)
#define L4_WAKEUP_2430_BASE     0x49000000
#define L3_SDRC_2430_BASE       0x6d000000
#define L3_GPMC_2430_BASE       0x6e000000
#define L3_SMS_2430_BASE        0x6c000000
#define OMAP24XX_SCM_BASE       (L4_WAKEUP_2430_BASE + 0x00002000)
#define OMAP24XX_PRCM_BASE      (L4_WAKEUP_2430_BASE + 0x00006000)
#define OMAP24XX_TAP_BASE       (L4_WAKEUP_2430_BASE + 0x0000a000)
#define OMAP24XX_GPIO1_BASE     (L4_WAKEUP_2430_BASE + 0x0000c000)
#define OMAP24XX_GPIO2_BASE     (L4_WAKEUP_2430_BASE + 0x0000e000)
#define OMAP24XX_GPIO3_BASE     (L4_WAKEUP_2430_BASE + 0x00010000)
#define OMAP24XX_GPIO4_BASE     (L4_WAKEUP_2430_BASE + 0x00012000)
#define OMAP24XX_GPT1           (L4_WAKEUP_2430_BASE + 0x00018000)
#define OMAP24XX_32KSYNCT_BASE  (L4_WAKEUP_2430_BASE + 0x00020000)
#define OMAP24XX_GPIO5_BASE     (L4_24XX_BASE + 0x000b6000)
#define OMAP24XX_SDRC_BASE      (L3_SDRC_2430_BASE   + 0)
#define OMAP24XX_GPMC_BASE      (L3_GPMC_2430_BASE   + 0)
#define OMAP24XX_RTPM_BASE      (L3_24XX_BASE        + 0x00010000)
#define OMAP24XX_SMS_REG_BASE   (L3_24XX_BASE        + 0x04000000)
#else
#define OMAP24XX_SCM_BASE       (L4_24XX_BASE + 0)
#define OMAP24XX_32KSYNCT_BASE  (L4_24XX_BASE + 0x00004000)
#define OMAP24XX_PRCM_BASE      (L4_24XX_BASE + 0x00008000)
#define OMAP24XX_TAP_BASE       (L4_24XX_BASE + 0x00014000)
#define OMAP24XX_GPIO1_BASE     (L4_24XX_BASE + 0x00018000)
#define OMAP24XX_GPIO2_BASE     (L4_24XX_BASE + 0x0001a000)
#define OMAP24XX_GPIO3_BASE     (L4_24XX_BASE + 0x0001c000)
#define OMAP24XX_GPIO4_BASE     (L4_24XX_BASE + 0x0001e000)
#define OMAP24XX_GPT1           (L4_24XX_BASE + 0x00028000)
#define OMAP24XX_RTPM_BASE      (L3_24XX_BASE + 0x00005000)
#define OMAP24XX_SDRC_BASE      (L3_24XX_BASE + 0x00009000)
#define OMAP24XX_GPMC_BASE      (L3_24XX_BASE + 0x0000a000)
#define OMAP24XX_SMS_REG_BASE   (L3_24XX_BASE + 0x8000)
#endif

#endif /* __ASM_ARCH_OMAP24XX_H */

