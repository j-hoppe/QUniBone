/* demo_io.hpp: sample QBUS/UNIBUS controller with Linux GPIO logic

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
#ifndef _DEMO_IO_HPP_
#define _DEMO_IO_HPP_

#include <fstream>

#include "utils.hpp"
#include "qunibusdevice.hpp"

class demo_io_c: public qunibusdevice_c {
private:

	qunibusdevice_register_t *switch_reg;
	qunibusdevice_register_t *display_reg;

	// file handles for GPIO pins
	std::fstream gpio_inputs[5]; // 4 switches, 1 button
	std::fstream gpio_outputs[4]; // 4 LEDs

	void gpio_open(std::fstream& value_stream, bool is_input, unsigned gpio_number);
	unsigned gpio_get_input(unsigned input_index);
	void gpio_set_output(unsigned output_index, unsigned value);

public:

	demo_io_c();
	~demo_io_c();

	bool on_param_changed(parameter_c *param) override;  // must implement

	parameter_bool_c switch_feedback = parameter_bool_c(this, "switch_feedback", "sf",/*readonly*/
	false, "1 = hard wire Switches to LEDs, PDP-11 can not set LEDs");

	// background worker function
	void worker(unsigned instance) override;

	// called by qunibusadapter on emulated register access
	void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control, DATO_ACCESS access)
			override;

	void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
	void on_init_changed(void) override;
};

#endif
