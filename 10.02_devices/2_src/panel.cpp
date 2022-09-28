/* panels.cpp: a device to access lamps & buttons connected over I2C bus

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

 a device to access lamps & buttons connected over I2C bus
 Up to 8 MC23017 GPIO extenders each with 16 I/Os can be connected.

 Other devices register some of their bit-Parameters with IOs.

 1 i2c driver - n panels
 1 paneldriver - controls for many devices of same type (4 buttons for each of 4 RL02s)
 1 control - identifed by unique combination of device name and control name
 ("rl1", "load_button")
 device name ideally same as device-> name
 control name ideally same as deviceparameter->name

 ! Static list of panel controls is consntat,
 but device parameters connected to controls is dynamic
 (run time device creation/deletion)

 */
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
//#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "utils.hpp"
#include "timeout.hpp"
#include "logger.hpp"
#include "gpios.hpp"
#include "panel.hpp"

panelcontrol_c::panelcontrol_c(string _device_name, string _control_name,
bool _is_input, uint8_t _chip_addr, uint8_t _reg_addr, uint8_t _reg_bitmask) {
	device_name = _device_name;
	control_name = _control_name;
	is_input = _is_input;
	chip_addr = _chip_addr;
	reg_addr = _reg_addr;
	reg_bitmask = _reg_bitmask;

	parameter = NULL;
	value = 0;
	value_invalid = true; // valid if queried once from chips

	associate = NULL;
}

string panelcontrol_c::full_name() {
	return device_name + "." + control_name;
}

// Set control value from a I2C chip register value
// Only update device parameters on GPIO changes.
// So user button operation can be used in parallel with
// other parmeter changing mechanisms.
void panelcontrol_c::set_param_from_register_value(i2c_chip_register_c *chip_register,
		uint8_t reg_value) {
	uint8_t new_value;
	// binary only is easy

	reg_value ^= chip_register->invert_mask; // correct GPIO polarity

	// 1. I2C chip register bits -> panel control value
	if (reg_value & reg_bitmask)
		new_value = 1;
	else
		new_value = 0;

	// 2. panel control value -> device parameter value
	if ((value_invalid || new_value != value) && parameter != NULL) {
		// changed: update param.
		// type independent, because over text representation
		string s = to_string(new_value);
		parameter->parse(s);
	}
	value = new_value;
	value_invalid = false;
}

// return value as bitmask for chip registers
uint8_t panelcontrol_c::get_param_as_register_value(i2c_chip_register_c *chip_register) {
	uint8_t reg_value;
	// 1. device parameter value -> panel control value
	// if no param connected: work on previous value
	if (parameter != NULL) {
		// type independent, because over text representation
		string *s = parameter->render();
		value = stoi(*s) & 0xff;
	}

	// 2. panel control value -> I2C chip register bits
	// binary only is easy
	if (value)
		reg_value = reg_bitmask;
	else
		reg_value = 0;
	return reg_value ^ chip_register->invert_mask;
}

paneldriver_c *paneldriver; // another Singleton

/* Register addresses of MC23017.
 Order is power-up default BNAK=0
 */
// pin directio:  1 = input, 0 = output
#define MC23017_IODIRA  0x00
#define MC23017_IODIRB  0x01

// polarity. 1 = inverted
#define MC23017_IPOLA   0x02
#define MC23017_IPOLB   0x03

// pullups: 1 = 100k to Vcc
#define MC23017_GPPUA 0x0c
#define MC23017_GPPUB 0x0d

// port register
#define MC23017_GPIOA 0x12
#define MC23017_GPIOB 0x13

paneldriver_c::paneldriver_c() :
		device_c()  // super class constructor
{
	// static config
	name.value = "PANEL";
	type_name.value = "paneldriver_c";
	log_label = "pnl";

	i2c_device_file = 0;
	register_controls();
}

paneldriver_c::~paneldriver_c() {
	unregister_controls();
}

bool paneldriver_c::on_param_changed(parameter_c *param) {
	// no own parameter logic
	return device_c::on_param_changed(param);
}

/* low level access to I2C bus slaves */
// https://elinux.org/Interfacing_with_I2C_Devices#Opening_the_Bus
// result: true = success
bool paneldriver_c::i2c_read_byte(uint8_t slave_addr, uint8_t reg_addr, uint8_t *result) {
	struct i2c_rdwr_ioctl_data msgset;
	struct i2c_msg iomsgs[2];
	uint8_t buf[1], rbuf[1];
	int rc;
	buf[0] = (char) reg_addr;
	// write address
	iomsgs[0].addr = iomsgs[1].addr = (unsigned) slave_addr;
	iomsgs[0].flags = 0; /* Write */
	iomsgs[0].buf = buf;
	iomsgs[0].len = 1;
	// read value
	iomsgs[1].flags = I2C_M_RD; /* Read */
	iomsgs[1].buf = rbuf;
	iomsgs[1].len = 1;
	msgset.msgs = iomsgs;
	msgset.nmsgs = 2;
	rc = ioctl(i2c_device_file, I2C_RDWR, &msgset);
	if (rc < 0)
		return false;
	*result = rbuf[0];
	return true;
}

// result: true = success
bool paneldriver_c::i2c_write_byte(uint8_t slave_addr, uint8_t reg_addr, uint8_t b) {
	// https://github.com/ve3wwg/raspberry_pi/tree/master/mcp23017
	struct i2c_rdwr_ioctl_data msgset;
	struct i2c_msg iomsgs[1];
	uint8_t buf[2];
	int rc;
	buf[0] = reg_addr; /* MCP23017 register no. */
	buf[1] = b; /* Byte to write to register */
	iomsgs[0].addr = (unsigned) slave_addr;
	iomsgs[0].flags = 0; /* Write */
	iomsgs[0].buf = buf;
	iomsgs[0].len = 2;
	msgset.msgs = iomsgs;
	msgset.nmsgs = 1;
	rc = ioctl(i2c_device_file, I2C_RDWR, &msgset);
	return (rc >= 0);
}

// reprogram the I2C chips and restart the worker
void paneldriver_c::reset(void) {
	timeout_c timeout;
	enabled.set(false); // worker_stop();

	// pulse "panel_reset_l"
	// MC32017: at least 1 us
	GPIO_SETVAL(gpios->i2c_panel_reset, 0)
	; // low active
	timeout.wait_us(10);
	GPIO_SETVAL(gpios->i2c_panel_reset, 1)
	;

	// re-open I2C device

	//
	if (i2c_device_file)
		close(i2c_device_file);
	if ((i2c_device_file = open(i2c_device_fname, O_RDWR)) < 0) {
		ERROR("Failed to open I2C bus on %s", i2c_device_fname);
		return;
	}

	/* setup io registers and directions of MC23017
	 Default: Port A = outputs, Port B = inputs with pull ups
	 TODO: OVERRIDEN BY  storage drivces registering their parameters.
	 */
	uint8_t chipnr; // iterates MC23017s
	for (chipnr = 0; chipnr < 2; chipnr++) {
		uint8_t slave_addr = 0x20 + chipnr;

		// Register order is for BANK=0
		// A = output, B = input
		i2c_write_byte(slave_addr, MC23017_IODIRA, 0x00);
		i2c_write_byte(slave_addr, MC23017_IODIRB, 0xff);
		// all pullups enabled
		i2c_write_byte(slave_addr, MC23017_GPPUA, 0xff);
		i2c_write_byte(slave_addr, MC23017_GPPUB, 0xff);
	}

	enabled.set(true); // worker_start();
}

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void paneldriver_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
	UNUSED(aclo_edge) ;
	UNUSED(dclo_edge) ;
}

void paneldriver_c::on_init_changed(void) {
}

/* clear static list of all controls on connected panels */
void paneldriver_c::unregister_controls() {
	// remove allocated list members, then trunc list
	vector<panelcontrol_c *>::iterator it = controls.begin();
	while (it != controls.end())
		delete (*it);
	controls.clear();
}

/* build static list of all controls on connected panels */
void paneldriver_c::register_controls() {
	bool input = true; // helper constants
	bool output = false;

	panelcontrol_c *pci, *pco; // inputs and outputs

	controls.clear();

	// name controls like device parameters.
	// see RL0102.hpp for parameter names
	// MC23017: reg 0x12 = GPIOA = lamp outputs
	//          reg 0x13 = GPIOB = button inputs
	pco = new panelcontrol_c("rl0", "loadlamp", output, 0x20, 0x12, 0x01);
	controls.push_back(pco);
	pci = new panelcontrol_c("rl0", "runstopbutton", input, 0x20, 0x13, 0x01);
	controls.push_back(pci);
	pci->associate = pco; // button includes lamp
	pco = new panelcontrol_c("rl0", "readylamp", output, 0x20, 0x12, 0x02);
	controls.push_back(pco);
	pco = new panelcontrol_c("rl0", "faultlamp", output, 0x20, 0x12, 0x04);
	controls.push_back(pco);
	pci = new panelcontrol_c("rl0", "writeprotectbutton", input, 0x20, 0x13, 0x08);
	controls.push_back(pci);
	pco = new panelcontrol_c("rl0", "writeprotectlamp", output, 0x20, 0x12, 0x08);
	controls.push_back(pco);
	pci->associate = pco; // button includes lamp

	pco = new panelcontrol_c("rl1", "loadlamp", output, 0x20, 0x12, 0x10);
	controls.push_back(pco);
	pci = new panelcontrol_c("rl1", "runstopbutton", input, 0x20, 0x13, 0x10);
	controls.push_back(pci);
	pci->associate = pco; // button includes lamp
	pco = new panelcontrol_c("rl1", "readylamp", output, 0x20, 0x12, 0x20);
	controls.push_back(pco);
	pco = new panelcontrol_c("rl1", "faultlamp", output, 0x20, 0x12, 0x40);
	controls.push_back(pco);
	pco = new panelcontrol_c("rl1", "writeprotectlamp", output, 0x20, 0x12, 0x80);
	controls.push_back(pco);
	pci = new panelcontrol_c("rl1", "writeprotectbutton", input, 0x20, 0x13, 0x80);
	controls.push_back(pci);
	pci->associate = pco; // button includes lamp

	pco = new panelcontrol_c("rl2", "loadlamp", output, 0x21, 0x12, 0x01);
	controls.push_back(pco);
	pci = new panelcontrol_c("rl2", "runstopbutton", input, 0x21, 0x13, 0x01);
	controls.push_back(pci);
	pci->associate = pco; // button includes lamp
	pco = new panelcontrol_c("rl2", "readylamp", output, 0x21, 0x12, 0x02);
	controls.push_back(pco);
	pco = new panelcontrol_c("rl2", "faultlamp", output, 0x21, 0x12, 0x04);
	controls.push_back(pco);
	pco = new panelcontrol_c("rl2", "writeprotectlamp", output, 0x21, 0x12, 0x08);
	controls.push_back(pco);
	pci = new panelcontrol_c("rl2", "writeprotectbutton", input, 0x21, 0x13, 0x08);
	controls.push_back(pci);
	pci->associate = pco; // button includes lamp

	pco = new panelcontrol_c("rl3", "loadlamp", output, 0x21, 0x12, 0x10);
	controls.push_back(pco);
	pci = new panelcontrol_c("rl3", "runstopbutton", input, 0x21, 0x13, 0x10);
	controls.push_back(pci);
	pci->associate = pco; // button includes lamp
	pco = new panelcontrol_c("rl3", "readylamp", output, 0x21, 0x12, 0x20);
	controls.push_back(pco);
	pco = new panelcontrol_c("rl3", "faultlamp", output, 0x21, 0x12, 0x40);
	controls.push_back(pco);
	pco = new panelcontrol_c("rl3", "writeprotectlamp", output, 0x21, 0x12, 0x80);
	controls.push_back(pco);
	pci = new panelcontrol_c("rl3", "writeprotectbutton", input, 0x21, 0x13, 0x80);
	controls.push_back(pci);
	pci->associate = pco; // button includes lamp

	// make additional list of used I2C registers
	i2c_chip_registers.clear();
	i2c_chip_registers.push_back(i2c_chip_register_c(0x20, 0x12, output, 0));
	i2c_chip_registers.push_back(i2c_chip_register_c(0x20, 0x13, input, 0xff));
	i2c_chip_registers.push_back(i2c_chip_register_c(0x21, 0x12, output, 0));
	i2c_chip_registers.push_back(i2c_chip_register_c(0x21, 0x13, input, 0xff));

	// connect button with the lamps inside them

}

// search for a control with given identifiers.
// NULL if none
panelcontrol_c *paneldriver_c::control_by_name(string device_name, string control_name) {
	for (vector<panelcontrol_c *>::iterator it = controls.begin(); it != controls.end(); ++it) {
		if (strcasecmp((*it)->device_name.c_str(), device_name.c_str()) == 0
				&& strcasecmp((*it)->control_name.c_str(), control_name.c_str()) == 0)
			return (*it);
	}
	return NULL;
}

void paneldriver_c::clear_all_outputs() {
	// clear parameter link for selected controls
	for (vector<panelcontrol_c *>::iterator it = controls.begin(); it != controls.end(); ++it)
		if (!(*it)->is_input)
			(*it)->value = 0;
}

void paneldriver_c::link_control_to_parameter(parameter_c *deviceparameter,
		panelcontrol_c *panelcontrol) {
	// only booleans allwoed at the moment
	parameter_bool_c *bp = dynamic_cast<parameter_bool_c *>(deviceparameter);
	if (bp == NULL)
		FATAL("Can link only boolean parameters to paneldriver controls");

	if (deviceparameter->readonly && panelcontrol->is_input)
		FATAL("Can not link readonly param to paneldriver input");

	panelcontrol->parameter = deviceparameter;
}

void paneldriver_c::unlink_controls_from_device(device_c *device) {
	// clear parameter link for selected controls
	for (vector<panelcontrol_c *>::iterator it = controls.begin(); it != controls.end(); ++it) {
		if ((*it)->parameter != NULL && (*it)->parameter->parameterized == device) {
			(*it)->parameter = NULL;
			(*it)->value = 0; // show as "OFF"
		}
	}
}

// invalidate input control values of all controls connected
// to a device parameter. Forces full update of parameters by worker()
void paneldriver_c::refresh_params(device_c *device) {
	for (vector<panelcontrol_c *>::iterator it = controls.begin(); it != controls.end(); ++it) {
		if ((*it)->parameter != NULL && (*it)->parameter->parameterized == device)
			(*it)->value_invalid = true;
	}
}

/* query input registers and set parameters
 * read parameters and update output registers
 */
void paneldriver_c::i2c_sync_all_params() {
	vector<i2c_chip_register_c>::iterator it_cr;
	for (it_cr = i2c_chip_registers.begin(); it_cr != i2c_chip_registers.end(); it_cr++) {
		if (it_cr->is_input) {
			// read registers, update controls
			uint8_t value;
			if (i2c_read_byte(it_cr->chip_addr, it_cr->reg_addr, &value)) {
				vector<panelcontrol_c *>::iterator it_pc;
				for (it_pc = controls.begin(); it_pc != controls.end(); ++it_pc)
					if ((*it_pc)->chip_addr == it_cr->chip_addr
							&& (*it_pc)->reg_addr == it_cr->reg_addr)
						// control linked to this chip register: update
						(*it_pc)->set_param_from_register_value(&(*it_cr), value);
			} // else I2C error: how report ?
		} else {
			// output register
			uint8_t value = 0;
			vector<panelcontrol_c *>::iterator it_pc;
			for (it_pc = controls.begin(); it_pc != controls.end(); ++it_pc)
				if ((*it_pc)->chip_addr == it_cr->chip_addr
						&& (*it_pc)->reg_addr == it_cr->reg_addr)
					// control linked to this chip register: output value bits
					value |= (*it_pc)->get_param_as_register_value(&(*it_cr));
			i2c_write_byte(it_cr->chip_addr, it_cr->reg_addr, value);
			// if I2C error: how report ?
		}
	}
}

/* background worker.
 Query all used I2C chip register,
 Update controls and parameters
 */
void paneldriver_c::worker(unsigned instance) {
	UNUSED(instance); // only one
	timeout_c timeout;

	while (!workers_terminate) {
		// poll in endless round
		i2c_sync_all_params();
		timeout.wait_ms(10);
	}
}

// test, requires running worker()
void paneldriver_c::test_moving_ones(void) {
	timeout_c timeout;
	unsigned delay_ms = 500; // longer than worker period!

	INFO("Light lamps one by one. Starting worker().");

	clear_all_outputs();
	timeout.wait_ms(delay_ms);

	// iterate outputs, light one after another
	for (vector<panelcontrol_c *>::iterator it = controls.begin(); it != controls.end(); ++it)
		if (!(*it)->is_input) {
			clear_all_outputs(); // delete prev lamp
			(*it)->value = 1;
			timeout.wait_ms(delay_ms);
		}
	clear_all_outputs();
	timeout.wait_ms(delay_ms);
	// all "OFF" on exit

}

// test, requires running worker()
void paneldriver_c::test_manual_loopback(void) {
	timeout_c timeout;
	vector<panelcontrol_c *>::iterator it;

	INFO("Manual loopback test, stop with ^C");
	INFO("Copy state of all inputs to associated output.");

	// Setup ^C catcher
	SIGINTcatchnext();
	while (!SIGINTreceived) {
		for (it = controls.begin(); it != controls.end(); it++)
			if ((*it)->is_input && (*it)->associate != NULL)
				(*it)->associate->value = (*it)->value;
		timeout.wait_ms(10);
	}
}

