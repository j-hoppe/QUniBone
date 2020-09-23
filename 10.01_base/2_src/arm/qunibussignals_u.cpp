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

class qunibus_signal_addr_c: public qunibus_signal_c {
public:
	qunibus_signal_addr_c(const char *name):qunibus_signal_c(name,18) {};
	virtual void set_val(unsigned value) override {
	buslatches[2]->setval(0xff, value); // ADDR0:7
	buslatches[3]->setval(0xff, value >> 8); // ADDR8:15
	buslatches[4]->setval(0x03, value >> 16); // ADDR16,17
		}
	virtual unsigned get_val() override {
		unsigned result = 0;
			result = buslatches[2]->getval(); // ADDR0:7
			result |= buslatches[3]->getval() << 8; // ADDR8:15
			result |= (buslatches[4]->getval() & 0x03) << 16; // ADDR8:15
		return result ;
		}
};


class qunibus_signal_data_c: public qunibus_signal_c {
public:
	qunibus_signal_data_c(const char *name):qunibus_signal_c(name, 16) {};
	virtual void set_val(unsigned value) override {
		buslatches[5]->setval(0xff, value); // DATA0:7
		buslatches[6]->setval(0xff, value >> 8); // DATA8:15
		}
	virtual unsigned get_val() override {
		unsigned result = 0 ;
			result = buslatches[5]->getval(); // DATA0:7
			result |= buslatches[6]->getval() << 8; // DATA8:15
		return result ;
		}
};

class qunibus_signal_control_c: public qunibus_signal_c {
public:
	qunibus_signal_control_c(const char *name):qunibus_signal_c(name, 2) {};
	virtual void set_val(unsigned value) override {
		buslatches[4]->setval(0x0C, value << 2); // C1 = 0x8, C0 = 0x4
		}
	virtual unsigned get_val() override {
		return (buslatches[4]->getval() & 0x0c) >> 2; // C1 = 0x8, C0 = 0x4
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



/**** GPIO access to UNIBUS signals ****/
qunibus_signals_c qunibus_signals; // singleton

qunibus_signals_c::qunibus_signals_c() {
// fill dictionary
// order like in DEC manual
	push_back(new qunibus_signal_addr_c("ADDR"));
	push_back(new qunibus_signal_data_c("DATA"));
	push_back(new qunibus_signal_control_c("C1,C0"));
	push_back(new qunibus_signal_bit_c("MSYN"));
	push_back(new qunibus_signal_bit_c("SSYN"));
	push_back(new qunibus_signal_bit_c("PA"));
	push_back(new qunibus_signal_bit_c("PB"));
	push_back(new qunibus_signal_bit_c("INTR"));
	push_back(new qunibus_signal_bit_c("BR4"));
	push_back(new qunibus_signal_bit_c("BR5"));
	push_back(new qunibus_signal_bit_c("BR6"));
	push_back(new qunibus_signal_bit_c("BR7"));
	push_back(new qunibus_signal_bitinv_c("BG4_IN")); // read only
	push_back(new qunibus_signal_bit_c("BG4_OUT")); // write only
	push_back(new qunibus_signal_bitinv_c("BG5_IN"));
	push_back(new qunibus_signal_bit_c("BG5_OUT"));
	push_back(new qunibus_signal_bitinv_c("BG6_IN"));
	push_back(new qunibus_signal_bit_c("BG6_OUT"));
	push_back(new qunibus_signal_bitinv_c("BG7_IN"));
	push_back(new qunibus_signal_bit_c("BG7_OUT"));
	push_back(new qunibus_signal_bit_c("NPR"));
	push_back(new qunibus_signal_bitinv_c("NPG_IN"));
	push_back(new qunibus_signal_bit_c("NPG_OUT"));
	push_back(new qunibus_signal_bit_c("SACK"));
	push_back(new qunibus_signal_bit_c("BBSY"));
	push_back(new qunibus_signal_bit_c("INIT"));
	push_back(new qunibus_signal_bit_c("ACLO"));
	push_back(new qunibus_signal_bit_c("DCLO"));
}

unsigned qunibus_signals_c::max_name_len() {
	return 7; // see above
}

bool test_probe(unsigned timeout_ms) {
	bool aborted = false ;

		try {
			// test in order of UniProbe signals
		qunibus_signals.by_name("ACLO")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("DCLO")->oscillate_bit(1, timeout_ms) ;

				// ADDR<17:0>
			for (int i=17 ; i >= 0 ; i--) {
				qunibus_signals.by_name("ADDR")->oscillate_bit(1 << i, timeout_ms) ;
				}
				// DATA<15:0>
			for (int i=15 ; i >= 0 ; i--) {
				qunibus_signals.by_name("DATA")->oscillate_bit(1 << i, timeout_ms) ;
				}

        // first C0, then C1
				qunibus_signals.by_name("C1,C0")->oscillate_bit(0x01, timeout_ms) ;
				qunibus_signals.by_name("C1,C0")->oscillate_bit(0x02, timeout_ms) ;

		qunibus_signals.by_name("MSYN")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("SSYN")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("PA")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("PB")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("INTR")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("BR4")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("BR5")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("BR6")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("BR7")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("NPR")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("BG4_OUT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("BG5_OUT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("BG6_OUT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("BG7_OUT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("NPG_OUT")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("SACK")->oscillate_bit(1, timeout_ms) ;
		qunibus_signals.by_name("BBSY")->oscillate_bit(1, timeout_ms) ;
			}
                        catch (std::exception const &e) {
                        aborted = true;
                        }; // abort by ^C
			return aborted ;

	}
