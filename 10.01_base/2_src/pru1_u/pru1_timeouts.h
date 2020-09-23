/* pru1_timeouts.h:  timeout conditions

 Copyright (c) 2019, Joerg Hoppe
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

 3-jul-2019	JH	begin edit
 */
#ifndef _PRU1_TIMEOUTS_H_
#define _PRU1_TIMEOUTS_H_

#include <stdint.h>
#include <stdbool.h>

// predefined timeouts
#define TIMEOUT_COUNT	3

// fixed pointers
#define TIMEOUT_DMA	(&timeout_target_cycles[0])
#define TIMEOUT_SACK 	(&timeout_target_cycles[1])
//#define TIMEOUT_TEST 	(&timeout_target_cycles[2])

// cycle end count for each active timeoput.
extern uint32_t timeout_target_cycles[TIMEOUT_COUNT];

// call all functions mit timeout_func(TIMEOUT_*,..)
// This allows the compiler to optimize the timeout_target_cycles[idx] expr

void timeout_init(void);
void timeout_set(uint32_t *target_cycles_var, uint32_t delta_cycles);
bool timeout_active(uint32_t *target_cycles_var) ;
bool timeout_reached(uint32_t *target_cycles_var);
void timeout_cleanup(uint32_t *target_cycles_var);

#endif
