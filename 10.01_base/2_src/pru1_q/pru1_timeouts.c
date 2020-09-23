/* pru1_timeouts.c:  timeout conditions

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

 22-aug-2020	JH	new version for QUniBone

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
#include <stdint.h>
#include <string.h>

#include "pru1_utils.h"
#include "pru1_timeouts.h"	// own

// cycle end count for each active timeout.
timeout_controlblock_t timeout_controlblock[TIMEOUT_COUNT];

void timeout_init(void) {
	// "clear all" not necessary: calling "reached()" before "set()" is an error anyhow. 
//memset(timeout_controlblock, 0, sizeof(timeout_controlblock_t) * TIMEOUT_COUNT);

	// make sure cycle counter starts running toward 0xffffffff 
	PRU1_CTRL.CTRL_bit.CTR_EN = 1;
}

// test: wait for 1 usec, pulse debug pin for timing measurements
void	timeout_test() {
//	PRU_DEBUG_PIN0(1) ; // signal:start of "set"

	__delay_cycles(MICROSECS(1000)) ; // 1/10 interval display pause

	// logic analyzer: sample with 20kHz und 2M buffer 
	// -> 100 seconds -> 10 roll-arounds of PRU1 cycle counter
	TIMEOUT_SET(TIMEOUT_TEST, MICROSECS(10000));// 10 msec / level
	PRU_DEBUG_PIN0(1) ; // signal:end of "set"

	bool timeout_reached ;
	PRU_DEBUG_PIN0(1) ; // signal:start of delay
	TIMEOUT_REACHED(TIMEOUT_TEST,timeout_reached) ;
	while (!timeout_reached) 
		TIMEOUT_REACHED(TIMEOUT_TEST,timeout_reached) ;
	PRU_DEBUG_PIN0(0) ; // signal: end of delay
	

}

