/* testcontroller.hpp: sample QBUS/UNIBUS controller with selftest logic

 Copyright (c) 2018-2019, Joerg Hoppe
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


 23-jul-2019  JH      added interrupt and DMA functions
 12-nov-2018  JH      entered beta phase
 */
#ifndef _TESTCONTROLLER_HPP_
#define _TESTCONTROLLER_HPP_

#include "utils.hpp"
#include "qunibusdevice.hpp"
#include "memoryimage.hpp"
#include "parameter.hpp"

class testcontroller_c: public qunibusdevice_c {
private:

public:

	qunibusdevice_register_t *CSR; // command and status register

	parameter_unsigned_c access_count = parameter_unsigned_c(this, "access_count", "ac",/*readonly*/
	true, "", "%u", "Total # of register accesses", 32, 10);

	// For arbitray tests of the priority request system, we have
	// one request for every slot/level combination
	static const unsigned dma_channel_count = 2;
	dma_request_c *dma_channel_request[dma_channel_count];
	// for concurrent DMA, testcontroller needs one data buffer per possible DMA.
	// these are n*4MB !
	memoryimage_c *dma_channel_buffer[dma_channel_count];

	intr_request_c *intr_request[PRIORITY_SLOT_COUNT][4]; // 3 slots, 4 levels

	testcontroller_c();
	~testcontroller_c();

	bool on_param_changed(parameter_c *param) override;  // must implement

	// background worker function
	void worker(unsigned instance) override;

	// called by qunibusadapter on emulated register access
	void on_after_register_access(qunibusdevice_register_t *device_reg, uint8_t unibus_control, DATO_ACCESS access)
			override;

	void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
	void on_init_changed(void) override;

	void test_dma_priority(void);
};

#endif
