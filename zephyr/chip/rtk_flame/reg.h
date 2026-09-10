/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * For Chipset: RTK EC
 *
 * Function: RTK Flash Utility
 */

#ifndef PLATFORM_EC_ZEPHYR_CHIP_RTK_FLAME_REG_H_
#define PLATFORM_EC_ZEPHYR_CHIP_RTK_FLAME_REG_H_

#include <stdint.h>
#include <stdio.h>

/**
 * @brief I/O Pad Controller (IOPAD)
 */

struct iopad { /*!< (@ 0x40091000) IOPAD Structure */

	volatile uint32_t FLASHWP; /*!< (@ 0x00000000) INTERNAL FLASH_WP
				      PAD CONTROL REGISTER */

	volatile uint32_t FLASHHOLD; /*!< (@ 0x00000004) INTERNAL
					FLASH_HOLD PAD CONTROL REGISTER
					      */
	volatile uint32_t FLASHSI; /*!< (@ 0x00000008) INTERNAL FLASH_SI
				      PAD CONTROL REGISTER */

	volatile uint32_t FLASHSO; /*!< (@ 0x0000000C) INTERNAL FLASH_SO
				      PAD CONTROL REGISTER */

	volatile uint32_t FLASHCS; /*!< (@ 0x00000010) INTERNAL FLASH_CS
					      PAD CONTROL REGISTER */

	volatile uint32_t FLASHCLK; /*!< (@ 0x00000014) INTERNAL
				      FLASH_CLK PAD CONTROL REGISTER */

	volatile uint32_t PECI; /*!< (@ 0x00000018) PECI PAD CONTROL
					   REGISTER */

}; /*!< Size = 28 (0x1c) */

struct slwtmr { /*!< (@ 0x4000C200) SLWTMR0 Structure */
	volatile uint32_t LDCNT; /*!< (@ 0x00000000) LOAD COUNTER REGISTER */
	volatile uint32_t CNT; /*!< (@ 0x00000004) CURRENT COUNTER REGISTER */
	volatile uint32_t CTRL; /*!< (@ 0x00000008) CONTROL REGISTER */
	volatile uint32_t INTSTS; /*!< (@ 0x0000000C) INTERRUPT STATUS
				     REGISTER */
}; /*!< Size = 16 (0x10)  */

/*
 * @brief SPIC Controller (SPIC)
 */

struct spic { /*!< (@ 0x40010200) SPIC Structure */

	volatile uint32_t CTRL0; /*!< (@ 0x00000000) CONTROL REGISTER #0
				  */
	volatile uint32_t RXNDF; /*!< (@ 0x00000004) NUMBER OF RX DATA
					    FRAME REGISTER */
	volatile uint32_t SSIENR; /*!< (@ 0x00000008) ENABLE REGISTER */

	volatile uint32_t RESERVED;

	volatile uint32_t SER; /*!< (@ 0x00000010) SELECT TARGET FLASH
					  REGISTER */

	volatile uint32_t BAUDR; /*!< (@ 0x00000014) BAUD RATE SELECT
					    REGISTER */

	volatile uint32_t TXFTLR; /*!< (@ 0x00000018) TX FIFO THRESHOLD REGISTER
				   */
	volatile uint32_t RXFTLR; /*!< (@ 0x0000001C) RECEIVE FIFO THRESHOLD
				     LEVEL                               */
	volatile uint32_t TXFLR; /*!< (@ 0x00000020) TRANSMIT FIFO LEVEL
				    REGISTER                               */
	volatile uint32_t RXFLR; /*!< (@ 0x00000024) RECEIVE FIFO LEVEL REGISTER
				  */

	volatile uint32_t SR; /*!< (@ 0x00000028) STATUS REGISTER */

	volatile uint32_t IMR; /*!< (@ 0x0000002C) Interrupt Mask
					  Register */
	volatile uint32_t ISR; /*!< (@ 0x00000030) INTERRUPT STATUS
					  REGISTER */
	volatile uint32_t RISR; /*!< (@ 0x00000034) RAW INTERRUPT STATUS
					   REGISTER */
	volatile uint32_t TXOICR; /*!< (@ 0x00000038) TRANSMIT FIFO OVERFLOW
				     INTERRUPT CLEAR REGISTER            */
	volatile uint32_t RXOICR; /*!< (@ 0x0000003C) RECEIVE FIFO OVERFLOW
				     INTERRUPT CLEAR REGISTER             */
	volatile uint32_t RXUICR; /*!< (@ 0x00000040) RECEIVE FIFO UNDERFLOW
				     INTERRUPT CLEAR REGISTER            */
	volatile uint32_t MSTICR; /*!< (@ 0x00000044) MASTER ERROR INTERRUPT
				     CLEAR REGISTER                      */
	volatile uint32_t ICR; /*!< (@ 0x00000048) INTERRUPT CLEAR REGISTER */
	volatile const uint32_t RESERVED1[5];
	union {
		volatile uint8_t BYTE;
		volatile uint16_t HALF;
		volatile uint32_t WORD;
	} DR;
	volatile const uint32_t RESERVED2[44];
	volatile uint32_t FBAUD;
	volatile uint32_t USERLENGTH; /*!< (@ 0x00000118) DECIDES BYTE
						 NUMBERS OF COMMAND, ADDRESS AND
							       DATA PHASE TO
						 TRANSMIT */
	volatile const uint32_t RESERVED3[3];

	volatile uint32_t FLUSH; /*!< (@ 0x00000128) FLUSH FIFO REGISTER
				  */

	volatile const uint32_t RESERVED4;

	volatile uint32_t TXNDF; /*!< (@ 0x00000130) A NUMBER OF DATA
					    FRAMES OF TX DATA IN USER MODE */

}; /*!< Size = 308 (0x134) */

/**
 * @brief GPIO Controller (GPIO)
 */

struct gpio { /*!< (@ 0x40090000) GPIO Structure */

	volatile uint32_t GCR[132]; /*!< (@ 0x00000000) CONTROL REGISTER
				     */
}; /*!< Size = 528 (0x210) */

/**
 * @brief UART Controller (UART)
 */

struct uart { /*!< (@ 0x40010100) UART Structure */
	union {
		volatile const uint32_t RBR; /*!< (@ 0x00000000) RECEIVE
							BUFFER REGISTER */
		volatile uint32_t THR; /*!< (@ 0x00000000) TRANSMIT
						  HOLDING REGISTER */
		volatile uint32_t DLL; /*!< (@ 0x00000000) DIVISOR LATCH
						  LOW REGISTER */
	};

	union {
		volatile uint32_t DLH; /*!< (@ 0x00000004) DIVISOR LATCH
						  HIGH REGISTER */
		volatile uint32_t IER; /*!< (@ 0x00000004) INTRRRUPT
						  ENABLE REGISTER */
	};

	union {
		volatile uint32_t IIR; /*!< (@ 0x00000008) INTERRUPT
						  IDENTIFICATION */

		volatile uint32_t FCR; /*!< (@ 0x00000008) FIFO CONTROL
						  REGISTER */
	};

	volatile uint32_t LCR; /*!< (@ 0x0000000C) LINE CONTROL REGISTER
				*/
	volatile const uint32_t RESERVED;

	volatile const uint32_t LSR; /*!< (@ 0x00000014) LINE STATUS
						REGISTER */

	volatile const uint32_t RESERVED1[25];

	volatile const uint32_t USR; /*!< (@ 0x0000007C) UART Status
						Register */

	volatile const uint32_t TFL; /*!< (@ 0x00000080) UART TRANSMIT FIFO
					LEVEL */
	volatile const uint32_t RFL; /*!< (@ 0x00000084) UART RECEIVE FIFO LEVEL
				      */

	volatile uint32_t SRR; /*!< (@ 0x00000088) UART SOFTWARE RESET
					  REGISTER */

}; /*!< Size = 140 (0x8c) */

/**
 * @brief Keyboard Matrix Controller (KBM)
 */

struct kbm { /*!< (@ 0x40010000) KBM Structure */

	volatile uint32_t SCANOUT; /*!< (@ 0x00000000) SCAN OUT CONTROL
					      REGISTER */

	volatile const uint32_t SCANIN; /*!< (@ 0x00000004) SACN INPUT
						   STATUS REGISTER */

	volatile uint32_t INTEN; /*!< (@ 0x00000008) INTERRUPT ENABLE
					    REGISTER */

	volatile uint32_t CTRL; /*!< (@ 0x0000000C) CONTROL REGISTER */

}; /*!< Size = 16 (0x10) */

#define UART_BASE 0x40010100UL
#define SPIC_BASE 0x40010200UL
#define GPIO_BASE 0x40090000UL
#define IOPAD_BASE 0x40091000UL
#define SYSTEM_BASE 0x40020000UL
#define KBM_BASE 0x40010000UL
#define SLWTMR0_BASE 0x4000C200UL

#define SPIC ((struct spic *)SPIC_BASE)
#define GPIO ((struct gpio *)GPIO_BASE)
#define IOPAD ((struct iopad *)IOPAD_BASE)
#define SYSTEM ((struct system *)SYSTEM_BASE)
#define KBM ((struct kbm *)KBM_BASE)
#define UART ((struct uart *)UART_BASE)
#define SLWTMR0 ((struct slwtmr *)SLWTMR0_BASE)

#define UART_USR_TFNF_Msk (0x2UL)
#define UART_USR_TFE_Msk (0x4UL)
#define UART_LSR_THRE_Msk (0x20UL)
#define SYSTEM_PERICLKPWR1_SLWTMR0CLKPWR_Msk (0x200000UL)

/* KBM pins */
#define KBM_KSO_0_PIN 41
#define KBM_KSO_1_PIN 42
#define KBM_KSO_2_PIN 43
#define KBM_KSO_3_PIN 44
#define KBM_KSO_4_PIN 45
#define KBM_KSO_5_PIN 46
#define KBM_KSO_6_PIN 47
#define KBM_KSO_7_PIN 48
#define KBM_KSO_8_PIN 49
#define KBM_KSO_9_PIN 50

#define KBM_KSI_0_PIN 64
#define KBM_KSI_1_PIN 65
#define KBM_KSI_2_PIN 66
#define KBM_KSI_3_PIN 67
#define KBM_KSI_4_PIN 68
#define KBM_KSI_5_PIN 69
#define KBM_KSI_6_PIN 70
#define KBM_KSI_7_PIN 71

/**
 * @brief System Controller (SYSTEM)
 */

struct system { /*!< (@ 0x40020000) SYSTEM Structure */

	union {
		volatile uint32_t I3CCLK; /*!< (@ 0x00000000) I3C CLOCK
						     REGISTER */

		volatile uint32_t TMRRST; /*!< (@ 0x00000000) TIMER
						     RESET REGISTER */
	};

	volatile uint32_t I2CCLK; /*!< (@ 0x00000004) I2C CLOCK REGISTER
				   */
	volatile uint32_t TMRCLK; /*!< (@ 0x00000008) TIMER32 CLOCK
					     REGISTER */
	volatile uint32_t PERICLKPWR0; /*!< (@ 0x0000000C) PERIPHERAL
						  CLOCK POWER REGISTER #0 */

	volatile uint32_t UARTCLK; /*!< (@ 0x00000010) UART CLOCK
					      REGISTER */

	volatile uint32_t SYSCLK; /*!< (@ 0x00000014) SYSTEM CLOCK
					     REGISTER */

	volatile uint32_t ADCCLK; /*!< (@ 0x00000018) ADC CLOCK REGISTER
				   */

	volatile uint32_t PERICLKPWR1; /*!< (@ 0x0000001C) PERIPHERAL
						  CLOCK POWER REGISTER #1 */

	volatile const uint32_t RESERVED[24];

	volatile uint32_t SLPCTRL; /*!< (@ 0x00000080) SYSTEM SLEEP
					      CONTROL REGISTER */

	volatile const uint32_t RESERVED1[7];

	volatile uint32_t VIVOCTRL; /*!< (@ 0x000000A0) VINVOUT CONTROL
					       REGISTER */

	volatile uint32_t LDOCTRL; /*!< (@ 0x000000A4) LDO CONTROL
					      REGISTER */

	volatile uint32_t RC25MCTRL; /*!< (@ 0x000000A8) RC25M CONTROL
						REGISTER */

	volatile uint32_t PLLCTRL; /*!< (@ 0x000000AC) PLL CONTROL
					      REGISTER */

	volatile const uint32_t RESERVED2[12];

	volatile uint32_t RC32KCTRL; /*!< (@ 0x000000E0) RC32K CONTROL
						REGISTER */

	volatile const uint32_t RESERVED3;

	volatile uint32_t PERICLKPWR2; /*!< (@ 0x000000E8) PERIPHERAL
						  CLOCK POWER REGISTER #2 */

}; /*!< Size = 236 (0xec) */

/* =========================================================  CTRL0
 * ========================================================= */
#define SPIC_CTRL0_SCPH_Pos \
	(6UL) /*!< SCPH (Bit 6)                                          */
#define SPIC_CTRL0_SCPH_Msk \
	(0x40UL) /*!< SCPH (Bitfield-Mask: 0x01)                            */
#define SPIC_CTRL0_SCPOL_Pos \
	(7UL) /*!< SCPOL (Bit 7)                                         */
#define SPIC_CTRL0_SCPOL_Msk \
	(0x80UL) /*!< SCPOL (Bitfield-Mask: 0x01)                           */
#define SPIC_CTRL0_TMOD_Pos \
	(8UL) /*!< TMOD (Bit 8)                                          */
#define SPIC_CTRL0_TMOD_Msk (0x300UL) /*!< TMOD (Bitfield-Mask: 0x03) */
#define SPIC_CTRL0_ADDRCH_Pos \
	(16UL) /*!< ADDRCH (Bit 16)                                       */
#define SPIC_CTRL0_ADDRCH_Msk (0x30000UL) /*!< ADDRCH (Bitfield-Mask: 0x03) */
#define SPIC_CTRL0_DATACH_Pos \
	(18UL) /*!< DATACH (Bit 18)                                       */
#define SPIC_CTRL0_DATACH_Msk (0xc0000UL) /*!< DATACH (Bitfield-Mask: 0x03) */
#define SPIC_CTRL0_CMDCH_Pos \
	(20UL) /*!< CMDCH (Bit 20)                                        */
#define SPIC_CTRL0_CMDCH_Msk (0x300000UL) /*!< CMDCH (Bitfield-Mask: 0x03) */
#define SPIC_CTRL0_USERMD_Pos \
	(31UL) /*!< USERMD (Bit 31)                                       */
#define SPIC_CTRL0_USERMD_Msk \
	(0x80000000UL) /*!< USERMD (Bitfield-Mask: 0x01) */
/* =========================================================  RXNDF
 * ========================================================= */
#define SPIC_RXNDF_NUM_Pos \
	(0UL) /*!< NUM (Bit 0)                                           */
#define SPIC_RXNDF_NUM_Msk (0xffffffUL) /*!< NUM (Bitfield-Mask: 0xffffff) */
/* ========================================================  SSIENR
 * ========================================================= */
#define SPIC_SSIENR_SPICEN_Pos \
	(0UL) /*!< SPICEN (Bit 0)                                        */
#define SPIC_SSIENR_SPICEN_Msk \
	(0x1UL) /*!< SPICEN (Bitfield-Mask: 0x01)                          */
#define SPIC_SSIENR_ATCKCMD_Pos \
	(1UL) /*!< ATCKCMD (Bit 1)                                       */
#define SPIC_SSIENR_ATCKCMD_Msk \
	(0x2UL) /*!< ATCKCMD (Bitfield-Mask: 0x01)                         */
/* ==========================================================  SER
 * ========================================================== */
#define SPIC_SER_SEL_Pos \
	(0UL) /*!< SEL (Bit 0)                                           */
#define SPIC_SER_SEL_Msk \
	(0x1UL) /*!< SEL (Bitfield-Mask: 0x01)                             */
/* =========================================================  BAUDR
 * ========================================================= */
#define SPIC_BAUDR_SCKDV_Pos \
	(0UL) /*!< SCKDV (Bit 0)                                         */
#define SPIC_BAUDR_SCKDV_Msk (0xfffUL) /*!< SCKDV (Bitfield-Mask: 0xfff) */
/* ========================================================  TXFTLR
 * ========================================================= */
/* ========================================================  RXFTLR
 * ========================================================= */
/* =========================================================  TXFLR
 * ========================================================= */
/* =========================================================  RXFLR
 * ========================================================= */
/* ==========================================================  SR
 * =========================================================== */
#define SPIC_SR_BUSY_Pos \
	(0UL) /*!< BUSY (Bit 0)                                          */
#define SPIC_SR_BUSY_Msk \
	(0x1UL) /*!< BUSY (Bitfield-Mask: 0x01)                            */
#define SPIC_SR_TFNF_Pos \
	(1UL) /*!< TFNF (Bit 1)                                          */
#define SPIC_SR_TFNF_Msk \
	(0x2UL) /*!< TFNF (Bitfield-Mask: 0x01)                            */
#define SPIC_SR_TFE_Pos \
	(2UL) /*!< TFE (Bit 2)                                           */
#define SPIC_SR_TFE_Msk \
	(0x4UL) /*!< TFE (Bitfield-Mask: 0x01)                             */
#define SPIC_SR_RFNE_Pos \
	(3UL) /*!< RFNE (Bit 3)                                          */
#define SPIC_SR_RFNE_Msk \
	(0x8UL) /*!< RFNE (Bitfield-Mask: 0x01)                            */
#define SPIC_SR_RFF_Pos \
	(4UL) /*!< RFF (Bit 4)                                           */
#define SPIC_SR_RFF_Msk \
	(0x10UL) /*!< RFF (Bitfield-Mask: 0x01)                             */
#define SPIC_SR_TXE_Pos \
	(5UL) /*!< TXE (Bit 5)                                           */
#define SPIC_SR_TXE_Msk \
	(0x20UL) /*!< TXE (Bitfield-Mask: 0x01)                             */
/* ==========================================================  IMR
 * ========================================================== */
#define SPIC_IMR_TXEIM_Pos \
	(0UL) /*!< TXEIM (Bit 0)                                         */
#define SPIC_IMR_TXEIM_Msk \
	(0x1UL) /*!< TXEIM (Bitfield-Mask: 0x01)                           */
#define SPIC_IMR_TXOIM_Pos \
	(1UL) /*!< TXOIM (Bit 1)                                         */
#define SPIC_IMR_TXOIM_Msk \
	(0x2UL) /*!< TXOIM (Bitfield-Mask: 0x01)                           */
#define SPIC_IMR_RXUIM_Pos \
	(2UL) /*!< RXUIM (Bit 2)                                         */
#define SPIC_IMR_RXUIM_Msk \
	(0x4UL) /*!< RXUIM (Bitfield-Mask: 0x01)                           */
#define SPIC_IMR_RXOIM_Pos \
	(3UL) /*!< RXOIM (Bit 3)                                         */
#define SPIC_IMR_RXOIM_Msk \
	(0x8UL) /*!< RXOIM (Bitfield-Mask: 0x01)                           */
#define SPIC_IMR_RXFIM_Pos \
	(4UL) /*!< RXFIM (Bit 4)                                         */
#define SPIC_IMR_RXFIM_Msk \
	(0x10UL) /*!< RXFIM (Bitfield-Mask: 0x01)                           */
#define SPIC_IMR_FSEIM_Pos \
	(5UL) /*!< FSEIM (Bit 5)                                         */
#define SPIC_IMR_FSEIM_Msk \
	(0x20UL) /*!< FSEIM (Bitfield-Mask: 0x01)                           */
#define SPIC_IMR_USSIM_Pos \
	(9UL) /*!< USSIM (Bit 9)                                         */
#define SPIC_IMR_USSIM_Msk (0x200UL) /*!< USSIM (Bitfield-Mask: 0x01) */
#define SPIC_IMR_TFSIM_Pos \
	(10UL) /*!< TFSIM (Bit 10)                                        */
#define SPIC_IMR_TFSIM_Msk (0x400UL) /*!< TFSIM (Bitfield-Mask: 0x01) */
/* ==========================================================  ISR
 * ========================================================== */
#define SPIC_ISR_TXEIS_Pos \
	(0UL) /*!< TXEIS (Bit 0)                                         */
#define SPIC_ISR_TXEIS_Msk \
	(0x1UL) /*!< TXEIS (Bitfield-Mask: 0x01)                           */
#define SPIC_ISR_TXOIS_Pos \
	(1UL) /*!< TXOIS (Bit 1)                                         */
#define SPIC_ISR_TXOIS_Msk \
	(0x2UL) /*!< TXOIS (Bitfield-Mask: 0x01)                           */
#define SPIC_ISR_RXUIS_Pos \
	(2UL) /*!< RXUIS (Bit 2)                                         */
#define SPIC_ISR_RXUIS_Msk \
	(0x4UL) /*!< RXUIS (Bitfield-Mask: 0x01)                           */
#define SPIC_ISR_RXOIS_Pos \
	(3UL) /*!< RXOIS (Bit 3)                                         */
#define SPIC_ISR_RXOIS_Msk \
	(0x8UL) /*!< RXOIS (Bitfield-Mask: 0x01)                           */
#define SPIC_ISR_RXFIS_Pos \
	(4UL) /*!< RXFIS (Bit 4)                                         */
#define SPIC_ISR_RXFIS_Msk \
	(0x10UL) /*!< RXFIS (Bitfield-Mask: 0x01)                           */
#define SPIC_ISR_FSEIS_Pos \
	(5UL) /*!< FSEIS (Bit 5)                                         */
#define SPIC_ISR_FSEIS_Msk \
	(0x20UL) /*!< FSEIS (Bitfield-Mask: 0x01)                           */
#define SPIC_ISR_USEIS_Pos \
	(9UL) /*!< USEIS (Bit 9)                                         */
#define SPIC_ISR_USEIS_Msk (0x200UL) /*!< USEIS (Bitfield-Mask: 0x01) */
#define SPIC_ISR_TFSIS_Pos \
	(10UL) /*!< TFSIS (Bit 10)                                        */
#define SPIC_ISR_TFSIS_Msk (0x400UL) /*!< TFSIS (Bitfield-Mask: 0x01) */
/* =========================================================  RISR
 * ========================================================== */
#define SPIC_RISR_TXEIR_Pos \
	(0UL) /*!< TXEIR (Bit 0)                                         */
#define SPIC_RISR_TXEIR_Msk \
	(0x1UL) /*!< TXEIR (Bitfield-Mask: 0x01)                           */
#define SPIC_RISR_TXOIR_Pos \
	(1UL) /*!< TXOIR (Bit 1)                                         */
#define SPIC_RISR_TXOIR_Msk \
	(0x2UL) /*!< TXOIR (Bitfield-Mask: 0x01)                           */
#define SPIC_RISR_RXUIR_Pos \
	(2UL) /*!< RXUIR (Bit 2)                                         */
#define SPIC_RISR_RXUIR_Msk \
	(0x4UL) /*!< RXUIR (Bitfield-Mask: 0x01)                           */
#define SPIC_RISR_RXOIR_Pos \
	(3UL) /*!< RXOIR (Bit 3)                                         */
#define SPIC_RISR_RXOIR_Msk \
	(0x8UL) /*!< RXOIR (Bitfield-Mask: 0x01)                           */
#define SPIC_RISR_RXFIR_Pos \
	(4UL) /*!< RXFIR (Bit 4)                                         */
#define SPIC_RISR_RXFIR_Msk \
	(0x10UL) /*!< RXFIR (Bitfield-Mask: 0x01)                           */
#define SPIC_RISR_FSEIR_Pos \
	(5UL) /*!< FSEIR (Bit 5)                                         */
#define SPIC_RISR_FSEIR_Msk \
	(0x20UL) /*!< FSEIR (Bitfield-Mask: 0x01)                           */
#define SPIC_RISR_USEIR_Pos \
	(9UL) /*!< USEIR (Bit 9)                                         */
#define SPIC_RISR_USEIR_Msk (0x200UL) /*!< USEIR (Bitfield-Mask: 0x01) */
#define SPIC_RISR_TFSIR_Pos \
	(10UL) /*!< TFSIR (Bit 10)                                        */
#define SPIC_RISR_TFSIR_Msk (0x400UL) /*!< TFSIR (Bitfield-Mask: 0x01) */
/* ========================================================  TXOICR
 * ========================================================= */
/* ========================================================  RXOICR
 * ========================================================= */
/* ========================================================  RXUICR
 * ========================================================= */
/* ========================================================  MSTICR
 * ========================================================= */
/* ==========================================================  ICR
 * ========================================================== */
/* ==========================================================  DR
 * =========================================================== */
/* ========================================================  DR_BYTE
 * ======================================================== */
/* ========================================================  DR_HALF
 * ======================================================== */
/* ========================================================  DR_WORD
 * ======================================================== */
/* ======================================================  USERLENGTH
 * ======================================================= */
#define SPIC_USERLENGTH_RDDUMMLEN_Pos \
	(0UL) /*!< RDDUMMLEN (Bit 0)                                    */
#define SPIC_USERLENGTH_RDDUMMLEN_Msk \
	(0xfffUL) /*!< RDDUMMLEN (Bitfield-Mask: 0xfff) */
#define SPIC_USERLENGTH_CMDLEN_Pos \
	(12UL) /*!< CMDLEN (Bit 12)                                       */
#define SPIC_USERLENGTH_CMDLEN_Msk \
	(0x3000UL) /*!< CMDLEN (Bitfield-Mask: 0x03) */
#define SPIC_USERLENGTH_ADDRLEN_Pos \
	(16UL) /*!< ADDRLEN (Bit 16)                                      */
#define SPIC_USERLENGTH_ADDRLEN_Msk \
	(0xf0000UL) /*!< ADDRLEN (Bitfield-Mask: 0x0f) */
/* =========================================================  FLUSH
 * ========================================================= */
#define SPIC_FLUSH_ALL_Pos \
	(0UL) /*!< ALL (Bit 0)                                           */
#define SPIC_FLUSH_ALL_Msk \
	(0x1UL) /*!< ALL (Bitfield-Mask: 0x01)                             */
#define SPIC_FLUSH_DRFIFO_Pos \
	(1UL) /*!< DRFIFO (Bit 1)                                        */
#define SPIC_FLUSH_DRFIFO_Msk \
	(0x2UL) /*!< DRFIFO (Bitfield-Mask: 0x01)                          */
#define SPIC_FLUSH_STFIFO_Pos \
	(2UL) /*!< STFIFO (Bit 2)                                        */
#define SPIC_FLUSH_STFIFO_Msk \
	(0x4UL) /*!< STFIFO (Bitfield-Mask: 0x01)                          */
/* =========================================================  TXNDF
 * ========================================================= */
#define SPIC_TXNDF_NUM_Pos \
	(0UL) /*!< NUM (Bit 0)                                           */
#define SPIC_TXNDF_NUM_Msk (0xffffffUL) /*!< NUM (Bitfield-Mask: 0xffffff) */

/* ========================================================  FLASHWP
 * ======================================================== */
#define IOPAD_FLASHWP_INDETEN_Pos \
	(0UL) /*!< INDETEN (Bit 0)                                       */
#define IOPAD_FLASHWP_INDETEN_Msk \
	(0x1UL) /*!< INDETEN (Bitfield-Mask: 0x01)                         */
#define IOPAD_FLASHWP_OUTDRV_Pos \
	(1UL) /*!< OUTDRV (Bit 1)                                        */
#define IOPAD_FLASHWP_OUTDRV_Msk \
	(0x2UL) /*!< OUTDRV (Bitfield-Mask: 0x01)                          */
#define IOPAD_FLASHWP_SLEWRATE_Pos \
	(2UL) /*!< SLEWRATE (Bit 2)                                      */
#define IOPAD_FLASHWP_SLEWRATE_Msk \
	(0x4UL) /*!< SLEWRATE (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHWP_PULLDWEN_Pos \
	(3UL) /*!< PULLDWEN (Bit 3)                                      */
#define IOPAD_FLASHWP_PULLDWEN_Msk \
	(0x8UL) /*!< PULLDWEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHWP_PULLUPEN_Pos \
	(4UL) /*!< PULLUPEN (Bit 4)                                      */
#define IOPAD_FLASHWP_PULLUPEN_Msk \
	(0x10UL) /*!< PULLUPEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHWP_SCHEN_Pos \
	(5UL) /*!< SCHEN (Bit 5)                                         */
#define IOPAD_FLASHWP_SCHEN_Msk \
	(0x20UL) /*!< SCHEN (Bitfield-Mask: 0x01)                           */
/* =======================================================  FLASHHOLD
 * ======================================================= */
#define IOPAD_FLASHHOLD_INDETEN_Pos \
	(0UL) /*!< INDETEN (Bit 0)                                       */
#define IOPAD_FLASHHOLD_INDETEN_Msk \
	(0x1UL) /*!< INDETEN (Bitfield-Mask: 0x01)                         */
#define IOPAD_FLASHHOLD_OUTDRV_Pos \
	(1UL) /*!< OUTDRV (Bit 1)                                        */
#define IOPAD_FLASHHOLD_OUTDRV_Msk \
	(0x2UL) /*!< OUTDRV (Bitfield-Mask: 0x01)                          */
#define IOPAD_FLASHHOLD_SLEWRATE_Pos \
	(2UL) /*!< SLEWRATE (Bit 2)                                      */
#define IOPAD_FLASHHOLD_SLEWRATE_Msk \
	(0x4UL) /*!< SLEWRATE (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHHOLD_PULLDWEN_Pos \
	(3UL) /*!< PULLDWEN (Bit 3)                                      */
#define IOPAD_FLASHHOLD_PULLDWEN_Msk \
	(0x8UL) /*!< PULLDWEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHHOLD_PULLUPEN_Pos \
	(4UL) /*!< PULLUPEN (Bit 4)                                      */
#define IOPAD_FLASHHOLD_PULLUPEN_Msk \
	(0x10UL) /*!< PULLUPEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHHOLD_SCHEN_Pos \
	(5UL) /*!< SCHEN (Bit 5)                                         */
#define IOPAD_FLASHHOLD_SCHEN_Msk \
	(0x20UL) /*!< SCHEN (Bitfield-Mask: 0x01)                           */
/* ========================================================  FLASHSI
 * ======================================================== */
#define IOPAD_FLASHSI_INDETEN_Pos \
	(0UL) /*!< INDETEN (Bit 0)                                       */
#define IOPAD_FLASHSI_INDETEN_Msk \
	(0x1UL) /*!< INDETEN (Bitfield-Mask: 0x01)                         */
#define IOPAD_FLASHSI_OUTDRV_Pos \
	(1UL) /*!< OUTDRV (Bit 1)                                        */
#define IOPAD_FLASHSI_OUTDRV_Msk \
	(0x2UL) /*!< OUTDRV (Bitfield-Mask: 0x01)                          */
#define IOPAD_FLASHSI_SLEWRATE_Pos \
	(2UL) /*!< SLEWRATE (Bit 2)                                      */
#define IOPAD_FLASHSI_SLEWRATE_Msk \
	(0x4UL) /*!< SLEWRATE (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHSI_PULLDWEN_Pos \
	(3UL) /*!< PULLDWEN (Bit 3)                                      */
#define IOPAD_FLASHSI_PULLDWEN_Msk \
	(0x8UL) /*!< PULLDWEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHSI_PULLUPEN_Pos \
	(4UL) /*!< PULLUPEN (Bit 4)                                      */
#define IOPAD_FLASHSI_PULLUPEN_Msk \
	(0x10UL) /*!< PULLUPEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHSI_SCHEN_Pos \
	(5UL) /*!< SCHEN (Bit 5)                                         */
#define IOPAD_FLASHSI_SCHEN_Msk \
	(0x20UL) /*!< SCHEN (Bitfield-Mask: 0x01)                           */
/* ========================================================  FLASHSO
 * ======================================================== */
#define IOPAD_FLASHSO_INDETEN_Pos \
	(0UL) /*!< INDETEN (Bit 0)                                       */
#define IOPAD_FLASHSO_INDETEN_Msk \
	(0x1UL) /*!< INDETEN (Bitfield-Mask: 0x01)                         */
#define IOPAD_FLASHSO_OUTDRV_Pos \
	(1UL) /*!< OUTDRV (Bit 1)                                        */
#define IOPAD_FLASHSO_OUTDRV_Msk \
	(0x2UL) /*!< OUTDRV (Bitfield-Mask: 0x01)                          */
#define IOPAD_FLASHSO_SLEWRATE_Pos \
	(2UL) /*!< SLEWRATE (Bit 2)                                      */
#define IOPAD_FLASHSO_SLEWRATE_Msk \
	(0x4UL) /*!< SLEWRATE (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHSO_PULLDWEN_Pos \
	(3UL) /*!< PULLDWEN (Bit 3)                                      */
#define IOPAD_FLASHSO_PULLDWEN_Msk \
	(0x8UL) /*!< PULLDWEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHSO_PULLUPEN_Pos \
	(4UL) /*!< PULLUPEN (Bit 4)                                      */
#define IOPAD_FLASHSO_PULLUPEN_Msk \
	(0x10UL) /*!< PULLUPEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHSO_SCHEN_Pos \
	(5UL) /*!< SCHEN (Bit 5)                                         */
#define IOPAD_FLASHSO_SCHEN_Msk \
	(0x20UL) /*!< SCHEN (Bitfield-Mask: 0x01)                           */
/* ========================================================  FLASHCS
 * ======================================================== */
#define IOPAD_FLASHCS_INDETEN_Pos \
	(0UL) /*!< INDETEN (Bit 0)                                       */
#define IOPAD_FLASHCS_INDETEN_Msk \
	(0x1UL) /*!< INDETEN (Bitfield-Mask: 0x01)                         */
#define IOPAD_FLASHCS_OUTDRV_Pos \
	(1UL) /*!< OUTDRV (Bit 1)                                        */
#define IOPAD_FLASHCS_OUTDRV_Msk \
	(0x2UL) /*!< OUTDRV (Bitfield-Mask: 0x01)                          */
#define IOPAD_FLASHCS_SLEWRATE_Pos \
	(2UL) /*!< SLEWRATE (Bit 2)                                      */
#define IOPAD_FLASHCS_SLEWRATE_Msk \
	(0x4UL) /*!< SLEWRATE (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHCS_PULLDWEN_Pos \
	(3UL) /*!< PULLDWEN (Bit 3)                                      */
#define IOPAD_FLASHCS_PULLDWEN_Msk \
	(0x8UL) /*!< PULLDWEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHCS_PULLUPEN_Pos \
	(4UL) /*!< PULLUPEN (Bit 4)                                      */
#define IOPAD_FLASHCS_PULLUPEN_Msk \
	(0x10UL) /*!< PULLUPEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHCS_SCHEN_Pos \
	(5UL) /*!< SCHEN (Bit 5)                                         */
#define IOPAD_FLASHCS_SCHEN_Msk \
	(0x20UL) /*!< SCHEN (Bitfield-Mask: 0x01)                           */
/* =======================================================  FLASHCLK
 * ======================================================== */
#define IOPAD_FLASHCLK_INDETEN_Pos \
	(0UL) /*!< INDETEN (Bit 0)                                       */
#define IOPAD_FLASHCLK_INDETEN_Msk \
	(0x1UL) /*!< INDETEN (Bitfield-Mask: 0x01)                         */
#define IOPAD_FLASHCLK_OUTDRV_Pos \
	(1UL) /*!< OUTDRV (Bit 1)                                        */
#define IOPAD_FLASHCLK_OUTDRV_Msk \
	(0x2UL) /*!< OUTDRV (Bitfield-Mask: 0x01)                          */
#define IOPAD_FLASHCLK_SLEWRATE_Pos \
	(2UL) /*!< SLEWRATE (Bit 2)                                      */
#define IOPAD_FLASHCLK_SLEWRATE_Msk \
	(0x4UL) /*!< SLEWRATE (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHCLK_PULLDWEN_Pos \
	(3UL) /*!< PULLDWEN (Bit 3)                                      */
#define IOPAD_FLASHCLK_PULLDWEN_Msk \
	(0x8UL) /*!< PULLDWEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHCLK_PULLUPEN_Pos \
	(4UL) /*!< PULLUPEN (Bit 4)                                      */
#define IOPAD_FLASHCLK_PULLUPEN_Msk \
	(0x10UL) /*!< PULLUPEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_FLASHCLK_SCHEN_Pos \
	(5UL) /*!< SCHEN (Bit 5)                                         */
#define IOPAD_FLASHCLK_SCHEN_Msk \
	(0x20UL) /*!< SCHEN (Bitfield-Mask: 0x01)                           */
/* =========================================================  PECI
 * ========================================================== */
#define IOPAD_PECI_INDETEN_Pos \
	(0UL) /*!< INDETEN (Bit 0)                                       */
#define IOPAD_PECI_INDETEN_Msk \
	(0x1UL) /*!< INDETEN (Bitfield-Mask: 0x01)                         */
#define IOPAD_PECI_OUTDRV_Pos \
	(1UL) /*!< OUTDRV (Bit 1)                                        */
#define IOPAD_PECI_OUTDRV_Msk \
	(0x2UL) /*!< OUTDRV (Bitfield-Mask: 0x01)                          */
#define IOPAD_PECI_SLEWRATE_Pos \
	(2UL) /*!< SLEWRATE (Bit 2)                                      */
#define IOPAD_PECI_SLEWRATE_Msk \
	(0x4UL) /*!< SLEWRATE (Bitfield-Mask: 0x01)                        */
#define IOPAD_PECI_PULLDWEN_Pos \
	(3UL) /*!< PULLDWEN (Bit 3)                                      */
#define IOPAD_PECI_PULLDWEN_Msk \
	(0x8UL) /*!< PULLDWEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_PECI_PULLUPEN_Pos \
	(4UL) /*!< PULLUPEN (Bit 4)                                      */
#define IOPAD_PECI_PULLUPEN_Msk \
	(0x10UL) /*!< PULLUPEN (Bitfield-Mask: 0x01)                        */
#define IOPAD_PECI_SCHEN_Pos \
	(5UL) /*!< SCHEN (Bit 5)                                         */
#define IOPAD_PECI_SCHEN_Msk \
	(0x20UL) /*!< SCHEN (Bitfield-Mask: 0x01)                           */

/* ==========================================================  GCR
 * ========================================================== */
#define GPIO_GCR_DIR_Pos \
	(0UL) /*!< DIR (Bit 0)                                           */
#define GPIO_GCR_DIR_Msk \
	(0x1UL) /*!< DIR (Bitfield-Mask: 0x01)                             */
#define GPIO_GCR_INDETEN_Pos \
	(1UL) /*!< INDETEN (Bit 1)                                       */
#define GPIO_GCR_INDETEN_Msk \
	(0x2UL) /*!< INDETEN (Bitfield-Mask: 0x01)                         */
#define GPIO_GCR_INVOLMD_Pos \
	(2UL) /*!< INVOLMD (Bit 2)                                       */
#define GPIO_GCR_INVOLMD_Msk \
	(0x4UL) /*!< INVOLMD (Bitfield-Mask: 0x01)                         */
#define GPIO_GCR_PINSTS_Pos \
	(3UL) /*!< PINSTS (Bit 3)                                        */
#define GPIO_GCR_PINSTS_Msk \
	(0x8UL) /*!< PINSTS (Bitfield-Mask: 0x01)                          */
#define GPIO_GCR_MFCTRL_Pos \
	(8UL) /*!< MFCTRL (Bit 8)                                        */
#define GPIO_GCR_MFCTRL_Msk (0x700UL) /*!< MFCTRL (Bitfield-Mask: 0x07) */
#define GPIO_GCR_OUTDRV_Pos \
	(11UL) /*!< OUTDRV (Bit 11)                                       */
#define GPIO_GCR_OUTDRV_Msk (0x800UL) /*!< OUTDRV (Bitfield-Mask: 0x01) */
#define GPIO_GCR_SLEWRATE_Pos \
	(12UL) /*!< SLEWRATE (Bit 12)                                     */
#define GPIO_GCR_SLEWRATE_Msk                          \
	(0x1000UL) /*!< SLEWRATE (Bitfield-Mask: 0x01) \
		    */
#define GPIO_GCR_PULLDWEN_Pos \
	(13UL) /*!< PULLDWEN (Bit 13)                                     */
#define GPIO_GCR_PULLDWEN_Msk                          \
	(0x2000UL) /*!< PULLDWEN (Bitfield-Mask: 0x01) \
		    */
#define GPIO_GCR_PULLUPEN_Pos \
	(14UL) /*!< PULLUPEN (Bit 14)                                     */
#define GPIO_GCR_PULLUPEN_Msk                          \
	(0x4000UL) /*!< PULLUPEN (Bitfield-Mask: 0x01) \
		    */
#define GPIO_GCR_SCHEN_Pos \
	(15UL) /*!< SCHEN (Bit 15)                                        */
#define GPIO_GCR_SCHEN_Msk (0x8000UL) /*!< SCHEN (Bitfield-Mask: 0x01) */
#define GPIO_GCR_OUTMD_Pos \
	(16UL) /*!< OUTMD (Bit 16)                                        */
#define GPIO_GCR_OUTMD_Msk (0x10000UL) /*!< OUTMD (Bitfield-Mask: 0x01) */
#define GPIO_GCR_OUTCTRL_Pos \
	(17UL) /*!< OUTCTRL (Bit 17)                                      */
#define GPIO_GCR_OUTCTRL_Msk (0x20000UL) /*!< OUTCTRL (Bitfield-Mask: 0x01) */
#define GPIO_GCR_INTCTRL_Pos \
	(24UL) /*!< INTCTRL (Bit 24)                                      */
#define GPIO_GCR_INTCTRL_Msk                             \
	(0x7000000UL) /*!< INTCTRL (Bitfield-Mask: 0x07) \
		       */
#define GPIO_GCR_INTEN_Pos \
	(28UL) /*!< INTEN (Bit 28)                                        */
#define GPIO_GCR_INTEN_Msk (0x10000000UL) /*!< INTEN (Bitfield-Mask: 0x01) */
#define GPIO_GCR_INTSTS_Pos \
	(31UL) /*!< INTSTS (Bit 31)                                       */
#define GPIO_GCR_INTSTS_Msk                              \
	(0x80000000UL) /*!< INTSTS (Bitfield-Mask: 0x01) \
			*/

/////////////////////////UART START
/* RBR */
#define UART_RBR_DATA_Pos (0UL)
#define UART_RBR_DATA_Msk (0xffUL)
/* THR */
#define UART_THR_DATA_Pos (0UL)
#define UART_THR_DATA_Msk (0xffUL)
/* DLL */
#define UART_DLL_DIVL_Pos (0UL)
#define UART_DLL_DIVL_Msk (0xffUL)
/* DLH */
#define UART_DLH_DIVH_Pos (0UL)
#define UART_DLH_DIVH_Msk (0xffUL)
/* IER */
#define UART_IER_ERBFI_Pos (0UL)
#define UART_IER_ERBFI_Msk (0x1UL)
#define UART_IER_ETBEI_Pos (1UL)
#define UART_IER_ETBEI_Msk (0x2UL)
#define UART_IER_ELSI_Pos (2UL)
#define UART_IER_ELSI_Msk (0x4UL)
#define UART_IER_PTIME_Pos (7UL)
#define UART_IER_PTIME_Msk (0x80UL)
/* IIR */
#define UART_IIR_IID_Pos (0UL)
#define UART_IIR_IID_Msk (0xfUL)
#define UART_IIR_FIFOSE_Pos (6UL)
#define UART_IIR_FIFOSE_Msk (0xc0UL)
/* FCR */
#define UART_FCR_FIFOE_Pos (0UL)
#define UART_FCR_FIFOE_Msk (0x1UL)
#define UART_FCR_RFIFOR_Pos (1UL)
#define UART_FCR_RFIFOR_Msk (0x2UL)
#define UART_FCR_XFIFOR_Pos (2UL)
#define UART_FCR_XFIFOR_Msk (0x4UL)
#define UART_FCR_TXTRILEV_Pos (4UL)
#define UART_FCR_TXTRILEV_Msk (0x30UL)
#define UART_FCR_RXTRILEV_Pos (6UL)
#define UART_FCR_RXTRILEV_Msk (0xc0UL)
/* LCR */
#define UART_LCR_DLS_Pos (0UL)
#define UART_LCR_DLS_Msk (0x3UL)
#define UART_LCR_STOP_Pos (2UL)
#define UART_LCR_STOP_Msk (0x4UL)
#define UART_LCR_PEN_Pos (3UL)
#define UART_LCR_PEN_Msk (0x8UL)
#define UART_LCR_EPS_Pos (4UL)
#define UART_LCR_EPS_Msk (0x10UL)
#define UART_LCR_STP_Pos (5UL)
#define UART_LCR_STP_Msk (0x20UL)
#define UART_LCR_BC_Pos (6UL)
#define UART_LCR_BC_Msk (0x40UL)
#define UART_LCR_DLAB_Pos (7UL)
#define UART_LCR_DLAB_Msk (0x80UL)
/* LSR */
#define UART_LSR_DR_Pos (0UL)
#define UART_LSR_DR_Msk (0x1UL)
#define UART_LSR_OE_Pos (1UL)
#define UART_LSR_OE_Msk (0x2UL)
#define UART_LSR_PE_Pos (2UL)
#define UART_LSR_PE_Msk (0x4UL)
#define UART_LSR_FE_Pos (3UL)
#define UART_LSR_FE_Msk (0x8UL)
#define UART_LSR_BI_Pos (4UL)
#define UART_LSR_BI_Msk (0x10UL)
#define UART_LSR_THRE_Pos (5UL)
#define UART_LSR_THRE_Msk (0x20UL)
#define UART_LSR_TEMT_Pos (6UL)
#define UART_LSR_TEMT_Msk (0x40UL)
#define UART_LSR_RFE_Pos (7UL)
#define UART_LSR_RFE_Msk (0x80UL)
/* USR */
#define UART_USR_BUSY_Pos (0UL)
#define UART_USR_BUSY_Msk (0x1UL)
#define UART_USR_TFNF_Pos (1UL)
#define UART_USR_TFNF_Msk (0x2UL)
#define UART_USR_TFE_Pos (2UL)
#define UART_USR_TFE_Msk (0x4UL)
#define UART_USR_RFNE_Pos (3UL)
#define UART_USR_RFNE_Msk (0x8UL)
#define UART_USR_RFF_Pos (4UL)
#define UART_USR_RFF_Msk (0x10UL)
/* TFL */
/* RFL */
/* SRR */
#define UART_SRR_UR_Pos (0UL)
#define UART_SRR_UR_Msk (0x1UL)
#define UART_SRR_RFR_Pos (1UL)
#define UART_SRR_RFR_Msk (0x2UL)
#define UART_SRR_XFR_Pos (2UL)
#define UART_SRR_XFR_Msk (0x4UL)

/////////////////////////UART END
/////////////////////////SYSTEM START
/* UARTCLK */
#define SYSTEM_UARTCLK_PWR_Pos (0UL)
#define SYSTEM_UARTCLK_PWR_Msk (0x1UL)
#define SYSTEM_UARTCLK_SRC_Pos (1UL)
#define SYSTEM_UARTCLK_SRC_Msk (0x2UL)
#define SYSTEM_UARTCLK_DIV_Pos (2UL)
#define SYSTEM_UARTCLK_DIV_Msk (0xCUL)
/////////////////////////SYSTEM END
/////////////////////////SLWTMR START
/* CTRL */
#define SLWTMR_CTRL_EN (0x1UL)

#define SLWTMR_CTRL_MDSELS_ONESHOT 0
#define SLWTMR_CTRL_MDSELS_PERIOD (0x2UL)

#define SLWTMR_CTRL_INTEN_EN (0x4UL)
/* INTSTS */
#define SLWTMR_INTSTS_STS (0x1UL)
/////////////////////////SLWTMR END

#endif /* PLATFORM_EC_ZEPHYR_CHIP_RTK_FLAME_REG_H_ */
