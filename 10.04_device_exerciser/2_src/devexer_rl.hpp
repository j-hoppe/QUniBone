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

#include <assert.h>

#include "utils.hpp"
#include "devexer.hpp"

namespace devexer {

class rl_c: public disk_c {
public:
	int subtype; // 1 =RL01, 2 =RL02

	rl_c(int subtype = 2);

	// implement abstracts

	// initialize drive
	virtual void init(unsigned unitnr);
	// read access
	virtual void readtrack(unsigned unitnr, uint8_t *data);
	virtual void readsector(unsigned unitnr, uint8_t *data);

	// must implemment for "paremterized_c"
	virtual bool on_param_changed(parameter_c *param) {
		UNUSED(param);
		return true; // all values accepted
	}

};

} // namespace

