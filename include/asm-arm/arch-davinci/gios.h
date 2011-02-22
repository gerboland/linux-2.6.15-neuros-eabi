#ifndef __ASM_ARCH_GIOS_H
#define __ASM_ARCH_GIOS_H

#define GIO_ETHER 3

#define GIO_TV_DET_INT 17  //was GIO_USB_ENABLE 
#define GIO_CFC_HOTPLUG 20
#define GIO_CFC_DETECT  12 // connected to CD1# signal
#define GIO_CFC_RESET   28

#define GIO_I2C_SCL     43
#define GIO_I2C_SDA     44


#define GIO_PCF8574_BASE 128 // external GPIOs on PCF8574 expander 
#define GIO_EXP_I2C_BASE_ADDR 0x38

#define GIO_EXP_MIN       GIO_PCF8574_BASE
#define GIO_EXP_38_BASE   GIO_PCF8574_BASE
#define GIO_EXP_39_BASE   (GIO_EXP_38_BASE+8)
#define GIO_EXP_3A_BASE   (GIO_EXP_39_BASE+8)
#define GIO_EXP_3B_BASE   (GIO_EXP_3A_BASE+8)
#define GIO_EXP_3C_BASE   (GIO_EXP_3B_BASE+8)
#define GIO_EXP_3D_BASE   (GIO_EXP_3C_BASE+8)
#define GIO_EXP_3E_BASE   (GIO_EXP_3D_BASE+8)
#define GIO_EXP_MAX     (GIO_EXP_3E_BASE+8)


#if defined( CONFIG_MACH_DAVINCI_DVDP)

#define GIO_EXP_USER_LED_DS8      (GIO_EXP_38_BASE+0)
#define GIO_EXP_USER_LED_DS7      (GIO_EXP_38_BASE+1)
#define GIO_EXP_USER_LED_DS6      (GIO_EXP_38_BASE+2)
#define GIO_EXP_USER_LED_DS5      (GIO_EXP_38_BASE+3)
#define GIO_EXP_USER_LED_DS4      (GIO_EXP_38_BASE+4)
#define GIO_EXP_USER_LED_DS3      (GIO_EXP_38_BASE+5)
#define GIO_EXP_USER_LED_DS2      (GIO_EXP_38_BASE+6)
#define GIO_EXP_USER_LED_DS1      (GIO_EXP_38_BASE+7)

#define GIO_EXP_PLL1705_CSEL      (GIO_EXP_39_BASE+0)
#define GIO_EXP_PLL1705_SR        (GIO_EXP_39_BASE+1)
#define GIO_EXP_PLL1705_FS1       (GIO_EXP_39_BASE+2)
#define GIO_EXP_PLL1705_FS2       (GIO_EXP_39_BASE+3)
#define GIO_EXP_SPAREIO1          (GIO_EXP_39_BASE+4)
#define GIO_EXP_SPAREIO2          (GIO_EXP_39_BASE+5)
#define GIO_EXP_SPAREIO3          (GIO_EXP_39_BASE+6)
#define GIO_EXP_USER_DIP_SWITCH   (GIO_EXP_39_BASE+7)

#define GIO_EXP_USB_DRVVBUS_z     (GIO_EXP_3A_BASE+0)
#define GIO_EXP_VDDIMX_EN         (GIO_EXP_3A_BASE+1)
#define GIO_EXP_VLYNQ_ON_z        (GIO_EXP_3A_BASE+2)
#define GIO_EXP_CF_RESET_z        (GIO_EXP_3A_BASE+3)
/* bit 4 is unused*/
#define GIO_EXP_WLAN_RESET        (GIO_EXP_3A_BASE+5)
#define GIO_EXP_ATA_SEL           (GIO_EXP_3A_BASE+6)
#define GIO_EXP_CFn_SEL           (GIO_EXP_3A_BASE+7)

#else 

#define GIO_EXP_N_PWR             (GIO_EXP_38_BASE+0)
#define GIO_EXP_LOCK_SWITCH       (GIO_EXP_38_BASE+1)
//#define GIO_EXP_WLAN_RESET        (GIO_EXP_38_BASE+2)
#define GIO_EXP_N_WLAN_RESET_CLR  (GIO_EXP_38_BASE+2)
#define GIO_EXP_N_5160_IRQ        (GIO_EXP_38_BASE+3)
#define GIO_EXP_N_VID_IN1_INT     (GIO_EXP_38_BASE+4)
#define GIO_EXP_N_VID_IN2_INT     (GIO_EXP_38_BASE+5)
#define GIO_EXP_N_VID_OUT1_INT    (GIO_EXP_38_BASE+6)
#define GIO_EXP_N_VID_OUT2_INT    (GIO_EXP_38_BASE+7)

/* GIO expanders 39 and 3A: keypad */

#define GIO_EXP_N_SD_CD           (GIO_EXP_3B_BASE+0)
#define GIO_EXP_N_CF_CD           (GIO_EXP_3B_BASE+1)
#define GIO_EXP_N_MS_INS          (GIO_EXP_3B_BASE+2)
#define GIO_EXP_SD_WP             (GIO_EXP_3B_BASE+3)
#define GIO_EXP_VLYNQ_ON_z        (GIO_EXP_3B_BASE+4)
#define GIO_EXP_POWER_HOLD_ON     (GIO_EXP_3B_BASE+5)
#define GIO_EXP_5160_RESET        (GIO_EXP_3B_BASE+6)
#define GIO_EXP_5160_PWRDN        (GIO_EXP_3B_BASE+7)

#define GIO_EXP_N_VID_OUT1_DET    (GIO_EXP_3C_BASE+0)
#define GIO_EXP_N_VID_OUT2_DET    (GIO_EXP_3C_BASE+1)
#define GIO_EXP_N_VID_IN1_DET     (GIO_EXP_3C_BASE+2)
#define GIO_EXP_N_VID_IN2_DET     (GIO_EXP_3C_BASE+3)
#define GIO_EXP_N_VID_OUT1_EN     (GIO_EXP_3C_BASE+4)
#define GIO_EXP_N_VID_OUT2_EN     (GIO_EXP_3C_BASE+5)
#define GIO_EXP_N_VID_IN1_EN      (GIO_EXP_3C_BASE+6)
#define GIO_EXP_N_VID_IN2_EN      (GIO_EXP_3C_BASE+7)

#define GIO_EXP_LINE_MIC          (GIO_EXP_3D_BASE+0)
#define GIO_EXP_N_USB_VBUS_EN     (GIO_EXP_3D_BASE+1)
#define GIO_EXP_N_OB_ASP_TX_EN    (GIO_EXP_3D_BASE+2)
#define GIO_EXP_N_OB_ASP_RX_EN    (GIO_EXP_3D_BASE+3)
#define GIO_EXP_N_EXT_ASP_TX_EN   (GIO_EXP_3D_BASE+4)
#define GIO_EXP_N_EXT_ASP_RX_EN   (GIO_EXP_3D_BASE+5)
// DM6420 Rev c and earlier
//#define GIO_EXP_N_430_BSL_TEST    (GIO_EXP_3D_BASE+6)
//#define GIO_EXP_N_430_BSL_RESET   (GIO_EXP_3D_BASE+7)
#define GIO_EXP_N_WLAN_RESET_SET  (GIO_EXP_3D_BASE+6)
#define GIO_EXP_N_NAND_WP         (GIO_EXP_3D_BASE+7)


#define GIO_EXP_PLL1705_FS1       (GIO_EXP_3E_BASE+0)
#define GIO_EXP_PLL1705_FS2       (GIO_EXP_3E_BASE+1)
#define GIO_EXP_PLL1705_CSEL      (GIO_EXP_3E_BASE+2)
#define GIO_EXP_PLL1705_SR        (GIO_EXP_3E_BASE+3)
/* Expander 3E: bits 7-4: unused */

#endif

#define GIO_AIC23_FREQ  GIO_EXP_PLL1705_FS1

#endif