/* pru_pru_mailbox.h: structure for data exchange between PRU0 and PRU1

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


 12-nov-2018  JH      entered beta phase
 */

#ifndef _PRU_PRU_MAILBOX_H_
#define _PRU_PRU_MAILBOX_H_

#include <stdint.h>

typedef struct {
	// just the state of PRU0 R30 output.
	// PRU1 writes the value
	// PRU0 monitors and copies to its local R30 = REG_DATA_OUT pins
	uint32_t xxx_pru0_r30;

} pru_pru_mailbox_t;

#ifndef _PRU_PRU_MAILBOX_C_
extern volatile pru_pru_mailbox_t pru_pru_mailbox;
#endif

#endif
