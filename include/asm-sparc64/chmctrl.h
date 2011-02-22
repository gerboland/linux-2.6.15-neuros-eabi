/* $Id: chmctrl.h,v 1.1.1.1 2006/01/06 00:52:02 jsantiago Exp $ */
#ifndef _SPARC64_CHMCTRL_H
#define _SPARC64_CHMCTRL_H

/* Cheetah memory controller programmable registers. */
#define CHMCTRL_TCTRL1		0x00 /* Memory Timing Control I		*/
#define CHMCTRL_TCTRL2		0x08 /* Memory Timing Control II	*/
#define CHMCTRL_TCTRL3		0x38 /* Memory Timing Control III	*/
#define CHMCTRL_TCTRL4		0x40 /* Memory Timing Control IV	*/
#define CHMCTRL_DECODE1		0x10 /* Memory Address Decode I		*/
#define CHMCTRL_DECODE2		0x18 /* Memory Address Decode II	*/
#define CHMCTRL_DECODE3		0x20 /* Memory Address Decode III	*/
#define CHMCTRL_DECODE4		0x28 /* Memory Address Decode IV	*/
#define CHMCTRL_MACTRL		0x30 /* Memory Address Control		*/

/* Memory Timing Control I */
#define TCTRL1_SDRAMCTL_DLY	0xf000000000000000UL
#define TCTRL1_SDRAMCTL_DLY_SHIFT     60
#define TCTRL1_SDRAMCLK_DLY	0x0e00000000000000UL
#define TCTRL1_SDRAMCLK_DLY_SHIFT     57
#define TCTRL1_R		0x0100000000000000UL
#define TCTRL1_R_SHIFT 		      56
#define TCTRL1_AUTORFR_CYCLE	0x00fe000000000000UL
#define TCTRL1_AUTORFR_CYCLE_SHIFT    49
#define TCTRL1_RD_WAIT		0x0001f00000000000UL
#define TCTRL1_RD_WAIT_SHIFT	      44
#define TCTRL1_PC_CYCLE		0x00000fc000000000UL
#define TCTRL1_PC_CYCLE_SHIFT	      38
#define TCTRL1_WR_MORE_RAS_PW	0x0000003f00000000UL
#define TCTRL1_WR_MORE_RAS_PW_SHIFT   32
#define TCTRL1_RD_MORE_RAW_PW	0x00000000fc000000UL
#define TCTRL1_RD_MORE_RAS_PW_SHIFT   26
#define TCTRL1_ACT_WR_DLY	0x0000000003f00000UL
#define TCTRL1_ACT_WR_DLY_SHIFT	      20
#define TCTRL1_ACT_RD_DLY	0x00000000000fc000UL
#define TCTRL1_ACT_RD_DLY_SHIFT	      14
#define TCTRL1_BANK_PRESENT	0x0000000000003000UL
#define TCTRL1_BANK_PRESENT_SHIFT     12
#define TCTRL1_RFR_INT		0x0000000000000ff8UL
#define TCTRL1_RFR_INT_SHIFT	      3
#define TCTRL1_SET_MODE_REG	0x0000000000000004UL
#define TCTRL1_SET_MODE_REG_SHIFT     2
#define TCTRL1_RFR_ENABLE	0x0000000000000002UL
#define TCTRL1_RFR_ENABLE_SHIFT	      1
#define TCTRL1_PRECHG_ALL	0x0000000000000001UL
#define TCTRL1_PRECHG_ALL_SHIFT	      0

/* Memory Timing Control II */
#define TCTRL2_WR_MSEL_DLY	0xfc00000000000000UL
#define TCTRL2_WR_MSEL_DLY_SHIFT      58
#define TCTRL2_RD_MSEL_DLY	0x03f0000000000000UL
#define TCTRL2_RD_MSEL_DLY_SHIFT      52
#define TCTRL2_WRDATA_THLD	0x000c000000000000UL
#define TCTRL2_WRDATA_THLD_SHIFT      50
#define TCTRL2_RDWR_RD_TI_DLY	0x0003f00000000000UL
#define TCTRL2_RDWR_RD_TI_DLY_SHIFT   44
#define TCTRL2_AUTOPRECHG_ENBL	0x0000080000000000UL
#define TCTRL2_AUTOPRECHG_ENBL_SHIFT  43
#define TCTRL2_RDWR_PI_MORE_DLY	0x000007c000000000UL
#define TCTRL2_RDWR_PI_MORE_DLY_SHIFT 38
#define TCTRL2_RDWR_1_DLY	0x0000003f00000000UL
#define TCTRL2_RDWR_1_DLY_SHIFT       32
#define TCTRL2_WRWR_PI_MORE_DLY	0x00000000f8000000UL
#define TCTRL2_WRWR_PI_MORE_DLY_SHIFT 27
#define TCTRL2_WRWR_1_DLY	0x0000000007e00000UL
#define TCTRL2_WRWR_1_DLY_SHIFT       21
#define TCTRL2_RDWR_RD_PI_MORE_DLY 0x00000000001f0000UL
#define TCTRL2_RDWR_RD_PI_MORE_DLY_SHIFT 16
#define TCTRL2_R		0x0000000000008000UL
#define TCTRL2_R_SHIFT		      15
#define TCTRL2_SDRAM_MODE_REG_DATA 0x0000000000007fffUL
#define TCTRL2_SDRAM_MODE_REG_DATA_SHIFT 0

/* Memory Timing Control III */
#define TCTRL3_SDRAM_CTL_DLY	0xf000000000000000UL
#define TCTRL3_SDRAM_CTL_DLY_SHIFT    60
#define TCTRL3_SDRAM_CLK_DLY	0x0e00000000000000UL
#define TCTRL3_SDRAM_CLK_DLY_SHIFT    57
#define TCTRL3_R		0x0100000000000000UL
#define TCTRL3_R_SHIFT		      56
#define TCTRL3_AUTO_RFR_CYCLE	0x00fe000000000000UL
#define TCTRL3_AUTO_RFR_CYCLE_SHIFT   49
#define TCTRL3_RD_WAIT		0x0001f00000000000UL
#define TCTRL3_RD_WAIT_SHIFT	      44
#define TCTRL3_PC_CYCLE		0x00000fc000000000UL
#define TCTRL3_PC_CYCLE_SHIFT	      38
#define TCTRL3_WR_MORE_RAW_PW	0x0000003f00000000UL
#define TCTRL3_WR_MORE_RAW_PW_SHIFT   32
#define TCTRL3_RD_MORE_RAW_PW	0x00000000fc000000UL
#define TCTRL3_RD_MORE_RAW_PW_SHIFT   26
#define TCTRL3_ACT_WR_DLY	0x0000000003f00000UL
#define TCTRL3_ACT_WR_DLY_SHIFT       20
#define TCTRL3_ACT_RD_DLY	0x00000000000fc000UL
#define TCTRL3_ACT_RD_DLY_SHIFT       14
#define TCTRL3_BANK_PRESENT	0x0000000000003000UL
#define TCTRL3_BANK_PRESENT_SHIFT     12
#define TCTRL3_RFR_INT		0x0000000000000ff8UL
#define TCTRL3_RFR_INT_SHIFT	      3
#define TCTRL3_SET_MODE_REG	0x0000000000000004UL
#define TCTRL3_SET_MODE_REG_SHIFT     2
#define TCTRL3_RFR_ENABLE	0x0000000000000002UL
#define TCTRL3_RFR_ENABLE_SHIFT       1
#define TCTRL3_PRECHG_ALL	0x0000000000000001UL
#define TCTRL3_PRECHG_ALL_SHIFT	      0

/* Memory Timing Control IV */
#define TCTRL4_WR_MSEL_DLY	0xfc00000000000000UL
#define TCTRL4_WR_MSEL_DLY_SHIFT      58
#define TCTRL4_RD_MSEL_DLY	0x03f0000000000000UL
#define TCTRL4_RD_MSEL_DLY_SHIFT      52
#define TCTRL4_WRDATA_THLD	0x000c000000000000UL
#define TCTRL4_WRDATA_THLD_SHIFT      50
#define TCTRL4_RDWR_RD_RI_DLY	0x0003f00000000000UL
#define TCTRL4_RDWR_RD_RI_DLY_SHIFT   44
#define TCTRL4_AUTO_PRECHG_ENBL	0x0000080000000000UL
#define TCTRL4_AUTO_PRECHG_ENBL_SHIFT 43
#define TCTRL4_RD_WR_PI_MORE_DLY 0x000007c000000000UL
#define TCTRL4_RD_WR_PI_MORE_DLY_SHIFT 38
#define TCTRL4_RD_WR_TI_DLY	0x0000003f00000000UL
#define TCTRL4_RD_WR_TI_DLY_SHIFT     32
#define TCTRL4_WR_WR_PI_MORE_DLY 0x00000000f8000000UL
#define TCTRL4_WR_WR_PI_MORE_DLY_SHIFT 27
#define TCTRL4_WR_WR_TI_DLY	0x0000000007e00000UL
#define TCTRL4_WR_WR_TI_DLY_SHIFT     21
#define TCTRL4_RDWR_RD_PI_MORE_DLY 0x00000000001f000UL0
#define TCTRL4_RDWR_RD_PI_MORE_DLY_SHIFT 16
#define TCTRL4_R		0x0000000000008000UL
#define TCTRL4_R_SHIFT		      15
#define TCTRL4_SDRAM_MODE_REG_DATA 0x0000000000007fffUL
#define TCTRL4_SDRAM_MODE_REG_DATA_SHIFT 0

/* All 4 memory address decoding registers have the
 * same layout.
 */
#define MEM_DECODE_VALID	0x8000000000000000UL /* Valid */
#define MEM_DECODE_VALID_SHIFT	      63
#define MEM_DECODE_UK		0x001ffe0000000000UL /* Upper mask */
#define MEM_DECODE_UK_SHIFT	      41
#define MEM_DECODE_UM		0x0000001ffff00000UL /* Upper match */
#define MEM_DECODE_UM_SHIFT	      20
#define MEM_DECODE_LK		0x000000000003c000UL /* Lower mask */
#define MEM_DECODE_LK_SHIFT	      14
#define MEM_DECODE_LM		0x0000000000000f00UL /* Lower match */
#define MEM_DECODE_LM_SHIFT           8

#define PA_UPPER_BITS		0x000007fffc000000UL
#define PA_UPPER_BITS_SHIFT	26
#define PA_LOWER_BITS		0x00000000000003c0UL
#define PA_LOWER_BITS_SHIFT	6

#define MACTRL_R0		         0x8000000000000000UL
#define MACTRL_R0_SHIFT		         63
#define MACTRL_ADDR_LE_PW                0x7000000000000000UL
#define MACTRL_ADDR_LE_PW_SHIFT		 60
#define MACTRL_CMD_PW                    0x0f00000000000000UL
#define MACTRL_CMD_PW_SHIFT		 56
#define MACTRL_HALF_MODE_WR_MSEL_DLY     0x00fc000000000000UL
#define MACTRL_HALF_MODE_WR_MSEL_DLY_SHIFT 50
#define MACTRL_HALF_MODE_RD_MSEL_DLY     0x0003f00000000000UL
#define MACTRL_HALF_MODE_RD_MSEL_DLY_SHIFT 44
#define MACTRL_HALF_MODE_SDRAM_CTL_DLY   0x00000f0000000000UL
#define MACTRL_HALF_MODE_SDRAM_CTL_DLY_SHIFT 40
#define MACTRL_HALF_MODE_SDRAM_CLK_DLY   0x000000e000000000UL
#define MACTRL_HALF_MODE_SDRAM_CLK_DLY_SHIFT 37
#define MACTRL_R1                        0x0000001000000000UL
#define MACTRL_R1_SHIFT                      36
#define MACTRL_BANKSEL_N_ROWADDR_SIZE_B3 0x0000000f00000000UL
#define MACTRL_BANKSEL_N_ROWADDR_SIZE_B3_SHIFT 32
#define MACTRL_ENC_INTLV_B3              0x00000000f8000000UL
#define MACTRL_ENC_INTLV_B3_SHIFT              27
#define MACTRL_BANKSEL_N_ROWADDR_SIZE_B2 0x0000000007800000UL
#define MACTRL_BANKSEL_N_ROWADDR_SIZE_B2_SHIFT 23
#define MACTRL_ENC_INTLV_B2              0x00000000007c0000UL
#define MACTRL_ENC_INTLV_B2_SHIFT              18
#define MACTRL_BANKSEL_N_ROWADDR_SIZE_B1 0x000000000003c000UL
#define MACTRL_BANKSEL_N_ROWADDR_SIZE_B1_SHIFT 14
#define MACTRL_ENC_INTLV_B1              0x0000000000003e00UL
#define MACTRL_ENC_INTLV_B1_SHIFT               9
#define MACTRL_BANKSEL_N_ROWADDR_SIZE_B0 0x00000000000001e0UL
#define MACTRL_BANKSEL_N_ROWADDR_SIZE_B0_SHIFT  5
#define MACTRL_ENC_INTLV_B0              0x000000000000001fUL
#define MACTRL_ENC_INTLV_B0_SHIFT               0

#endif /* _SPARC64_CHMCTRL_H */
