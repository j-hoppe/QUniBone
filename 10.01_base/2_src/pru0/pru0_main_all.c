/* pru0_main.c: endless loop, writes outputs on R30 from mailbox (C solution)

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

  from d:\RetroCmp\dec\pdp11\UniBone\91_3rd_party\pru-c-compile\pru-software-support-package\examples\am335x\PRU_gpioToggle\


 */

#include <stdint.h>
#include <pru_cfg.h>
#include "resource_table_empty.h"

#include "pru_pru_mailbox.h"

volatile register uint32_t __R30;
volatile register uint32_t __R31;

void main(void) {

	/* Clear SYSCFG[STANDBY_INIT] to enable OCP master port */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	// loop forever
	void pru0_dataout(void) ;
	pru0_dataout() ;
#ifdef USED
	// old code using shared RAM mailbox,  not reached
	while(1) {
		__R30 = pru_pru_mailbox.pru0_r30 ;
	}
#endif

}

