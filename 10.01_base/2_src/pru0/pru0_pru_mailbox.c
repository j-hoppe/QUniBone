/* pru0_pru_mailbox.c: datastructures common to PRU0 and PRU1 (C solution)

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

  datastructures common to PRU0 and PRU1
  Compiled in both PR0 and PRU1 code
  To be included into both PRU0 and PRU1 projects.
  Section ".pru_pru_mailbox_sec" to be placed at the end of PRU0 internal RAM.
  pos in PRU0 space: 0x1f00
  pos in PRU1 space: 0x3f00
  See linker control file!

  OBSOLETE: asm program with XFR as mailbox is used.
 */
#include <stdint.h>

#include "pru_pru_mailbox.h"
#define _PRU_PRU_MAILBOX_C_


// everything here is to be placed in the same section
#pragma SET_DATA_SECTION(".pru_pru_mailbox_sec")

volatile pru_pru_mailbox_t pru_pru_mailbox ;
