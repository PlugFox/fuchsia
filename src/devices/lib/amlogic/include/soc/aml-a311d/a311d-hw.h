// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A311D_A311D_HW_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A311D_A311D_HW_H_

// HIU - includes clock control registers
#define A311D_HIU_BASE 0xff63c000
#define A311D_HIU_LENGTH 0x2000

// gpio
#define A311D_GPIO_BASE 0xff634400
#define A311D_GPIO_LENGTH 0x400
#define A311D_GPIO_AO_BASE 0xff800000
#define A311D_GPIO_AO_LENGTH 0x1000
#define A311D_GPIO_INTERRUPT_BASE 0xffd00000
#define A311D_GPIO_INTERRUPT_LENGTH 0x10000

// i2c
#define A311D_I2C_AOBUS_BASE 0xff805000
#define A311D_I2C_AOBUS_LENGTH 0x1000

// Video decoder/encoder bus.
#define A311D_DOS_BASE 0xff620000
#define A311D_DOS_LENGTH 0x10000

#define A311D_EE_I2C_M0_BASE 0xffd1f000
#define A311D_EE_I2C_M1_BASE 0xffd1e000
#define A311D_EE_I2C_M2_BASE 0xffd1d000
#define A311D_EE_I2C_M3_BASE 0xffd1c000
#define A311D_EE_I2C_M3_LENGTH 0x1000
// spicc
#define A311D_SPICC0_BASE 0xffd13000
#define A311D_SPICC1_BASE 0xffd15000

// Peripherals - datasheet is nondescript about this section, but it contains
//  top level ethernet control and temp sensor registers
#define A311D_PERIPHERALS_BASE 0xff634000
#define A311D_PERIPHERALS_LENGTH 0x1000

// Ethernet
#define A311D_ETH_PHY_BASE 0xff64c000
#define A311D_ETH_PHY_LENGTH 0x2000
#define A311D_ETH_MAC_BASE 0xff3f0000
#define A311D_ETH_MAC_LENGTH 0x10000

// eMMC
#define A311D_EMMC_A_BASE 0xffe03000
#define A311D_EMMC_A_LENGTH 0x2000
#define A311D_EMMC_B_BASE 0xffe05000
#define A311D_EMMC_B_LENGTH 0x2000
#define A311D_EMMC_C_BASE 0xffe07000
#define A311D_EMMC_C_LENGTH 0x2000

// NNA
#define A311D_NNA_BASE 0xFF100000
#define A311D_NNA_LENGTH 0x30000

// Power domain
#define A311D_POWER_DOMAIN_BASE 0xff800000
#define A311D_POWER_DOMAIN_LENGTH 0x1000

// Memory Power Domain
#define A311D_MEMORY_PD_BASE 0xff63c000
#define A311D_MEMORY_PD_LENGTH 0x1000

// IRQs
#define A311D_VIU1_VSYNC_IRQ 35
#define A311D_ETH_GMAC_IRQ 40
#define A311D_USB_IDDIG_IRQ 48
#define A311D_I2C_M0_IRQ 53
#define A311D_DEMUX_IRQ 55
#define A311D_UART_EE_A_IRQ 58
#define A311D_USB0_IRQ 62
#define A311D_USB1_IRQ 63
#define A311D_PARSER_IRQ 64
#define A311D_TS_PLL_IRQ 67
#define A311D_I2C_M3_IRQ 71
#define A311D_DOS_MBOX_0_IRQ 75
#define A311D_DOS_MBOX_1_IRQ 76
#define A311D_DOS_MBOX_2_IRQ 77
#define A311D_GPIO_IRQ_0 96
#define A311D_GPIO_IRQ_1 97
#define A311D_GPIO_IRQ_2 98
#define A311D_GPIO_IRQ_3 99
#define A311D_GPIO_IRQ_4 100
#define A311D_GPIO_IRQ_5 101
#define A311D_GPIO_IRQ_6 102
#define A311D_GPIO_IRQ_7 103
#define A311D_UART1_IRQ 107
#define A311D_SPICC0_IRQ 113
#define A311D_VID1_WR_IRQ 118
#define A311D_RDMA_DONE 121
#define A311D_SPICC1_IRQ 122
#define A311D_UART2_IRQ 125
#define A311D_NNA_IRQ 179
#define A311D_MALI_IRQ_GP 192
#define A311D_MALI_IRQ_GPMMU 193
#define A311D_MALI_IRQ_PP 194
#define A311D_SD_EMMC_A_IRQ 221
#define A311D_SD_EMMC_B_IRQ 222
#define A311D_SD_EMMC_C_IRQ 223
#define A311D_I2C_AO_IRQ 227
#define A311D_I2C_M1_IRQ 246
#define A311D_I2C_M2_IRQ 247

#define A311D_EE_PDM_BASE (0xff640000)
#define A311D_EE_PDM_LENGTH (0x2000)

#define A311D_EE_AUDIO_BASE (0xff642000)
#define A311D_EE_AUDIO_LENGTH (0x2000)

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A311D_A311D_HW_H_
