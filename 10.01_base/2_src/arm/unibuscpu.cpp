/* unibuscpu.cpp: base class for all CPU implementations

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


27-aug-2019	JH      start
 */

#include "logger.hpp"
#include "unibuscpu.hpp"

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void unibuscpu_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
	// called within a bus_cycle, and initiates other cycles?!
	// Emulation of old behaviour
	if (aclo_edge == SIGNAL_EDGE_RAISING) {    
		INFO("CPU: ACLO active");
		power_event_ACLO_active = true ;
	} else if (dclo_edge == SIGNAL_EDGE_RAISING) {    
		INFO("CPU: DCLO active");
		power_event_DCLO_active = true ;
//			ka11_pwrfail_trap(&unibone_cpu->ka11);
		// ACLO failed. 
			// CPU traps to vector 24 and has 2ms time to execute code
	} else if (aclo_edge == SIGNAL_EDGE_FALLING) {
		INFO("CPU: ACLO inactive");
		 power_event_ACLO_inactive = true;
//			ka11_pwrup_vector_fetch(&unibone_cpu->ka11);
		// DCLO restored
		// CPU loads PC and PSW from vector 24 
		// if HALTed: do nothing, user is expected to setup PC and PSW ?
	} 
// cleared only by cpu after processing	
// else power_event = power_event_none ;
		
/*	 	
		

on DCLO: set INIT
on ACLO: init, timeout, 
set "power_up_pending"

On start (rmeove of HALT):
if (power_up_pending) fect 24/26, start executiuon

fetch vector 24/26,

//assert(dbg==0) ;
	if (power_down) { // power-on defaults
*/
}

// QBUS/UNIBUS INIT: clear all registers
void unibuscpu_c::on_init_changed(void) {
// a CPU does not react to INIT ... else its own RESET would reset it.
}


