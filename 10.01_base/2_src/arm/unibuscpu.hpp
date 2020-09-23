/* unibuscpu.hpp: base class for all CPU implementations

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

#ifndef _UNIBUSCPU_HPP_
#define _UNIBUSCPU_HPP_


#include "qunibusdevice.hpp"

// a CPU is just a device with INTR facilities
class unibuscpu_c: public qunibusdevice_c {
	public:
			unibuscpu_c(): qunibusdevice_c() {
				power_event_ACLO_active = power_event_ACLO_inactive = power_event_DCLO_active = false ;
				} ;

//	enum power_event_enum   {power_event_none, power_event_ACLO_active, power_event_ACLO_inactive, power_event_DCLO_active} ;

	bool power_event_ACLO_active ;
	bool power_event_DCLO_active ;
	bool power_event_ACLO_inactive ;
//	enum power_event_enum power_event ;
		
	// called by PRU on INTR, returns new priority level
	virtual void on_interrupt(uint16_t vector) = 0 ;

	
	virtual void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) ;
	virtual void on_init_changed(void) ;
};

#endif
