/* qunibussignals.hpp: Control of singel UNIBUS/QBUS signal wires

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

#include <string.h>
#include <string>
#include <vector>

// compound qunibus signals

class qunibus_signal_c {
public:
	const char *name;
	unsigned bitwidth;
	qunibus_signal_c() {
	}
	;
	qunibus_signal_c(const char *name, unsigned bitwidth) {
		this->name = name;
		this->bitwidth = bitwidth;
	}
	virtual void set_val(unsigned value)=0;
	virtual unsigned get_val()=0;

	void oscillate_bit(unsigned bitmask, unsigned timeout_ms) ;
};

// regular 1-bit signal, directly controls buslatch bit. uses wire_info
class qunibus_signal_bit_c: public qunibus_signal_c {
public:
	// bitwidth = 1
	qunibus_signal_bit_c(const char *name) :
			qunibus_signal_c(name, 1) {
	}

	virtual void set_val(unsigned value) override {
		// all other are single bit signals: get from wire_info
		buslatches_wire_info_t *wi = buslatches_wire_info_get(name, /*is_input*/false);
		if (wi) { // writable
			buslatches[wi->reg_sel]->setval(1 << wi->bit_nr, value << wi->bit_nr);
		}
	}

	virtual unsigned get_val() override {
		unsigned result=0;
		// all other are single bit signals: get from wire_info
		buslatches_wire_info_t *wi = buslatches_wire_info_get(name, /*is_input*/true);
		if (wi) { // readable
			uint8_t latchval = buslatches[wi->reg_sel]->getval();
			result = (latchval & (1 << wi->bit_nr)) >> wi->bit_nr;
		}
		return result;
	}
};

// single bit signal with inverted levels (UNIBUS BG*,NPG)
class qunibus_signal_bitinv_c: public qunibus_signal_bit_c {
public:
	// bitwidth = 1
	qunibus_signal_bitinv_c(const char *name) :
			qunibus_signal_bit_c(name) {
	}
	virtual void set_val(unsigned value) override {
		qunibus_signal_bit_c::set_val(!value);
	}
	virtual unsigned get_val() override {
		return !qunibus_signal_bit_c::get_val();
	}
};

class qunibus_signals_c: public std::vector<qunibus_signal_c *> {
public:
	qunibus_signals_c();
	unsigned max_name_len();

	// search signal
	qunibus_signal_c *by_name(const char *name) {
	for (const auto& sig: *this) // C++11 wizardry
		if (!strcasecmp(sig->name, name))
			return sig ;
    return nullptr ;
	}

// clear or set all signals
	void reset(unsigned state) {
		unsigned val = state? 0xffffffff : 0 ;
		// set all to 0
		for (unsigned i = 0; i < size(); i++)
			at(i)->set_val(val);
	}

};

extern qunibus_signals_c qunibus_signals; // singleton

// test IniProbe or QProbe by oscillating qunibus_signals
bool test_probe(unsigned timeout_ms) ;


