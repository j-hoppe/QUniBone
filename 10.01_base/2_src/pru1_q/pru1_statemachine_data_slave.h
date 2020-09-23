

/* pru1_statemachine_slave.h: state machine for execution of slave DATO* or DATI* cycles

 Copyright (c) 2018-2019, Joerg Hoppe
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
#ifndef _PRU1_STATEMACHINE_DATA_SLAVE_H_
#define _PRU1_STATEMACHINE_DATA_SLAVE_H_
#include <stdint.h>
#include "pru1_utils.h"	// statemachine_state_func

// states
enum sm_data_slave_states_enum {
    sm_data_slave_state_stop = 0, // all work done
    sm_data_slave_state_start,
    sm_data_slave_state_dindout_start,
    sm_data_slave_state_din_complete,
    sm_data_slave_state_dout_complete,
    sm_data_slave_state_din_end,
    sm_data_slave_state_dout_end
} ;

// Transfers a block of worst as data cycles
typedef struct {
    enum sm_data_slave_states_enum state;
    uint16_t        val;                            // prefetched memory content
    uint32_t        addr;                           // latched address
} statemachine_data_slave_t;


extern statemachine_data_slave_t sm_data_slave;
enum sm_data_slave_states_enum  sm_data_slave_func(enum sm_data_slave_states_enum  /**/ state) ;
#endif

