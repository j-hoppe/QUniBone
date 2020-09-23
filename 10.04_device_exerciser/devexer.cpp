/* devexer.hpp: base classes for device exerciser

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


 16-mar-2019  JH      edit start

 A device exerciser access a device installed on UNIBUS,
 for test or to read/write data.


 Classhierarche

 rl < disk < devexer
 rk < disk < devexer

 identify a piece of data in the streams

 blockaddr_disk_c <- blockaddr_c
 blockaddr_tape_c <- blockaddr_c

 Uses also params
 */

#include <algorithm>

#include "devexer.hpp"

namespace devexer {

// declare device list of class separate
list<devexer_c *> devexer_c::myexercisers;

devexer_c::devexer_c() {
// add reference to device to class list
	myexercisers.push_back(this);
}

devexer_c::~devexer_c() {
	// registered parameters must be deleted by allocator
	parameter.clear();

	// remove device from class list
	// https://www.safaribooksonline.com/library/view/c-cookbook/0596007612/ch08s05.html
	list<devexer_c*>::iterator p = find(myexercisers.begin(), myexercisers.end(), this);
	if (p != myexercisers.end())
		myexercisers.erase(p);
}

} // namespace
