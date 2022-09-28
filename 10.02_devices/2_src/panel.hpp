/* panels.hpp: a device to access lamps & buttons connected over I2C bus

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

#include "utils.hpp"
#include "device.hpp"
#include "parameter.hpp"

// describes the register of an I2C bus chip
class i2c_chip_register_c {
public:
	uint8_t chip_addr;
	uint8_t reg_addr;bool is_input;
	uint8_t invert_mask; // these GPIO work inverted
	i2c_chip_register_c(uint8_t _chip_addr, uint8_t _reg_addr, bool _is_input,
			uint8_t _invert_mask) {
		chip_addr = _chip_addr;
		reg_addr = _reg_addr;
		is_input = _is_input;
		invert_mask = _invert_mask;
	}
};

/* one instance of a lamp or button on one of the panels
 * may be linked to a device parameter*/
class panelcontrol_c {
public:
	// unique identifier is combination of arbitary device anen
	// and arbitrary control name.
	// device name ideally same as device-> name
	// control name ideally same as deviceparameter->name
	string device_name; // "rl1"
	string control_name; // "load_button"
	string full_name();

	bool is_input; // false = 0 = output
	unsigned bitwidth = 1; 	// later non-binary controls?
	// bool inverted ; // help interpret GPIO signals

	// parameter interface
	parameter_c *parameter; // link

	// i2c interface
	uint8_t chip_addr; // addr of chip in I2C addr space
	uint8_t reg_addr; // register address of GPIO port inside chip
	uint8_t reg_bitmask; // bit positions in device GPIO registers

	// run time data
	bool value_invalid;
	unsigned value; // input or output

	// output lamps can be linekd to their including buttons
	panelcontrol_c *associate;

	panelcontrol_c(string device_name, string control_name,
	bool is_input, uint8_t chip_addr, uint8_t reg_addr, uint8_t reg_bitmask);

	void set_param_from_register_value(i2c_chip_register_c *chip_register, uint8_t reg_value);
	uint8_t get_param_as_register_value(i2c_chip_register_c *chip_register);

};

/* manages the I2C hardware and updates parameters */
class paneldriver_c: public device_c {
private:
	int i2c_device_file; // file handle to I2C bus devive
	const char *i2c_device_fname = "/dev/i2c-2"; // BUS I2C2

	vector<i2c_chip_register_c> i2c_chip_registers;

public:
	paneldriver_c();
	~paneldriver_c();

	bool on_param_changed(parameter_c *param) override;  // must implement

	vector<panelcontrol_c *> controls;

	void unregister_controls(void); // clear static list of all connected controls
	void register_controls(void); // build static list of all connected controls
	panelcontrol_c *control_by_name(string device_name, string control_name);

	void reset(void);

	// low level I2C register access
	bool i2c_read_byte(uint8_t slave_addr, uint8_t reg_addr, uint8_t *result);

	bool i2c_write_byte(uint8_t slave_addr, uint8_t reg_addr, uint8_t b);

	void i2c_sync_all_params();

	// background worker function
	void worker(unsigned instance) override;

	void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override; // must implement
	void on_init_changed(void) override; // must implement

	void clear_all_outputs(void);

	// link_control_to_parameter(new panelcontrol_c()) ;
	void link_control_to_parameter(parameter_c *deviceparameter, panelcontrol_c *panelcontrol);
	void unlink_controls_from_device(device_c *device);

	void refresh_params(device_c *device);

	void test_moving_ones(void);
	void test_manual_loopback(void);

};

extern paneldriver_c *paneldriver; // another Singleton

