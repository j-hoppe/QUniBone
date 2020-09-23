/* pru1_ddrmem.c: Control the shared DDR RAM - used for UNIBUS memory

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

#include <stdint.h>
#include <string.h>

#include "mailbox.h"
#include "ddrmem.h"

// fill whole memory with pattern
// called by mailbox command ARM2PRU_REQUEST_DDR_FILL_PATTERN
void ddrmem_fill_pattern(void) {
	unsigned n;
	volatile uint16_t *wordaddr = mailbox.ddrmem_base_physical->memory.words;
	for (n = 0; n < QUNIBUS_MAX_WORDCOUNT; n++)
		*wordaddr++ = n;
}
