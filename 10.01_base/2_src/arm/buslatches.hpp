/* buslatches.hpp: PRU GPIO multiplier latches on QUniBone PCB-

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


 16-jul-2020  JH      refactored from gpio.hpp
 */

#ifndef _BUSLATCHES_H_
#define _BUSLATCHES_H_

#include <stdint.h>
#include <vector>
#include <string>
#include <assert.h>

#include "bitcalc.h"

// raw 1 bit signal traces
typedef struct {
	unsigned reg_sel;
	unsigned bit_nr;
	unsigned is_input; // 0 = "74LVTH541 <- DS8641" ; 1 = "74LS377 -> DS8641"
	unsigned properties; // 0=normal QUNIBUS, 1= inverted (only UNIBUS BG*_OUT),
	// 					2 = latched DATASYNClatch, 4 = function
	const char *qunibus_name; // UNIBUS/QBUS signal name
	const char *path; // long info with net list
} buslatches_wire_info_t;

extern buslatches_wire_info_t buslatches_wire_info[];

buslatches_wire_info_t *buslatches_wire_info_get(const char * unibus_name, unsigned is_input);

#define BUSLATCHES_COUNT	8

// save current state uf gpios and registers,
// to suppress redundant write accesses
class buslatch_c {
public:
	unsigned addr; // address of latch
	unsigned bitmask; // mask with valid bits
	unsigned rw_bitmask; // mask with persistent read/write bits

	uint8_t outreg_val; // output value of each register, for test functions

	bool read_inverted; // true:  read back inverted with respect to write levels
	unsigned cur_reg_val; // content of output latches

	buslatch_c(unsigned addr, unsigned bitmask) {
		// create with defaults
		this->addr = addr;
		this->bitmask = this->rw_bitmask = bitmask;
		this->read_inverted = false;
	}
	// PRU handles special DAL/ADDR demux logic
	void setval(unsigned bitmask, unsigned val);
	unsigned getval();

};

typedef buslatch_c *buslatch_ptr;

class buslatches_c: public std::vector<buslatch_ptr> {
public:
	void setup(); // delayed constructor
	// current signal state, used for optimization
	unsigned cur_output_enable; // state of ENABLE
	unsigned cur_reg_sel; // state of SEL A0,A1,A2
	void pru_reset(void);
	void output_enable(bool enable);
	bool get_pin_val(buslatches_wire_info_t *wi);

	// write single signal wire
	void set_pin_val(buslatches_wire_info_t *wi, unsigned val);

	void exerciser_random_order();

	void test_simple_pattern(unsigned pattern, buslatch_c *bl);
	void test_simple_pattern_multi(unsigned pattern, bool stop_on_error);

	void test_timing(uint8_t addr_0_7, uint8_t addr_8_15, uint8_t data_0_7, uint8_t data_8_15);

};

extern buslatches_c buslatches;

#endif

