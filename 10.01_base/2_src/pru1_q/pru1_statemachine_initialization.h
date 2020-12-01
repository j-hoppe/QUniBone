/* pru1_statemachine_initialization.h: state machine for POK/DOK/INIT

 Copyright (c) 2020, Joerg Hoppe
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

 12-nov-2020  JH      begin

*/ 
#ifndef _PRU1_STATEMACHINE_INITIALIZATION_H_
#define _PRU1_STATEMACHINE_INITIALIZATION_H_

// states (switch() test order)
enum sm_initialization_states_enum {
    state_initialization_idle, //
    state_initialization_init_asserted, // wait for EVENT_ACK
    state_initialization_init_negated // wait for EVENT_ACK
} ;


// a priority-arbitration-worker returns a bit mask with the GRANT signal he recognized

typedef struct {
    enum sm_initialization_states_enum state;
	// reg5 signals as sampled
	uint8_t bussignals_cur ;
} statemachine_initialization_t ;

void sm_initialization_reset(void) ;
void sm_initialization_func(void) ;


#endif


