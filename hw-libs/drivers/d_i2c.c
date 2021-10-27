/*
MIT LICENSE

Copyright 2014-2019 Inertial Sense, Inc. - http://inertialsense.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT, IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#include <asf.h>
#include <string.h>
#include <stdio.h>

#include "d_i2c.h"

#ifdef __INERTIAL_SENSE_EVB_2__
i2c_t sn_i2c = {.instance_id = ID_TWIHS0, .instance = TWIHS0};
#else 
i2c_t sn_i2c = {.instance_id = ID_TWIHS1, .instance = TWIHS1};
#endif

//Configuration for TWI instance
#define TWI_CLK			400000

#ifndef INT_PRIORITY_DMA
#define INT_PRIORITY_DMA	3
#endif

#define INT_PRIORITY_I2C	INT_PRIORITY_DMA

static const uint32_t _xdmaint = XDMAC_CIE_BIE; // End of Block Interrupt Enable

static const uint32_t _i2cint = 
	TWIHS_IER_RXRDY |
	TWIHS_IER_TXRDY;

int i2c_master_get_defaults(i2c_t *i2c_init)
{
	i2c_init->cfg.master_clk = sysclk_get_peripheral_hz();
	i2c_init->cfg.speed = TWI_CLK;
	i2c_init->cfg.chip = 0x40;
	i2c_init->cfg.smbus = 0;

#ifdef __INERTIAL_SENSE_EVB_2__
	i2c_init->rx_dma.chan = DMA_CH_EVB_I2C_SENSORS_RX;
	i2c_init->tx_dma.chan = DMA_CH_EVB_I2C_SENSORS_TX;
#else
	i2c_init->rx_dma.chan = DMA_CH_I2C_SENSORS_RX;
	i2c_init->tx_dma.chan = DMA_CH_I2C_SENSORS_TX;
#endif

	i2c_init->rx_dma.xdmac.mbr_sa = (uint32_t)&(i2c_init->instance->TWIHS_RHR);
	i2c_init->rx_dma.xdmac.mbr_cfg =
		XDMAC_CC_TYPE_PER_TRAN |
		XDMAC_CC_MBSIZE_SINGLE |
		XDMAC_CC_DSYNC_PER2MEM |
		XDMAC_CC_MEMSET_NORMAL_MODE |
		XDMAC_CC_CSIZE_CHK_1 |
		XDMAC_CC_DWIDTH_BYTE |
		XDMAC_CC_SIF_AHB_IF1 |
		XDMAC_CC_DIF_AHB_IF0 |
		XDMAC_CC_SAM_FIXED_AM |
		XDMAC_CC_DAM_INCREMENTED_AM;
	i2c_init->rx_dma.xdmac.mbr_ubc = 0;
	
	i2c_init->tx_dma.xdmac.mbr_da = (uint32_t)&(i2c_init->instance->TWIHS_THR);
	i2c_init->tx_dma.xdmac.mbr_sa = (uint32_t)i2c_init->buf;
	i2c_init->tx_dma.xdmac.mbr_cfg =
		XDMAC_CC_TYPE_PER_TRAN |
		XDMAC_CC_MBSIZE_SINGLE |
		XDMAC_CC_DSYNC_MEM2PER |
		XDMAC_CC_MEMSET_NORMAL_MODE |
		XDMAC_CC_CSIZE_CHK_1 |
		XDMAC_CC_DWIDTH_BYTE |
		XDMAC_CC_SIF_AHB_IF0 |
		XDMAC_CC_DIF_AHB_IF1 |
		XDMAC_CC_SAM_INCREMENTED_AM |
		XDMAC_CC_DAM_FIXED_AM;
	i2c_init->tx_dma.xdmac.mbr_ubc = 0;
	
	if(i2c_init->instance == TWIHS0)
	{
		i2c_init->tx_dma.xdmac.mbr_cfg |= XDMAC_CC_PERID(XDMAC_PERID_TWIHS0_TX);
		i2c_init->rx_dma.xdmac.mbr_cfg |= XDMAC_CC_PERID(XDMAC_PERID_TWIHS0_RX);
	}
	else if(i2c_init->instance == TWIHS1)
	{
		i2c_init->tx_dma.xdmac.mbr_cfg |= XDMAC_CC_PERID(XDMAC_PERID_TWIHS1_TX);
		i2c_init->rx_dma.xdmac.mbr_cfg |= XDMAC_CC_PERID(XDMAC_PERID_TWIHS1_RX);
	}
	else if(i2c_init->instance == TWIHS2)
	{
		i2c_init->tx_dma.xdmac.mbr_cfg |= XDMAC_CC_PERID(XDMAC_PERID_TWIHS2_TX);
		i2c_init->rx_dma.xdmac.mbr_cfg |= XDMAC_CC_PERID(XDMAC_PERID_TWIHS2_RX);
	}

	i2c_init->len = 0;

	i2c_init->tx_status = I2C_TXSTATUS_IDLE;
	i2c_init->rx_status = I2C_RXSTATUS_IDLE;

	for(int i = 0; i < I2C_NUM_DEVICES; i++)
	{
		i2c_init->device[i].rx_len = 0;
		i2c_init->device[i].tx_len = 0;
		i2c_init->device[i].addr = 0x40;
	}

	for(int i = 0; i < I2C_NUM_DEVICES; i++)
	{
		i2c_init->device[i].addr = 0;
	}

	return STATUS_OK;
}

static void i2c_master_dma_init_rx(i2c_t *i2c_init)
{
	xdmac_configure_transfer(XDMAC, i2c_init->rx_dma.chan, &i2c_init->rx_dma.xdmac);

	xdmac_channel_set_descriptor_control(XDMAC, i2c_init->rx_dma.chan, 0);

	xdmac_enable_interrupt(XDMAC, i2c_init->rx_dma.chan);
	xdmac_channel_enable_interrupt(XDMAC, i2c_init->rx_dma.chan, _xdmaint);
	
	NVIC_DisableIRQ(XDMAC_IRQn);
	NVIC_ClearPendingIRQ(XDMAC_IRQn);
	NVIC_SetPriority(XDMAC_IRQn, INT_PRIORITY_DMA);
	NVIC_EnableIRQ(XDMAC_IRQn);
}

static void i2c_master_dma_config_rx(i2c_t *i2c_init, uint8_t *buf, uint32_t len)
{
	i2c_init->rx_dma.xdmac.mbr_da = (uint32_t)buf;
	i2c_init->rx_dma.xdmac.mbr_ubc = len;
	
	xdmac_configure_transfer(XDMAC, i2c_init->rx_dma.chan, &i2c_init->rx_dma.xdmac);
}

static void i2c_master_dma_start_rx(i2c_t *i2c_init)
{
	xdmac_channel_enable_no_cache(XDMAC, i2c_init->rx_dma.chan);
}

static void i2c_master_dma_stop_rx(i2c_t *i2c_init)
{
	xdmac_channel_disable(XDMAC, i2c_init->rx_dma.chan);
	
	SCB_InvalidateDCache_by_Addr((uint32_t *)i2c_init->buf, I2C_BUF_SIZE/4);	// Round up
}

static void i2c_master_dma_init_tx(i2c_t *i2c_init)
{
	xdmac_configure_transfer(XDMAC, i2c_init->tx_dma.chan, &i2c_init->tx_dma.xdmac);
	
	xdmac_channel_set_descriptor_control(XDMAC, i2c_init->tx_dma.chan, 0);

	xdmac_enable_interrupt(XDMAC, i2c_init->tx_dma.chan);
	xdmac_channel_enable_interrupt(XDMAC, i2c_init->tx_dma.chan, _xdmaint);
	NVIC_DisableIRQ(XDMAC_IRQn);
	NVIC_ClearPendingIRQ(XDMAC_IRQn);
	NVIC_SetPriority(XDMAC_IRQn, INT_PRIORITY_DMA);
	NVIC_EnableIRQ(XDMAC_IRQn);
}

static void i2c_master_dma_config_tx(i2c_t *i2c_init, uint8_t *buf, uint32_t len)
{
	if(len > (I2C_BUF_SIZE - 1))
		len = (I2C_BUF_SIZE - 1);
	
	i2c_init->tx_dma.xdmac.mbr_sa = buf;
	i2c_init->tx_dma.xdmac.mbr_ubc = len;

	xdmac_configure_transfer(XDMAC, i2c_init->tx_dma.chan, &i2c_init->tx_dma.xdmac);
}

static void i2c_master_dma_start_tx(i2c_t *i2c_init)
{
	SCB_CleanDCache_by_Addr((uint32_t *)i2c_init->buf, I2C_BUF_SIZE/4);
	xdmac_channel_enable_no_cache(XDMAC, i2c_init->tx_dma.chan);
}

static void i2c_master_dma_stop_tx(i2c_t *i2c_init)
{
	xdmac_channel_disable(XDMAC, i2c_init->tx_dma.chan);
}

/*
 *	Make sure to call i2c_get_defaults() first
 */
int i2c_master_init(i2c_t *i2c_init)	// TODO: Rewrite this so it can be used for any instance of I2C
{	
	// Configure pins (done in common location on uINS-3)
	// TODO: Move out of this function and into EVB-2 project
#ifdef __INERTIAL_SENSE_EVB_2__
	ioport_set_pin_peripheral_mode(I2C_0_SDA_PIN, I2C_0_SDA_FLAGS);
	ioport_set_pin_peripheral_mode(I2C_0_SCL_PIN, I2C_0_SCL_FLAGS);
#endif

	sysclk_enable_peripheral_clock(i2c_init->instance_id);
	
	i2c_init->instance->TWIHS_CR |= TWIHS_CR_SWRST;
	
	if(twihs_master_init(i2c_init->instance, &i2c_init->cfg) != TWIHS_SUCCESS)
		return -1;

	if(i2c_init->instance == TWIHS0)
	{
		NVIC_DisableIRQ(TWIHS0_IRQn);
		NVIC_ClearPendingIRQ(TWIHS0_IRQn);
		NVIC_SetPriority(TWIHS0_IRQn, INT_PRIORITY_I2C);
		NVIC_EnableIRQ(TWIHS0_IRQn);
	}
	else if(i2c_init->instance == TWIHS1)
	{
		NVIC_DisableIRQ(TWIHS1_IRQn);
		NVIC_ClearPendingIRQ(TWIHS1_IRQn);
		NVIC_SetPriority(TWIHS1_IRQn, INT_PRIORITY_I2C);
		NVIC_EnableIRQ(TWIHS1_IRQn);
	}
	else if(i2c_init->instance == TWIHS2)
	{
		NVIC_DisableIRQ(TWIHS2_IRQn);
		NVIC_ClearPendingIRQ(TWIHS2_IRQn);
		NVIC_SetPriority(TWIHS2_IRQn, INT_PRIORITY_I2C);
		NVIC_EnableIRQ(TWIHS2_IRQn);
	}
	
	i2c_master_dma_init_rx(i2c_init);
	i2c_master_dma_init_tx(i2c_init);
	
	twihs_enable_interrupt(i2c_init->instance, TWIHS_IER_ARBLST);		// Arbitration lost interrupt enable
	
	return 0;
}

static bool i2c_master_is_busy(i2c_t *i2c_init)
{
	if(i2c_init->len > 0)
	{
		return true;
	}

	// If there is stuff in the queue, it will already have been serviced by the interrupt handler. No need to check here.

	return false;
}

static bool i2c_master_is_write_buf_full(i2c_t *i2c_init, uint8_t dev)
{
	if(i2c_init->device[dev].tx_len > 0)
	{
		return true;
	}

	return false;
}

int i2c_master_write(i2c_t *i2c_init, uint8_t dev, uint8_t *buf, uint8_t len)
{
	i2c_device_t *device = &i2c_init->device[dev];

	/* Check argument */
	if (len == 0 || device->addr == 0) {
		return -1;
	}

	if(i2c_master_is_busy(i2c_init))
	{
		if(i2c_master_is_write_buf_full(i2c_init, dev))
		{
			return -1;
		}
	
		// Add to queue
		device->tx_len = len;
		memcpy(device->tx_buf, buf, len);
	}
	else	// Start the write immediately 
	{
		if(!(i2c_init->instance->TWIHS_SR & TWIHS_SR_TXRDY))
		{
			return -1;
		}

		i2c_init->len = len;		// Do this as soon as possible to "lock" the peripheral

		i2c_init->instance->TWIHS_MMR = TWIHS_MMR_DADR(device->addr);

		// Bypass DMA for 1 byte transmissions
		if (len == 1) {
			i2c_init->instance->TWIHS_THR = buf[0];
			i2c_init->instance->TWIHS_CR = TWIHS_CR_STOP;
			i2c_init->len = 0;
			return 0;
		}
		
		memcpy((uint8_t*)i2c_init->buf, buf, len);

		i2c_init->tx_status = I2C_TXSTATUS_TRANSMIT_DMA;

		i2c_master_dma_config_tx(i2c_init, (uint8_t*)i2c_init->buf, i2c_init->len-1);	// Datasheet says we have to transmit last byte manually
		i2c_master_dma_start_tx(i2c_init);
	}

	return 0;
}

/* Writes the next chunk of data from the queue, low numbered devices have priority */
static int i2c_master_write_from_queue(i2c_t *i2c_init)
{
	if(i2c_master_is_busy(i2c_init))
	{
		return -1;
	}

	for(int i = 0; i < I2C_NUM_DEVICES; i++)
	{
		if(i2c_master_is_write_buf_full(i2c_init, i))
		{
			i2c_device_t *device = &i2c_init->device[i];

			if(!(i2c_init->instance->TWIHS_SR & TWIHS_SR_TXRDY))
			{
				return -1;
			}

			// Bypass DMA for 1 byte transmissions
			if (device->tx_len == 1) {
				i2c_init->instance->TWIHS_THR = device->tx_buf[0];				
				i2c_init->instance->TWIHS_CR = TWIHS_CR_STOP;
				device->tx_len = 0;
				return 0;
			}

			i2c_init->len = device->tx_len;				// Do this as soon as possible to "lock" the peripheral		
			device->tx_len = 0;		// Clear the queue

			i2c_init->instance->TWIHS_MMR = TWIHS_MMR_DADR(device->addr);		// Grab the address from the device struct, should be there from init
			
			memcpy((uint8_t*)i2c_init->buf, device->tx_buf, i2c_init->len);

			i2c_master_dma_config_tx(i2c_init, (uint8_t*)i2c_init->buf, i2c_init->len - 1);	// Datasheet says we have to transmit last byte manually
			i2c_master_dma_start_tx(i2c_init);

			return 0;
		}
	}
}

static bool i2c_master_is_read_buf_full(i2c_t *i2c_init, uint8_t dev)
{
	if(i2c_init->device[dev].rx_len > 0)
	{
		return true;
	}

	return false;
}

int i2c_master_read(i2c_t *i2c_init, uint8_t dev, uint8_t *buf, uint8_t len)
{	
	i2c_device_t *device = &i2c_init->device[dev];

	/* Check argument */
	if (len == 0 || device->addr == 0) {
		return -1;
	}
	
	if(i2c_master_is_busy(i2c_init))
	{
		if(i2c_master_is_read_buf_full(i2c_init, dev))
		{
			return -1;
		}
	
		// Add to queue
		device->rx_len = len;
		device->rx_buf_dest = buf;
	}
	else	// Start the write immediately 
	{
		i2c_init->len = len;		// Do this as soon as possible to "lock" the peripheral
		i2c_init->rx_buf_dest = buf;

		i2c_init->instance->TWIHS_MMR = TWIHS_MMR_DADR(device->addr) | TWIHS_MMR_MREAD;

		// TODO: Bypass DMA for 1 (maybe 2) byte writes. Uncommon but could happen.

		i2c_master_dma_config_rx(i2c_init, (uint8_t*)i2c_init->buf, len-2);			// Datasheet says we have to read last 2 bytes manually
		
		i2c_init->rx_status = I2C_RXSTATUS_READ_DMA;
	
		i2c_master_dma_start_rx(i2c_init);
		
		i2c_init->instance->TWIHS_CR |= TWIHS_CR_START;
	}

	return 0;
}

/* Writes the next chunk of data from the queue, low numbered devices have priority */
static int i2c_master_read_from_queue(i2c_t *i2c_init)
{
	if(i2c_master_is_busy(i2c_init))
	{
		return -1;
	}

	for(int i = 0; i < I2C_NUM_DEVICES; i++)
	{
		if(i2c_master_is_read_buf_full(i2c_init, i))
		{
			i2c_device_t *device = &i2c_init->device[i];

			i2c_init->instance->TWIHS_MMR = TWIHS_MMR_DADR(device->addr) | TWIHS_MMR_MREAD;		// Grab the address from the device struct, should be there from init

			// TODO: Bypass DMA for 1 (maybe 2) byte writes. Uncommon but could happen.
			
			i2c_init->rx_buf_dest = device->rx_buf_dest;
			i2c_init->len = device->rx_len;
			device->rx_len = 0;		// Clear the queue

			i2c_master_dma_config_rx(i2c_init, (uint8_t*)i2c_init->buf, i2c_init->len - 2);			// Datasheet says we have to read last 2 bytes manually
			
			i2c_init->rx_status = I2C_RXSTATUS_READ_DMA;
		
			i2c_master_dma_start_rx(i2c_init);
			
			i2c_init->instance->TWIHS_CR |= TWIHS_CR_START;

			return 0;
		}
	}
	
}

static void TWIHS_Handler(i2c_t *i2c_init)
{
	uint32_t status = i2c_init->instance->TWIHS_SR;
	if(status & TWIHS_SR_ARBLST)
	{
		
	}
	
	if(status & TWIHS_SR_TXRDY && i2c_init->tx_status != I2C_TXSTATUS_IDLE)
	{
		twihs_disable_interrupt(i2c_init->instance, TWIHS_IDR_TXRDY);	// Without this we get spammed by the interrupt
		
		if(i2c_init->tx_status == I2C_TXSTATUS_TRANSMIT_DMA_WAIT_TXRDY)
		{
			i2c_init->instance->TWIHS_CR |= TWIHS_CR_STOP;
			i2c_init->instance->TWIHS_THR = i2c_init->buf[i2c_init->len - 1];	// Transmit the last byte

			i2c_init->len = 0;		// Indicate we're done
			i2c_init->tx_status = I2C_TXSTATUS_IDLE;
		}
	}
	
	if(status & TWIHS_SR_RXRDY)
	{
		if(i2c_init->rx_status == I2C_RXSTATUS_READ_DMA_WAIT_RXRDY)
		{
			i2c_init->instance->TWIHS_CR |= TWIHS_CR_STOP;
			
			i2c_init->buf[i2c_init->len - 2] = i2c_init->instance->TWIHS_RHR;
			
			i2c_init->rx_status = I2C_RXSTATUS_READ_PENULTIMATE;
		}
		else if(i2c_init->rx_status == I2C_RXSTATUS_READ_PENULTIMATE)
		{
			i2c_init->buf[i2c_init->len - 1] = i2c_init->instance->TWIHS_RHR;
			
			twihs_disable_interrupt(i2c_init->instance, TWIHS_IDR_RXRDY);
			
			memcpy(i2c_init->rx_buf_dest, i2c_init->buf, i2c_init->len);	
			i2c_init->len = 0;		// Indicate we're done
			i2c_init->rx_status = I2C_RXSTATUS_IDLE;
		}
	}

	if(i2c_init->rx_status == I2C_RXSTATUS_IDLE && i2c_init->tx_status == I2C_RXSTATUS_IDLE)
	{
		i2c_master_write_from_queue(i2c_init);
		i2c_master_read_from_queue(i2c_init);
	}

}

#ifdef __INERTIAL_SENSE_EVB_2__
void TWIHS0_Handler(void)
{
	TWIHS_Handler(&sn_i2c);
}
#else
void TWIHS1_Handler(void)
{
	TWIHS_Handler(&sn_i2c);
}
#endif

/*
 *	Sub-handler that is called from the XDMAC_Handler() in d_dma.c
 */
void XDMAC_i2c_Handler(void)
{
	uint32_t status = XDMAC->XDMAC_CHID[sn_i2c.rx_dma.chan].XDMAC_CIS;
	
	if((status & XDMAC_CIS_BIS) && (sn_i2c.rx_status == I2C_RXSTATUS_READ_DMA))
	{
		//DBGPIO_START(DBG_MISC_DEBUG_PIN);
		i2c_master_dma_stop_rx(&sn_i2c);
		//DBGPIO_END(DBG_MISC_DEBUG_PIN);
		sn_i2c.rx_status = I2C_RXSTATUS_READ_DMA_WAIT_RXRDY;
		twihs_enable_interrupt(sn_i2c.instance, TWIHS_IER_RXRDY);
	}
	
	status = XDMAC->XDMAC_CHID[sn_i2c.tx_dma.chan].XDMAC_CIS;
	
	if((status & XDMAC_CIS_BIS) && (sn_i2c.tx_status == I2C_TXSTATUS_TRANSMIT_DMA))
	{
		i2c_master_dma_stop_tx(&sn_i2c);
		
		sn_i2c.tx_status = I2C_TXSTATUS_TRANSMIT_DMA_WAIT_TXRDY;
		twihs_enable_interrupt(sn_i2c.instance, TWIHS_IER_TXRDY);
	}
}
