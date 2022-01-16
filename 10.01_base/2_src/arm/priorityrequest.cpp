/* priorityrequest.cpp: DMA or iNTR request of an device

 Copyright (c) 2019 Joerg Hoppe
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


 jul-2019     JH      start: multiple parallel arbitration levels
 */

#include <assert.h>
#include <algorithm>

#include "logger.hpp"
#include "qunibusdevice.hpp"
#include "priorityrequest.hpp"

priority_request_c::priority_request_c(qunibusdevice_c *device) {
	this->log_label = "REQ";
	this->device = device;
	this->complete = false;
	this->executing_on_PRU = false;
	this->priority_slot = 0xff; // uninitialized, asserts() if used
	complete_mutex = PTHREAD_MUTEX_INITIALIZER;
	complete_cond = PTHREAD_COND_INITIALIZER; // PRU signal notifies request on completeness
}

priority_request_c::~priority_request_c() {
	// not used, but need some virtual func for dynamic_cast()
}

void priority_request_c::set_priority_slot(uint8_t priority_slot) {
	assert(priority_slot > 0); // 0 reserved
	assert(priority_slot < PRIORITY_SLOT_COUNT);
	if (this->priority_slot == priority_slot)
		return ; // called on every on_param_change()
	qunibusdevice_c *ubdevice = qunibusdevice_c::find_by_request_slot(priority_slot);
	if (ubdevice && ubdevice != this->device) {
            WARNING("Slot %u requested by device %s, already used by %s", (unsigned) priority_slot,
                      this->device->name.value.c_str(), ubdevice->name.value.c_str());
		}
	this->priority_slot = priority_slot;
	// todo: check for collision with all other devices, all other requests
}

// create invalid requests, is setup by qunibusadapter
dma_request_c::dma_request_c(qunibusdevice_c *device) :
		priority_request_c(device) {
	this->level_index = PRIORITY_LEVEL_INDEX_NPR;
	this->success = false;
	this->is_cpu_access = false ;// over written for emulated CPU
	// register request for device
	if (device) {
		device->dma_requests.push_back(this);
	}
}

dma_request_c::~dma_request_c() {
	if (device) {
		// find and erase from device's request list
		std::vector<dma_request_c *>::iterator it = std::find(device->dma_requests.begin(),
				device->dma_requests.end(), this);
		device->dma_requests.erase(it);
	}
}

// create invalid requests, is setup by qunibusadapter
intr_request_c::intr_request_c(qunibusdevice_c *device) :
		priority_request_c(device) {
	// convert QBUS/UNIBUS level 4,5,6,7 to internal priority, see REQUEST_INDEX_*
	this->level_index = 0xff; // uninitialized, asserts() if used
	this->vector = 0xffff; // uninitialized, asserts() if used
	this->signal_level = 0;
	if (device)
		device->intr_requests.push_back(this);
}

intr_request_c::~intr_request_c() {
	if (device) {
		// find and erase from device's request list
		std::vector<intr_request_c *>::iterator it = std::find(device->intr_requests.begin(),
				device->intr_requests.end(), this);
		device->intr_requests.erase(it);
	}
}

void intr_request_c::set_level(uint8_t level) {
	assert(level >= 4 && level <= 7);
	this->level_index = level - 4;  // one of PRIORITY_LEVEL_INDEX_*
}

void intr_request_c::set_vector(uint16_t vector) {
	assert((vector & 3) == 0); // multiple of 2 words
	this->vector = vector;
}

