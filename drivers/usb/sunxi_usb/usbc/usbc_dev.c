/*
 * drivers/usb/sunxi_usb/usbc/usbc_dev.c
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * daniel, 2009.09.01
 *
 * usb register ops.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include  "usbc_i.h"

/*
 * select the usb transfer type, eg control/iso/interrupt/bulk transfer
 */

static void __USBC_Dev_TsType_default(__u32 usbc_base_addr)
{
	//disable all transfer type
	USBC_REG_clear_bit_b(USBC_BP_POWER_D_ISO_UPDATE_EN, USBC_REG_PCTL(usbc_base_addr));
}

static void __USBC_Dev_TsType_Ctrl(__u32 usbc_base_addr)
{
	//--<1>--disable other transfer type
	USBC_REG_clear_bit_b(USBC_BP_POWER_D_ISO_UPDATE_EN, USBC_REG_PCTL(usbc_base_addr));

	//--<2>--select Ctrl type
	/* donot need config */
}

static void __USBC_Dev_TsType_Iso(__u32 usbc_base_addr)
{
	//--<1>--disable other transfer type
	/* donot need config */

	//--<2>--select Ctrl type
	USBC_REG_set_bit_b(USBC_BP_POWER_D_ISO_UPDATE_EN, USBC_REG_PCTL(usbc_base_addr));
}

static void __USBC_Dev_TsType_Int(__u32 usbc_base_addr)
{
	//--<1>--disable other transfer type
	USBC_REG_clear_bit_b(USBC_BP_POWER_D_ISO_UPDATE_EN, USBC_REG_PCTL(usbc_base_addr));

	//--<2>--select Ctrl type
	/* donot need config */
}

static void __USBC_Dev_TsType_Bulk(__u32 usbc_base_addr)
{
	//--<1>--disable other transfer type
	USBC_REG_clear_bit_b(USBC_BP_POWER_D_ISO_UPDATE_EN, USBC_REG_PCTL(usbc_base_addr));

	//--<2>--select Ctrl type
	/* donot need config */
}

/*
 * select the usb speed type, eg high/full/low
 */

static void __USBC_Dev_TsMode_default(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_b(USBC_BP_POWER_D_HIGH_SPEED_EN, USBC_REG_PCTL(usbc_base_addr));
}

static void __USBC_Dev_TsMode_Hs(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_b(USBC_BP_POWER_D_HIGH_SPEED_EN, USBC_REG_PCTL(usbc_base_addr));
}

static void __USBC_Dev_TsMode_Fs(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_b(USBC_BP_POWER_D_HIGH_SPEED_EN, USBC_REG_PCTL(usbc_base_addr));
}

static void __USBC_Dev_TsMode_Ls(__u32 usbc_base_addr)
{
	// hw not support ls, so default select fs
	__USBC_Dev_TsMode_Fs(usbc_base_addr);
}

static void __USBC_Dev_ep0_ConfigEp0_Default(__u32 usbc_base_addr)
{
	USBC_Writew(1<<USBC_BP_CSR0_D_FLUSH_FIFO, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_ep0_ConfigEp0(__u32 usbc_base_addr)
{
	USBC_Writew(1<<USBC_BP_CSR0_D_FLUSH_FIFO, USBC_REG_CSR0(usbc_base_addr));
}

static __u32 __USBC_Dev_ep0_IsReadDataReady(__u32 usbc_base_addr)
{
	return USBC_REG_test_bit_w(USBC_BP_CSR0_D_RX_PKT_READY, USBC_REG_CSR0(usbc_base_addr));
}

static __u32 __USBC_Dev_ep0_IsWriteDataReady(__u32 usbc_base_addr)
{
	return USBC_REG_test_bit_w(USBC_BP_CSR0_D_TX_PKT_READY, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_ep0_ReadDataHalf(__u32 usbc_base_addr)
{
	USBC_Writew(1<<USBC_BP_CSR0_D_SERVICED_RX_PKT_READY, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_ep0_ReadDataComplete(__u32 usbc_base_addr)
{
	USBC_Writew((1<<USBC_BP_CSR0_D_SERVICED_RX_PKT_READY) | (1<<USBC_BP_CSR0_D_DATA_END),
		USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_ep0_WriteDataHalf(__u32 usbc_base_addr)
{
	USBC_Writew(1<<USBC_BP_CSR0_D_TX_PKT_READY, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_ep0_WriteDataComplete(__u32 usbc_base_addr)
{
	USBC_Writew((1<<USBC_BP_CSR0_D_TX_PKT_READY) | (1<<USBC_BP_CSR0_D_DATA_END),
		USBC_REG_CSR0(usbc_base_addr));
}

static __u32 __USBC_Dev_ep0_IsEpStall(__u32 usbc_base_addr)
{
	return USBC_REG_test_bit_w(USBC_BP_CSR0_D_SENT_STALL, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_ep0_SendStall(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_w(USBC_BP_CSR0_D_SEND_STALL, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_ep0_ClearStall(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_w(USBC_BP_CSR0_D_SEND_STALL, USBC_REG_CSR0(usbc_base_addr));
	USBC_REG_clear_bit_w(USBC_BP_CSR0_D_SENT_STALL, USBC_REG_CSR0(usbc_base_addr));
}

static __u32 __USBC_Dev_ep0_IsSetupEnd(__u32 usbc_base_addr)
{
	return USBC_REG_test_bit_w(USBC_BP_CSR0_D_SETUP_END, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_ep0_ClearSetupEnd(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_w(USBC_BP_CSR0_D_SERVICED_SETUP_END, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_Tx_EnableIsoEp(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_w(USBC_BP_TXCSR_D_ISO, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Dev_Tx_EnableIntEp(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_w(USBC_BP_TXCSR_D_ISO, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Dev_Tx_EnableBulkEp(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_w(USBC_BP_TXCSR_D_ISO, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Dev_Tx_ConfigEp_Default(__u32 usbc_base_addr)
{
	//--<1>--clear tx csr
	USBC_Writew(0x00, USBC_REG_TXCSR(usbc_base_addr));

	//--<2>--clear tx ep max packet
	USBC_Writew(0x00, USBC_REG_TXMAXP(usbc_base_addr));

	//--<3>--config ep transfer type
}

static void __USBC_Dev_Tx_ConfigEp(__u32 usbc_base_addr, __u32 ts_type, __u32 is_double_fifo, __u32 ep_MaxPkt)
{
	__u16 reg_val = 0;
	__u16 temp = 0;

	//--<1>--config tx csr
	reg_val = (1 << USBC_BP_TXCSR_D_MODE);
	reg_val |= (1 << USBC_BP_TXCSR_D_CLEAR_DATA_TOGGLE);
	reg_val |= (1 << USBC_BP_TXCSR_D_FLUSH_FIFO);
	USBC_Writew(reg_val, USBC_REG_TXCSR(usbc_base_addr));

	if (is_double_fifo) {
		USBC_Writew(reg_val, USBC_REG_TXCSR(usbc_base_addr));
	}

	//--<2>--config tx ep max packet
	reg_val = USBC_Readw(USBC_REG_TXMAXP(usbc_base_addr));
	temp    = ep_MaxPkt & ((1 << USBC_BP_TXMAXP_PACKET_COUNT) - 1);
	reg_val |= temp;
	USBC_Writew(reg_val, USBC_REG_TXMAXP(usbc_base_addr));

	//--<3>--config ep transfer type
	switch(ts_type) {
	case USBC_TS_TYPE_ISO:
		__USBC_Dev_Tx_EnableIsoEp(usbc_base_addr);
		break;

	case USBC_TS_TYPE_INT:
		__USBC_Dev_Tx_EnableIntEp(usbc_base_addr);
		break;

	case USBC_TS_TYPE_BULK:
		__USBC_Dev_Tx_EnableBulkEp(usbc_base_addr);
		break;

	default:
		__USBC_Dev_Tx_EnableBulkEp(usbc_base_addr);
	}
}

static void __USBC_Dev_Tx_ConfigEpDma(__u32 usbc_base_addr)
{
	__u16 ep_csr = 0;

	//auto_set, tx_mode, dma_tx_en, mode1
	ep_csr = USBC_Readb(USBC_REG_TXCSR(usbc_base_addr) + 1);
	ep_csr |= (1 << USBC_BP_TXCSR_D_AUTOSET) >> 8;
	ep_csr |= (1 << USBC_BP_TXCSR_D_MODE) >> 8;
	ep_csr |= (1 << USBC_BP_TXCSR_D_DMA_REQ_EN) >> 8;
	ep_csr |= (1 << USBC_BP_TXCSR_D_DMA_REQ_MODE) >> 8;
	USBC_Writeb(ep_csr, (USBC_REG_TXCSR(usbc_base_addr) + 1));
}

static void __USBC_Dev_Tx_ClearEpDma(__u32 usbc_base_addr)
{
	__u16 ep_csr = 0;

	//auto_set, dma_tx_en, mode1
	ep_csr = USBC_Readb(USBC_REG_TXCSR(usbc_base_addr) + 1);
	ep_csr &= ~((1 << USBC_BP_TXCSR_D_AUTOSET) >> 8);
	ep_csr &= ~((1 << USBC_BP_TXCSR_D_DMA_REQ_EN) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_TXCSR(usbc_base_addr) + 1));

	//DMA_REQ_EN and DMA_REQ_MODE cannot be cleared in the same cycle
	ep_csr = USBC_Readb(USBC_REG_TXCSR(usbc_base_addr) + 1);
	ep_csr &= ~((1 << USBC_BP_TXCSR_D_DMA_REQ_MODE) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_TXCSR(usbc_base_addr) + 1));
}

static __u32 __USBC_Dev_Tx_IsWriteDataReady(__u32 usbc_base_addr)
{
	__u32 temp = 0;

	temp = USBC_Readw(USBC_REG_TXCSR(usbc_base_addr));
	temp &= (1 << USBC_BP_TXCSR_D_TX_READY);

	return temp;
}

static __u32 __USBC_Dev_Tx_IsWriteDataReady_FifoEmpty(__u32 usbc_base_addr)
{
	__u32 temp = 0;

	temp = USBC_Readw(USBC_REG_TXCSR(usbc_base_addr));
	temp &= (1 << USBC_BP_TXCSR_D_TX_READY) | (1 << USBC_BP_TXCSR_D_FIFO_NOT_EMPTY);

	return temp;
}


static void __USBC_Dev_Tx_WriteDataHalf(__u32 usbc_base_addr)
{
	__u16 ep_csr = 0;

	ep_csr = USBC_Readw(USBC_REG_TXCSR(usbc_base_addr));
	ep_csr |= 1 << USBC_BP_TXCSR_D_TX_READY;
	ep_csr &= ~(1 << USBC_BP_TXCSR_D_UNDER_RUN);
	USBC_Writew(ep_csr, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Dev_Tx_WriteDataComplete(__u32 usbc_base_addr)
{
	__u16 ep_csr = 0;

	ep_csr = USBC_Readw(USBC_REG_TXCSR(usbc_base_addr));
	ep_csr |= 1 << USBC_BP_TXCSR_D_TX_READY;
	ep_csr &= ~(1 << USBC_BP_TXCSR_D_UNDER_RUN);
	USBC_Writew(ep_csr, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Dev_Tx_SendStall(__u32 usbc_base_addr)
{
	//send stall, and fifo is flushed automaticly
	USBC_REG_set_bit_w(USBC_BP_TXCSR_D_SEND_STALL, USBC_REG_TXCSR(usbc_base_addr));
}

static __u32 __USBC_Dev_Tx_IsEpStall(__u32 usbc_base_addr)
{
	return USBC_REG_test_bit_w(USBC_BP_TXCSR_D_SENT_STALL, USBC_REG_TXCSR(usbc_base_addr));
}


static void __USBC_Dev_Tx_ClearStall(__u32 usbc_base_addr)
{
	__u32 reg_val;

	reg_val = USBC_Readw(USBC_REG_TXCSR(usbc_base_addr));
	reg_val &= ~((1 << USBC_BP_TXCSR_D_SENT_STALL)|(1 << USBC_BP_TXCSR_D_SEND_STALL));
	USBC_Writew(reg_val, USBC_REG_TXCSR(usbc_base_addr));
}

/* in order to clear compile warning */
/*
static __u32 __USBC_Dev_Tx_IsEpIncomp(__u32 usbc_base_addr)
{
	return USBC_REG_test_bit_w(USBC_BP_TXCSR_D_INCOMPLETE, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Dev_Tx_ClearIncomp(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_w(USBC_BP_TXCSR_D_INCOMPLETE, USBC_REG_TXCSR(usbc_base_addr));
}
*/

static void __USBC_Dev_Rx_EnableIsoEp(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_w(USBC_BP_RXCSR_D_ISO, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Dev_Rx_EnableIntEp(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_w(USBC_BP_RXCSR_D_ISO, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Dev_Rx_EnableBulkEp(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_w(USBC_BP_RXCSR_D_ISO, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Dev_Rx_ConfigEp_Default(__u32 usbc_base_addr)
{
	//--<1>--clear tx csr
	USBC_Writew(0x00, USBC_REG_RXCSR(usbc_base_addr));

	//--<2>--clear tx ep max packet
	USBC_Writew(0x00, USBC_REG_RXMAXP(usbc_base_addr));

	//--<3>--config ep transfer type
}

static void __USBC_Dev_Rx_ConfigEp(__u32 usbc_base_addr, __u32 ts_type, __u32 is_double_fifo, __u32 ep_MaxPkt)
{
	__u16 reg_val = 0;
	__u16 temp = 0;

	//--<1>--config tx csr
	USBC_Writew((1 << USBC_BP_RXCSR_D_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_RXCSR_D_FLUSH_FIFO),
	USBC_REG_RXCSR(usbc_base_addr));

	if (is_double_fifo) {
		USBC_Writew((1 << USBC_BP_RXCSR_D_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_RXCSR_D_FLUSH_FIFO),
		USBC_REG_RXCSR(usbc_base_addr));
	}

	//--<2>--config tx ep max packet
	reg_val = USBC_Readw(USBC_REG_RXMAXP(usbc_base_addr));
	temp    = ep_MaxPkt & ((1 << USBC_BP_RXMAXP_PACKET_COUNT) - 1);
	reg_val |= temp;
	USBC_Writew(reg_val, USBC_REG_RXMAXP(usbc_base_addr));

	//--<3>--config ep transfer type
	switch(ts_type) {
	case USBC_TS_TYPE_ISO:
		__USBC_Dev_Rx_EnableIsoEp(usbc_base_addr);
		break;

	case USBC_TS_TYPE_INT:
		__USBC_Dev_Rx_EnableIntEp(usbc_base_addr);
		break;

	case USBC_TS_TYPE_BULK:
		__USBC_Dev_Rx_EnableBulkEp(usbc_base_addr);
		break;

	default:
		__USBC_Dev_Rx_EnableBulkEp(usbc_base_addr);
	}
}

static void __USBC_Dev_Rx_ConfigEpDma(__u32 usbc_base_addr)
{
	__u16 ep_csr = 0;

	//auto_clear, dma_rx_en, mode0
	ep_csr = USBC_Readb(USBC_REG_RXCSR(usbc_base_addr) + 1);
	ep_csr |= ((1 << USBC_BP_RXCSR_D_DMA_REQ_MODE) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_RXCSR(usbc_base_addr) + 1));

	ep_csr = USBC_Readb(USBC_REG_RXCSR(usbc_base_addr) + 1);
	ep_csr |= ((1 << USBC_BP_RXCSR_D_AUTO_CLEAR) >> 8);
	ep_csr |= ((1 << USBC_BP_RXCSR_D_DMA_REQ_EN) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_RXCSR(usbc_base_addr) + 1));

	ep_csr = USBC_Readb(USBC_REG_RXCSR(usbc_base_addr) + 1);
	ep_csr &= ~((1 << USBC_BP_RXCSR_D_DMA_REQ_MODE) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_RXCSR(usbc_base_addr) + 1));

	ep_csr = USBC_Readb(USBC_REG_RXCSR(usbc_base_addr) + 1);
	ep_csr |= ((1 << USBC_BP_RXCSR_D_DMA_REQ_MODE) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_RXCSR(usbc_base_addr) + 1));
}

static void __USBC_Dev_Rx_ClearEpDma(__u32 usbc_base_addr)
{
	__u16 ep_csr = 0;

	//auto_clear, dma_rx_en, mode0
	ep_csr = USBC_Readb(USBC_REG_RXCSR(usbc_base_addr) + 1);

	ep_csr &= ~((1 << USBC_BP_RXCSR_D_AUTO_CLEAR) >> 8);
	ep_csr &= ~((1 << USBC_BP_RXCSR_D_DMA_REQ_MODE) >> 8);
	ep_csr &= ~((1 << USBC_BP_RXCSR_D_DMA_REQ_EN) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_RXCSR(usbc_base_addr) + 1));
}

static __u32 __USBC_Dev_Rx_IsReadDataReady(__u32 usbc_base_addr)
{
	return USBC_REG_test_bit_w(USBC_BP_RXCSR_D_RX_PKT_READY, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Dev_Rx_ReadDataHalf(__u32 usbc_base_addr)
{
	__u32 reg_val = 0;

	//overrun, dataerr is used in iso transfer
	reg_val = USBC_Readw(USBC_REG_RXCSR(usbc_base_addr));
	reg_val &= ~(1 << USBC_BP_RXCSR_D_RX_PKT_READY);
	reg_val &= ~(1 << USBC_BP_RXCSR_D_OVERRUN);
	reg_val &= ~(1 << USBC_BP_RXCSR_D_DATA_ERROR);
	USBC_Writew(reg_val, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Dev_Rx_ReadDataComplete(__u32 usbc_base_addr)
{
	__u32 reg_val = 0;

	//overrun, dataerr is used in iso transfer
	reg_val = USBC_Readw(USBC_REG_RXCSR(usbc_base_addr));
	reg_val &= ~(1 << USBC_BP_RXCSR_D_RX_PKT_READY);
	reg_val &= ~(1 << USBC_BP_RXCSR_D_OVERRUN);
	reg_val &= ~(1 << USBC_BP_RXCSR_D_DATA_ERROR);
	USBC_Writew(reg_val, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Dev_Rx_SendStall(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_w(USBC_BP_RXCSR_D_SEND_STALL, USBC_REG_RXCSR(usbc_base_addr));
}

static __u32 __USBC_Dev_Rx_IsEpStall(__u32 usbc_base_addr)
{
	return USBC_REG_test_bit_w(USBC_BP_RXCSR_D_SENT_STALL, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Dev_Rx_ClearStall(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_w(USBC_BP_RXCSR_D_SEND_STALL, USBC_REG_RXCSR(usbc_base_addr));
	USBC_REG_clear_bit_w(USBC_BP_RXCSR_D_SENT_STALL, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Dev_ClearDma_Trans(__u32 usbc_base_addr)
{
	/*
	 * in SUN8IW5, SUN8IW6 and later ic, this bit is fix to 1, set when
	 * drv initialize;
	 * in SUN8IW5, SUN8IW6 former ic, we donot use inner dma, so this bit
	 * should be 0.
	 */
#if !defined (CONFIG_ARCH_SUN8IW5) && !defined (CONFIG_ARCH_SUN8IW6) && !defined (CONFIG_ARCH_SUN8IW9) && !defined (CONFIG_ARCH_SUN8IW8) && !defined (CONFIG_ARCH_SUN8IW7)

	__u32 reg_val;

	reg_val  = USBC_Readl(usbc_base_addr + USBC_REG_o_PCTL);
	reg_val &= ~(1 << 24);
	USBC_Writel(reg_val, usbc_base_addr + USBC_REG_o_PCTL);
#endif
}

static void __USBC_Dev_ConfigDma_Trans(__u32 usbc_base_addr)
{
	/*
	 * in SUN8IW5 and later ic, this bit is fix to 1, set when drv
	 * initialize, so donot set here;
	 * in SUN8IW5 former ic(eg SUN8IW3), we donot use inner dma(use cpu
	 * or outer dma), this bit should be 0, so cannot set here.
	 */
#if 0
	__u32 reg_val;

	reg_val  = readl(usbc_base_addr + USBC_REG_o_PCTL);
	reg_val |= (1 << 24);
	writel(reg_val, usbc_base_addr + USBC_REG_o_PCTL);
#endif
}

/*
 * clear the address allocated by host for device
 * @hUSB: handle return by USBC_open_otg, include the key data which USBC need
 */
void USBC_Dev_SetAddress_default(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return;
	}

	USBC_Writeb(0x00, USBC_REG_FADDR(usbc_otg->base_addr));
}

/*
 * set the address
 * @hUSB: handle return by USBC_open_otg, include the key data which USBC need
 */
void USBC_Dev_SetAddress(__hdle hUSB, __u8 address)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return;
	}

	USBC_Writeb(address, USBC_REG_FADDR(usbc_otg->base_addr));
}

__u32 USBC_Dev_QueryTransferMode(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return USBC_TS_MODE_UNKOWN;
	}

	if (USBC_REG_test_bit_b(USBC_BP_POWER_D_HIGH_SPEED_FLAG, USBC_REG_PCTL(usbc_otg->base_addr))) {
		return USBC_TS_MODE_HS;
	} else {
		return USBC_TS_MODE_FS;
	}
}

/*
 * config the device's transfer type and speed mode
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ts_type:    transfer type
 * @speed_mode: speed mode
 */
void USBC_Dev_ConfigTransferMode(__hdle hUSB, __u8 ts_type, __u8 speed_mode)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return;
	}

	//--<1>--select the transfer type
	//default bulk transfer
	switch(ts_type) {
	case USBC_TS_TYPE_CTRL:
		__USBC_Dev_TsType_Ctrl(usbc_otg->base_addr);
		break;

	case USBC_TS_TYPE_ISO:
		__USBC_Dev_TsType_Iso(usbc_otg->base_addr);
		break;

	case USBC_TS_TYPE_INT:
		__USBC_Dev_TsType_Int(usbc_otg->base_addr);
		break;

	case USBC_TS_TYPE_BULK:
		__USBC_Dev_TsType_Bulk(usbc_otg->base_addr);
		break;

	default:
		__USBC_Dev_TsType_default(usbc_otg->base_addr);
	}

	//--<2>--select the transfer speed
	switch(speed_mode) {
	case USBC_TS_MODE_HS:
		__USBC_Dev_TsMode_Hs(usbc_otg->base_addr);
		break;

	case USBC_TS_MODE_FS:
		__USBC_Dev_TsMode_Fs(usbc_otg->base_addr);
		break;

	case USBC_TS_MODE_LS:
		__USBC_Dev_TsMode_Ls(usbc_otg->base_addr);
		break;

	default:
		__USBC_Dev_TsMode_default(usbc_otg->base_addr);
	}
}

/*
 * the switch to communicate with PC
 * @hUSB:     handle return by USBC_open_otg, include the key data which USBC need
 * @is_on:    1 - open the switch, 0 - close the switch
 *
 */
void USBC_Dev_ConectSwitch(__hdle hUSB, __u32 is_on)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return ;
	}

	if (is_on == USBC_DEVICE_SWITCH_ON) {
		USBC_REG_set_bit_b(USBC_BP_POWER_D_SOFT_CONNECT, USBC_REG_PCTL(usbc_otg->base_addr));
	} else {
		USBC_REG_clear_bit_b(USBC_BP_POWER_D_SOFT_CONNECT, USBC_REG_PCTL(usbc_otg->base_addr));
	}
}

/*
 * query current device's status, eg reset, resume, suspend, etc.
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 *
 */
__u32 USBC_Dev_QueryPowerStatus(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return 0;
	}

	return (USBC_Readb(USBC_REG_PCTL(usbc_otg->base_addr)) & 0x0f);
}

/*
 * config EP, include double fifo, max packet size, etc.
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    transfer type
 * @is_double_fifo: speed mode
 * @ep_MaxPkt:  max packet size
 *
 */
__s32 USBC_Dev_ConfigEp(__hdle hUSB, __u32 ts_type, __u32 ep_type, __u32 is_double_fifo, __u32 ep_MaxPkt)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return -1;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		__USBC_Dev_ep0_ConfigEp0(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_TX:
		__USBC_Dev_Tx_ConfigEp(usbc_otg->base_addr, ts_type, is_double_fifo,  ep_MaxPkt);
		break;

	case USBC_EP_TYPE_RX:
		__USBC_Dev_Rx_ConfigEp(usbc_otg->base_addr, ts_type, is_double_fifo, ep_MaxPkt);
		break;

	default:
		return -1;
	}

	return 0;
}

/*
 * release all Ep resources, excpet irq
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 *
 * return: 0 - success, !0 - failed
 */
__s32 USBC_Dev_ConfigEp_Default(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return -1;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		__USBC_Dev_ep0_ConfigEp0_Default(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_TX:
		__USBC_Dev_Tx_ConfigEp_Default(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_RX:
		__USBC_Dev_Rx_ConfigEp_Default(usbc_otg->base_addr);
		break;

	default:
		return -1;
	}

	return 0;
}

/*
 * config  Ep's dma
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 *
 * return: 0 - success, !0 - failed
 */
__s32 USBC_Dev_ConfigEpDma(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return -1;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		//not support
		return -1;
		//break;

	case USBC_EP_TYPE_TX:
		__USBC_Dev_Tx_ConfigEpDma(usbc_otg->base_addr);
		__USBC_Dev_ConfigDma_Trans(usbc_otg->base_addr);
	break;

	case USBC_EP_TYPE_RX:
		__USBC_Dev_Rx_ConfigEpDma(usbc_otg->base_addr);
		__USBC_Dev_ConfigDma_Trans(usbc_otg->base_addr);
	break;

	default:
		return -1;
	}

	return 0;
}

/*
 * clear  Ep's dma configuration
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 *
 * return: 0 - success, !0 - failed
 */
__s32 USBC_Dev_ClearEpDma(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return -1;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		//not support
		return -1;
		//break;

	case USBC_EP_TYPE_TX:
		__USBC_Dev_Tx_ClearEpDma(usbc_otg->base_addr);
		__USBC_Dev_ClearDma_Trans(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_RX:
		__USBC_Dev_Rx_ClearEpDma(usbc_otg->base_addr);
		__USBC_Dev_ClearDma_Trans(usbc_otg->base_addr);
		break;

	default:
		return -1;
	}

	return 0;
}

/*
 * check if ep is stalled
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 *
 * return: 0 - success, !0 - failed
 */
__s32 USBC_Dev_IsEpStall(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return -1;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		__USBC_Dev_ep0_IsEpStall(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_TX:
		__USBC_Dev_Tx_IsEpStall(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_RX:
		__USBC_Dev_Rx_IsEpStall(usbc_otg->base_addr);
		break;

	default:
		return -1;
	}

	return 0;
}

/*
 * let ep enter stall status
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 *
 * return: 0 - success, !0 - failed
 */
__s32 USBC_Dev_EpSendStall(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return -1;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		__USBC_Dev_ep0_SendStall(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_TX:
		__USBC_Dev_Tx_SendStall(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_RX:
		__USBC_Dev_Rx_SendStall(usbc_otg->base_addr);
		break;

	default:
		return -1;
	}

	return 0;
}

/*
 * clear the ep's stall status
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 *
 * return: 0 - success, !0 - failed
 */
__s32 USBC_Dev_EpClearStall(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return -1;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		__USBC_Dev_ep0_ClearStall(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_TX:
		__USBC_Dev_Tx_ClearStall(usbc_otg->base_addr);
		break;

	case USBC_EP_TYPE_RX:
		__USBC_Dev_Rx_ClearStall(usbc_otg->base_addr);
		break;

	default:
		return -1;
	}

	return 0;
}

/*
 * check if ep0 is SetupEnd
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 *
 */
__u32 USBC_Dev_Ctrl_IsSetupEnd(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return 0;
	}

	return __USBC_Dev_ep0_IsSetupEnd(usbc_otg->base_addr);
}

/*
 * clear the ep0's SetupEnd status
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 *
 */
void USBC_Dev_Ctrl_ClearSetupEnd(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return ;
	}

	__USBC_Dev_ep0_ClearSetupEnd(usbc_otg->base_addr);
}


static __s32 __USBC_Dev_WriteDataHalf(__u32 usbc_base_addr, __u32 ep_type)
{
	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		__USBC_Dev_ep0_WriteDataHalf(usbc_base_addr);
		break;

	case USBC_EP_TYPE_TX:
		__USBC_Dev_Tx_WriteDataHalf(usbc_base_addr);
		break;

	case USBC_EP_TYPE_RX:
		//not support
		return -1;
		//break;

	default:
		return -1;
	}

	return 0;
}

static __s32 __USBC_Dev_WriteDataComplete(__u32 usbc_base_addr, __u32 ep_type)
{
	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		__USBC_Dev_ep0_WriteDataComplete(usbc_base_addr);
		break;

	case USBC_EP_TYPE_TX:
		__USBC_Dev_Tx_WriteDataComplete(usbc_base_addr);
		break;

	case USBC_EP_TYPE_RX:
		//not support
		return -1;
		//break;

	default:
		return -1;
	}

	return 0;
}

static __s32 __USBC_Dev_ReadDataHalf(__u32 usbc_base_addr, __u32 ep_type)
{
	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		__USBC_Dev_ep0_ReadDataHalf(usbc_base_addr);
		break;

	case USBC_EP_TYPE_TX:
		//not support
		return -1;
		//break;

	case USBC_EP_TYPE_RX:
		__USBC_Dev_Rx_ReadDataHalf(usbc_base_addr);
		break;

	default:
		return -1;
	}

	return 0;
}

static __s32 __USBC_Dev_ReadDataComplete(__u32 usbc_base_addr, __u32 ep_type)
{
	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		__USBC_Dev_ep0_ReadDataComplete(usbc_base_addr);
		break;

	case USBC_EP_TYPE_TX:
		//not support
		return -1;
		//break;

	case USBC_EP_TYPE_RX:
		__USBC_Dev_Rx_ReadDataComplete(usbc_base_addr);
		break;

	default:
		return -1;
	}

	return 0;
}

/*
 * get the write status, eg write over or not
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 * @complete:   if all data has been written over.
 *
 * return: 0 - success, !0 - failed
 */
__s32 USBC_Dev_WriteDataStatus(__hdle hUSB, __u32 ep_type, __u32 complete)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return -1;
	}

	if (complete) {
		return __USBC_Dev_WriteDataComplete(usbc_otg->base_addr, ep_type);
	} else {
		return __USBC_Dev_WriteDataHalf(usbc_otg->base_addr, ep_type);
	}
}

/*
 * get the read status, eg write over or not
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 * @complete:   if all data has been read over.
 *
 * return: 0 - success, !0 - failed
 */
__s32 USBC_Dev_ReadDataStatus(__hdle hUSB, __u32 ep_type, __u32 complete)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return -1;
	}

	if (complete) {
		return __USBC_Dev_ReadDataComplete(usbc_otg->base_addr, ep_type);
	} else {
		return __USBC_Dev_ReadDataHalf(usbc_otg->base_addr, ep_type);
	}
}

/*
 * check if the data ready for reading
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 *
 */
__u32 USBC_Dev_IsReadDataReady(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return 0;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		return __USBC_Dev_ep0_IsReadDataReady(usbc_otg->base_addr);

	case USBC_EP_TYPE_TX:
		//not support
		break;

	case USBC_EP_TYPE_RX:
		return __USBC_Dev_Rx_IsReadDataReady(usbc_otg->base_addr);

	default:
		break;
	}

	return 0;
}

/*
 * check if the data ready for writting
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 * @ep_type:    ep type
 *
 */
__u32 USBC_Dev_IsWriteDataReady(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return 0;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		return __USBC_Dev_ep0_IsWriteDataReady(usbc_otg->base_addr);

	case USBC_EP_TYPE_TX:
		return __USBC_Dev_Tx_IsWriteDataReady(usbc_otg->base_addr);

	case USBC_EP_TYPE_RX:
		//not support
		break;

	default:
		break;
	}

	return 0;
}

__u32 USBC_Dev_IsWriteDataReady_FifoEmpty(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if (usbc_otg == NULL) {
		return 0;
	}

	switch(ep_type) {
	case USBC_EP_TYPE_EP0:
		return __USBC_Dev_ep0_IsWriteDataReady(usbc_otg->base_addr);

	case USBC_EP_TYPE_TX:
		return __USBC_Dev_Tx_IsWriteDataReady_FifoEmpty(usbc_otg->base_addr);

	case USBC_EP_TYPE_RX:
		//not support
		break;

	default:
		break;
	}

	return 0;
}



/*
 * configure the device's transfer type and speed mode.
 * @hUSB:       handle return by USBC_open_otg, include the key data which USBC need
 *
 */
__s32 USBC_Dev_IsoUpdateEnable(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return -1;
	}

	__USBC_Dev_TsType_Iso(usbc_otg->base_addr);
	return 0;
}

static void __USBC_Dev_ep0_FlushFifo(__u32 usbc_base_addr)
{
	USBC_Writew(1 << USBC_BP_CSR0_D_FLUSH_FIFO, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Dev_Tx_FlushFifo(__u32 usbc_base_addr)
{
	USBC_Writew((1 << USBC_BP_TXCSR_D_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_TXCSR_D_FLUSH_FIFO),
	USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Dev_Rx_FlushFifo(__u32 usbc_base_addr)
{
	USBC_Writew((1 << USBC_BP_RXCSR_D_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_RXCSR_D_FLUSH_FIFO),
	USBC_REG_RXCSR(usbc_base_addr));
}

void USBC_Dev_FlushFifo(__hdle hUSB, __u32 ep_type)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Dev_ep0_FlushFifo(usbc_otg->base_addr);
		break;

		case USBC_EP_TYPE_TX:
			__USBC_Dev_Tx_FlushFifo(usbc_otg->base_addr);
		break;

		case USBC_EP_TYPE_RX:
			__USBC_Dev_Rx_FlushFifo(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

EXPORT_SYMBOL(USBC_Dev_SetAddress_default);
EXPORT_SYMBOL(USBC_Dev_SetAddress);

EXPORT_SYMBOL(USBC_Dev_QueryTransferMode);
EXPORT_SYMBOL(USBC_Dev_ConfigTransferMode);
EXPORT_SYMBOL(USBC_Dev_ConectSwitch);
EXPORT_SYMBOL(USBC_Dev_QueryPowerStatus);

EXPORT_SYMBOL(USBC_Dev_ConfigEp);
EXPORT_SYMBOL(USBC_Dev_ConfigEp_Default);
EXPORT_SYMBOL(USBC_Dev_ConfigEpDma);
EXPORT_SYMBOL(USBC_Dev_ClearEpDma);

EXPORT_SYMBOL(USBC_Dev_IsEpStall);
EXPORT_SYMBOL(USBC_Dev_EpSendStall);
EXPORT_SYMBOL(USBC_Dev_EpClearStall);

EXPORT_SYMBOL(USBC_Dev_Ctrl_IsSetupEnd);
EXPORT_SYMBOL(USBC_Dev_Ctrl_ClearSetupEnd);

EXPORT_SYMBOL(USBC_Dev_IsReadDataReady);
EXPORT_SYMBOL(USBC_Dev_IsWriteDataReady);
EXPORT_SYMBOL(USBC_Dev_WriteDataStatus);
EXPORT_SYMBOL(USBC_Dev_ReadDataStatus);

EXPORT_SYMBOL(USBC_Dev_IsoUpdateEnable);
EXPORT_SYMBOL(USBC_Dev_FlushFifo);
