/* pru1_timeouts.h:  timeout conditions

 Copyright (c) 2019-2020, Joerg Hoppe
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
 
 22-aug-2020	 JH  new version for QUniBone
 
 Several timers are needed, but PRU hs only one
 global cycle counter CYCLEOUNT. It is 32 bit and runs at 200 MHz,
 so rollaround every 21 seconds.
 
 Usage and limitations:
 the cycle counter will stop at 0xffffffff and is polled for restart
 so timeouts are longer than defined
 _reached() may only be queried after _set()
 _reached() may not called again after result is "true" for first time
 _ max delay is 2^31*5ns = ca. 10 secs
 
 */

#ifndef _PRU1_TIMEOUTS_H_
#define _PRU1_TIMEOUTS_H_

#include <stdint.h>
#include <stdbool.h>
#include <pru_cfg.h>
#include <pru_ctrl.h>

//#include "pru1_utils.h"


typedef struct {
	uint32_t target_cycles;	// must be reached for timeout
	// roll_over: PRU cycle counter must roll over 0xffffffff to reach target_time
	bool roll_over;
} timeout_controlblock_t;

// predefined timeouts
#define TIMEOUT_COUNT	3

// fixed indices
#define TIMEOUT_DMA	0
#define TIMEOUT_SACK 	1
#define TIMEOUT_TEST 	2

// cycle end count for each active timeout.
extern timeout_controlblock_t timeout_controlblock[TIMEOUT_COUNT];

// SET and TEST as macros
#define TIMEOUT_SET(idx,delay_ticks)	do {			\
	uint32_t _cycles = PRU1_CTRL.CYCLE + 1;				\
	if (_cycles == 0 ) {									\
			/* 0xffffffff: restart circular 32 bit cycle counter */ \
			PRU1_CTRL.CYCLE = 0 ;						\
			PRU1_CTRL.CTRL_bit.CTR_EN = 1;				\
	}													\
	timeout_controlblock[idx].target_cycles = _cycles + (delay_ticks) ; /* may roll over */ \
	timeout_controlblock[idx].roll_over = (timeout_controlblock[idx].target_cycles < _cycles) ;	\
	} while(0)

// check if timed-out. result in var "result"	
#define TIMEOUT_REACHED(idx,result)	do {					\
        uint32_t _cycles = PRU1_CTRL.CYCLE + 1 ;			\
        if (_cycles == 0) {									\
                /* 0xffffffff: restart circular 32 bit cycle counter */ \
                PRU1_CTRL.CYCLE = 0 ;						\
                PRU1_CTRL.CTRL_bit.CTR_EN = 1;				\
        }													\
        if (timeout_controlblock[idx].roll_over && _cycles > 0x80000000) \
                /* cycle count not yet wrapped around to "near-zero" */ \
                (result) = false ;							\
        else  /* fast path */									\
    	    (result) = ( _cycles >= timeout_controlblock[idx].target_cycles) ; \
} while(0)

void timeout_init(void);
void timeout_test(void);



#endif
