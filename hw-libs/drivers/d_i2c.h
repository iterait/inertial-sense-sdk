/*
MIT LICENSE

Copyright 2014-2019 Inertial Sense, Inc. - http://inertialsense.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT, IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef D_I2C_H_
#define D_I2C_H_

#include "compiler.h"
#include "d_dma.h"
#include "conf_d_i2c.h"
#include "twihs.h"
#include "debug_gpio.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_BUF_SIZE	64	// MUST be a multiple of 4 (8-bit writes have to fit into 32-bit blocks for cache clear/invalidate to work)

enum
{
	I2C_TXSTATUS_IDLE	= 0,
	I2C_TXSTATUS_TRANSMIT_DMA,
	I2C_TXSTATUS_TRANSMIT_DMA_WAIT_TXRDY,
	I2C_TXSTATUS_ERROR,	
};

enum 
{	
	I2C_RXSTATUS_IDLE = 0,
	I2C_RXSTATUS_READ_DMA,
	I2C_RXSTATUS_READ_DMA_WAIT_RXRDY,
	I2C_RXSTATUS_READ_PENULTIMATE,
	I2C_RXSTATUS_READ_LAST,
	I2C_RXSTATUS_ERROR,	
};

typedef struct
{
	uint8_t 				addr;			// The current device we're communicating with - helps identify rx
	
	uint8_t					tx_buf[I2C_BUF_SIZE];	// Queue buffer
	uint8_t 				tx_len;

	uint8_t					*rx_buf_dest;	// Destination for data to be copied to after read
	uint8_t					rx_len;
} i2c_device_t;


typedef struct
{
	Twihs 					*instance;
	uint32_t				instance_id;
	twihs_options_t 		cfg;

	dma_channel_config_t 	rx_dma;
	dma_channel_config_t	tx_dma;
	
	volatile uint8_t		tx_status;
	volatile uint8_t		rx_status;

	i2c_device_t			device[I2C_NUM_DEVICES];		// Queue for each I2C slave on the bus
	
	uint8_t					buf[I2C_BUF_SIZE] __attribute__((aligned(32)));		// Common RX/TX buffer
	uint8_t 				len;	
	
	uint8_t					*rx_buf_dest;	// Destination for data to be copied to after read
} i2c_t;

extern i2c_t sn_i2c;

int i2c_master_get_defaults(i2c_t *init);
int i2c_master_init(i2c_t *init);

int i2c_master_write(i2c_t *init, uint8_t dev, uint8_t *buf, uint8_t len);
int i2c_master_read(i2c_t *init, uint8_t dev, uint8_t *buf, uint8_t len);

uint8_t i2c_get_status(i2c_t *init);

void XDMAC_i2c_Handler(void);

#ifdef __cplusplus
}
#endif

#endif  // D_I2C_H_
