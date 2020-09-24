/* rl.cpp: device exerciser for RL disk drive

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


 19-mar-2019  JH      edit start
 */

#include "devexer_rl.hpp"

namespace devexer {

rl_c::rl_c(int subtype) :
		disk_c() {
	this->subtype = subtype;
	assert(subtype == 1 || subtype == 2);
	name.value = "RL";
	// type_name.value = "PDP-11/20";
	log_label = "DERL";

	// RL11 drive
	base_addr.value = 0774400;

	max_blockaddr.unit.value = 3;
	max_blockaddr.cylinder.value = (subtype == 1 ? 255 : 511);
	max_blockaddr.head.value = 1;
	max_blockaddr.sector.value = 39;
	max_blockaddr.blocknr.value = (max_blockaddr.unit.value + 1)
			* (max_blockaddr.cylinder.value + 1) * (max_blockaddr.head.value + 1);
}

void rl_c::init(unsigned unitnr) {
	UNUSED(unitnr); // todo
}

// read access
void rl_c::readtrack(unsigned unitnr, uint8_t *data) {
	UNUSED(unitnr); // todo
	UNUSED(data);
}

void rl_c::readsector(unsigned unitnr, uint8_t *data) {
	UNUSED(unitnr); // todo
	UNUSED(data);
}

} // namespace
