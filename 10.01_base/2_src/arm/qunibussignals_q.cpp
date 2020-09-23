/* qunibussignals.cpp: Control of singel UNIBUS/QBUS signal wires

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


 19-jul-2020	JH  	begin
 */

#include "utils.hpp"
#include "timeout.hpp"

#include "buslatches.hpp"
#include "qunibussignals.hpp"


// QBUS dal lines
class qunibus_signal_dal_c: public qunibus_signal_c {
public:
	qunibus_signal_dal_c(const char *name):qunibus_signal_c(name,22) {};
	virtual void set_val(unsigned value) override {
		buslatches[0]->setval(0xff, value); // DAL<0:7>
		buslatches[1]->setval(0xff, value >> 8); // DAL<8:15>
		buslatches[2]->setval(0x3f, value >> 16); // DAL<16:21>
		}
	virtual unsigned get_val() override {
		unsigned result = 0 ;
		result = buslatches[0]->getval(); // DAL<0:7>
		result |= buslatches[1]->getval() << 8; // DAL<8:15>
		result |= (buslatches[2]->getval() & 0x3f) << 16; // DAL<16:21>
		return result ;
		}
};

// single bit, latched by SYNC (BS7, WTBT not latched on SYNC)
class qunibus_signal_bit_sync_latched_c: public qunibus_signal_bit_c {
	// the SYNC signal to be used for latching
	qunibus_signal_bit_c *sync_signal = nullptr;

public:
	qunibus_signal_bit_sync_latched_c(const char *name, qunibus_signal_bit_c *sync): qunibus_signal_bit_c(name) {
		this->sync_signal = sync;
	}
	virtual void set_val(unsigned value) override {
		bool cur_sync_val = sync_signal->get_val() ;
		// sync inactive
		if (cur_sync_val)
			sync_signal->set_val(0) ;
		// output signal
		qunibus_signal_bit_c::set_val(value) ;
		// SYNC active .. latches on L->H edge
		sync_signal->set_val(1) ;
		// restore sync
		if (cur_sync_val != 1)
			sync_signal->set_val(0) ;
		}
	virtual unsigned get_val() override {
		// latched value always visible
		return qunibus_signal_bit_c::get_val() ;
		}
};


// oscillate single bit of a multi-bit signal.
// runs for timeout_ms or throws expection on ^C
void qunibus_signal_c::oscillate_bit(unsigned bitmask, unsigned timeout_ms) {
	timeout_c timeout;
	unsigned count;

	timeout.start_ms(timeout_ms);
	SIGINTcatchnext();

	// high speed loop
	count = 0;
	while (!timeout.reached() && !SIGINTreceived) {
		if ((count % 4) == 0) // toggle 0/0/0/1
			set_val(0xffffffff) ; // 1
		else // clear single bit 	
		set_val(0xffffffff & ~bitmask) ; // 0
		count++;
	}
	set_val(0xffffffff) ; // end condition: 1
	if (SIGINTreceived)
		throw std::exception() ; // break caller
}



/**** GPIO access to QBUS signals ****/
qunibus_signals_c qunibus_signals; // singleton

qunibus_signals_c::qunibus_signals_c() {
// fill dictionary, order like in DEC manual

	push_back(new qunibus_signal_dal_c("DAL"));
	qunibus_signal_bit_c *sync = new qunibus_signal_bit_c("SYNC") ;
	push_back(sync);
	push_back(new qunibus_signal_bit_c("DIN"));
	push_back(new qunibus_signal_bit_c("DOUT"));
	push_back(new qunibus_signal_bit_c("WTBT"));
	push_back(new qunibus_signal_bit_sync_latched_c("BS7", sync));
	push_back(new qunibus_signal_bit_c("RPLY"));
	push_back(new qunibus_signal_bit_c("DMR"));
	push_back(new qunibus_signal_bit_c("DMGI")); // read only
	push_back(new qunibus_signal_bit_c("DMGO"));// write only
	push_back(new qunibus_signal_bit_c("SACK"));
	push_back(new qunibus_signal_bit_c("IRQ4"));
	push_back(new qunibus_signal_bit_c("IRQ5"));
	push_back(new qunibus_signal_bit_c("IRQ6"));
	push_back(new qunibus_signal_bit_c("IRQ7"));
	push_back(new qunibus_signal_bit_c("IAKI")); // read only
	push_back(new qunibus_signal_bit_c("IAKO")); // write only
	push_back(new qunibus_signal_bit_c("POK"));
	push_back(new qunibus_signal_bit_c("DCOK"));
	push_back(new qunibus_signal_bit_c("INIT"));
	push_back(new qunibus_signal_bit_c("HALT"));
	push_back(new qunibus_signal_bit_c("REF"));
	push_back(new qunibus_signal_bit_c("EVNT"));
}

unsigned qunibus_signals_c::max_name_len() {
	return 4; // see above
}




// test QProbe LEDs, until ^C
bool test_probe(unsigned timeout_ms) {
	bool aborted = false ;
		
		try { 
			// test in order of QProbe signals
				// lower row: DAL0..21
			for (unsigned i=0 ; i <= 21 ; i++) {
				qunibus_signals.by_name("DAL")->oscillate_bit(1 << i, timeout_ms) ;
				}				
			
		qunibus_signals.by_name("BS7")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("IRQ4")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("IRQ5")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("IRQ6")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("IRQ7")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("IAKO")->oscillate_bit(1, timeout_ms) ;

			// upper row
		qunibus_signals.by_name("POK")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("DCOK")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("DOUT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("DIN")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("WTBT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("SYNC")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("RPLY")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("INIT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("HALT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("EVNT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("REF")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("SACK")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("DMR")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("DMGO")->oscillate_bit(1, timeout_ms) ;
			}catch (std::exception const &e) {aborted = true; }; // abort by ^C
			return aborted ;
			
	}
