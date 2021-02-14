/* cpu.hpp: PDP-11/05 CPU

 Copyright (c) 2018, Angelo Papenhoff, Joerg Hoppe
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


 23-nov-2018  JH      created
 */
#ifndef _CPU_HPP_
#define _CPU_HPP_

using namespace std;

#include "utils.hpp"
//#include "qunibusadapter.hpp"
//#include "qunibusdevice.hpp"
#include "unibuscpu.hpp"
#include "qunibus_trigger.hpp"
#include "cpu20/11.h"
#include "cpu20/ka11.h"

class cpu_c: public unibuscpu_c {
private:

	//qunibusdevice_register_t *switch_reg;
	//qunibusdevice_register_t *display_reg;

public:

	cpu_c();
	~cpu_c();

	bool on_before_install(void) override ;
	void on_after_uninstall(void) override ;

	// used for DATI/DATO, operated by qunibusadapter
	dma_request_c data_transfer_request = dma_request_c(this);

	bool on_param_changed(parameter_c *param) override;  // must implement

	parameter_bool_c runmode = parameter_bool_c(this, "run_led", "r",/*readonly*/
	true, "RUN LED: 1 = CPU running, 0 = halted.");
	parameter_bool_c halt_switch = parameter_bool_c(this, "halt_switch", "h",/*readonly*/
	false, "HALT switch: 1 = CPU stopped, 0 = CPU may run.");
	parameter_bool_c continue_switch = parameter_bool_c(this, "continue_switch", "c",/*readonly*/
	false, "CONT action switch: 1 = CPU restart after HALT. CONT+HALT = single step.");
	parameter_bool_c start_switch = parameter_bool_c(this, "start_switch", "s",/*readonly*/
	false, "START action switch: 1 = reset & start CPU from PC. START+HALT: reset.");

	parameter_bool_c direct_memory = parameter_bool_c(this, "pmi", "pmi",/*readonly*/
	false, "Private Memory Interconnect: CPU accesses memory internally, not over UNIBUS.");

	parameter_bool_c swab_vbit = parameter_bool_c(this, "swab_vbit", "swab",/*readonly*/
	false, "SWAB instruction does not(=0) or does(=1) modify psw v-bit (=0 is standard 11/20 behavior)");

	parameter_unsigned_c pc = parameter_unsigned_c(this, "PC", "pc",/*readonly*/
	false, "", "%06o", "program counter helper register.", 16, 8);

	parameter_unsigned_c swreg = parameter_unsigned_c(this, "switch_reg", "swr",/*readonly*/
	false, "", "%06o", "Console switch register.", 16, 8);

	parameter_unsigned_c cycle_count = parameter_unsigned_c(this, "cycle_count", "cc",/*readonly*/
	true, "", "%u", "CPU opcodes executed since last HALT (32bit roll around).", 32, 10);


	struct Bus bus; // UNIBUS interface of CPU
	struct KA11 ka11; // Angelos CPU state

	void start(void);
	void stop(const char * info, bool print_pc = false);

	// background worker function
	void worker(unsigned instance) override;

	// called by qunibusadapter on emulated register access
	void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control)
			override;

	void on_interrupt(uint16_t vector);

	//diagnostic
	trigger_c	trigger ;


};

#endif
