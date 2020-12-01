/* pru1_statemachine_dma.h: state machine for bus master DMA

 Copyright (c) 2018, Joerg Hoppe
 j_hoppe@t-online.de, www.retrocmp.com

 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 JOERG HOPPE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


 29-jun-2019	JH		rework: state returns ptr to next state func
 12-nov-2018  JH      entered beta phase
 */

#ifndef  _PRU1_STATEMACHINE_DMA_H_
#define  _PRU1_STATEMACHINE_DMA_H_

#include "pru1_utils.h"	// statemachine_state_func

// Transfers a block of worst as data cycles
typedef struct {
	uint8_t state_timeout; // timeout occured?
	uint16_t *dataptr; // points to current word in mailbox.words[] ;
	uint16_t words_left; // # of words left to transfer
	uint32_t block_end_addr	; // last address of a DATBI/DATBO transfer.
//	uint16_t block_words_left ; // # of words left to transfer in DATBI/BO block
	statemachine_state_func block_data_state_func ;
	bool 	first_data_portion ; // signal to DIN/DOUT: remove ADDR,BS7, from DAL
} statemachine_dma_t;

extern statemachine_dma_t sm_dma;

statemachine_state_func sm_dma_start(void);

#endif
