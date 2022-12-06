/* demo_io.cpp: sample QBUS/UNIBUS controller with Linux GPIO logic

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

 demo_io:
 A device to access GPIOs with /sys/class interface
 Implements a combined "Switch register/display register" at 0760100
 Read gets the value of the 4 switches at bits 0x000f
 and the button state at MSB 0x0010
 Write sets the LEDs with mask 0x000f
 No active register callbacks, just polling in worker()
 */

#include <string.h>

#include "logger.hpp"
#include "timeout.hpp"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"	// definition of class device_c
#include "demo_io.hpp"

demo_io_c::demo_io_c() :
		qunibusdevice_c()  // super class constructor
{
	// static config
	name.value = "DEMO_IO";
	type_name.value = "demo_io_c";
	log_label = "di";

	set_default_bus_params(0760100, 31, 0, 0); // base addr, intr-vector, intr level

	// init parameters
	switch_feedback.value = false;

	// controller has only 2 register
	register_count = 2;

	switch_reg = &(this->registers[0]); // @  base addr
	strcpy(switch_reg->name, "SR"); // "Switches and Display"
	switch_reg->active_on_dati = false; // no controller state change
	switch_reg->active_on_dato = false;
	switch_reg->reset_value = 0;
	switch_reg->writable_bits = 0x0000; // read only

	display_reg = &(this->registers[1]); // @  base addr + 2
	strcpy(display_reg->name, "DR"); // "Switches and Display"
	display_reg->active_on_dati = false; // no controller state change
	display_reg->active_on_dato = false;
	display_reg->reset_value = 0;
	display_reg->writable_bits = 0x000f; // not necessary

	// map register bits to gpio pins
	//       BBB     ARM       /sys/class/gpio
	// LED0: P8.25 = GPIO1_0  = 32
	// LED1: P8.24 = GPIO1_1  = 33
	// LED2: P8.05 = GPIO1_2  = 34
	// LED3: P8.06 = GPIO1_3  = 35
	// SW0:  P8.23 = GPIO1_4  = 36
	// SW1:  P8.22 = GPIO1_5  = 37
	// SW2:  P8.03 = GPIO1_6  = 38
	// SW3:  P8.04 = GPIO1_7  = 39
	// BTN:  P8.12 = GPIO1_12 = 44

	gpio_open(gpio_outputs[0], false, 32); // LED 0
	gpio_open(gpio_outputs[1], false, 33); // LED 1
	gpio_open(gpio_outputs[2], false, 34); // LED 2
	gpio_open(gpio_outputs[3], false, 35); // LED 3

	gpio_open(gpio_inputs[0], true, 36); // SW0
	gpio_open(gpio_inputs[1], true, 37); // SW1
	gpio_open(gpio_inputs[2], true, 38); // SW2
	gpio_open(gpio_inputs[3], true, 39); // SW3
	gpio_open(gpio_inputs[4], true, 44); // BUTTON
}

demo_io_c::~demo_io_c() 
{
	// close all gpio value files
	unsigned i;
	for (i = 0; i < 5; i++)
		gpio_inputs[i].close();
	for (i = 0; i < 4; i++)
		gpio_outputs[i].close();
}

bool demo_io_c::on_param_changed(parameter_c *param) 
{
	// no own parameter or "enable" logic
	return qunibusdevice_c::on_param_changed(param); // more actions (for enable)
}

/* helper: opens the control file for a gpio
 * exports, programs directions, assigns stream
 */
void demo_io_c::gpio_open(std::fstream& value_stream, bool is_input, unsigned gpio_number) 
{
	const char *gpio_class_path = "/sys/class/gpio";

	value_stream.close(); // if open

	// 1. export pin, so it appears as .../gpio<nr>
	char export_filename[80];
	std::ofstream export_file;
	sprintf(export_filename, "%s/export", gpio_class_path);
	export_file.open(export_filename);

	if (!export_file.is_open()) {
		printf("Failed to open %s.\n", export_filename);
		return;
	}
	export_file << gpio_number << "\n";
	export_file.close();

	// 2. Now we have directory /sys/class/gpio<number>
	//	Set to input or output
	char direction_filename[80];
	std::ofstream direction_file;
	sprintf(direction_filename, "%s/gpio%d/direction", gpio_class_path, gpio_number);
	direction_file.open(direction_filename);
	if (!direction_file.is_open()) {
		printf("Failed to open %s.\n", direction_filename);
		return;
	}
	direction_file << (is_input ? "in" : "out") << "\n";
	direction_file.close();

	// 3. Open the "value" file
	char value_filename[80];
	sprintf(value_filename, "%s/gpio%d/value", gpio_class_path, gpio_number);
	if (is_input)
		value_stream.open(value_filename, std::fstream::in);
	else
		value_stream.open(value_filename, std::fstream::out);
	if (!value_stream.is_open())
		printf("Failed to open %s.\n", value_filename);
}

// read a gpio input value from its stream
unsigned demo_io_c::gpio_get_input(unsigned input_index) 
{
	std::fstream *value_stream = &(gpio_inputs[input_index]);
	if (!value_stream->is_open())
		return 0; // ignore gpio file access errors
	std::string val_str;
	value_stream->seekg(0); // restart reading from begin
	*value_stream >> val_str; // read "0" or "1"
	if (val_str.length() > 0 && val_str[0] == '1')
		return 1;
	else
		return 0;
}

// write a gpio output value into its stream
void demo_io_c::gpio_set_output(unsigned output_index, unsigned value) 
{
	std::fstream *value_stream = &(gpio_outputs[output_index]);
	if (!value_stream->is_open())
		// ignore file access errors
		return;
	value_stream->seekp(0); // restart writing from begin
	// LED voltage signals are inverted: "ON" = 0.
	if (value)
		*value_stream << "0\n";
	else
		*value_stream << "1\n";
	value_stream->flush(); // now!
}

// background worker.
// udpate LEDS, poll switches direct to register flipflops
void demo_io_c::worker(unsigned instance) 
{
	UNUSED(instance); // only one
	timeout_c timeout;
	while (!workers_terminate) {
		timeout.wait_ms(100);

		unsigned i;
		uint16_t register_bitmask, register_value = 0; // bit assembly

		// 1. read the switch values from /sys/class/gpio<n>/value pseudo files
		//    into QBUS/UNIBUS register value bits
		for (i = 0; i < 5; i++) {
			register_bitmask = (1 << i);
			if (gpio_get_input(i) != 0)
				register_value |= register_bitmask;
		}
		// update QBUS/UNIBUS "display" registers
		set_register_dati_value(switch_reg, register_value, __func__);

		// 2. write the LED values from QBUS/UNIBUS register value bits
		//    into /sys/class/gpio<n>/value pseudo files

		// LED control from switches or QBUS/UNIBUS "DR" register?
		if (!switch_feedback.value)
			register_value = get_register_dato_value(display_reg);
		for (i = 0; i < 4; i++) {
			register_bitmask = (1 << i);
			gpio_set_output(i, register_value & register_bitmask);
		}
	}
}

// process DATI/DATO access to one of my "active" registers
// !! called asynchronuously by PRU, with SSYN asserted and blocking QBUS/UNIBUS.
// The time between PRU event and program flow into this callback
// is determined by ARM Linux context switch
//
// QBUS/UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void demo_io_c::on_after_register_access(qunibusdevice_register_t *device_reg,
		uint8_t unibus_control) 
{
	// nothing todo
	UNUSED(device_reg);
	UNUSED(unibus_control);
}

// after QBUS/UNIBUS install, device is reset by DCLO cycle
 void demo_io_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) 
 {
	UNUSED(aclo_edge) ;
	UNUSED(dclo_edge) ;
}

// QBUS/UNIBUS INIT: clear all registers
void demo_io_c::on_init_changed(void) 
{
	// write all registers to "reset-values"
	if (init_asserted) {
		reset_unibus_registers();
		INFO("demo_io_c::on_init()");
	}
}

