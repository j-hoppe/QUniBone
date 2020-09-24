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

 A device exerciser accesses a device installed on UNIBUS,
 for test or to read/write data.


 Classhierarche

 rl < disk < devexer
 rk < disk < devexer

 identify a piece of data in the streams

 blockaddr_disk_c <- blockaddr_c
 blockaddr_tape_c <- blockaddr_c

 Uses also params
 */

#include <list>
#include <stdint.h>
//#include <string.h>
#include "time.h"

#include <vector>

#include "logsource.hpp"
#include "parameter.hpp"

using namespace std;

namespace devexer {

class devexer_c;

// abstract base class for different block addresses
class blockaddr_c {
	int dummy; // abstroct block addr;
};

// an error, which occured on a certain disk/tape block
class event_c {
	timeval timestamp;
	blockaddr_c blockaddr;
	string info;
};

// abstract base class for all devicee exerciser
class devexer_c: public logsource_c, public parameterized_c {
public:
	// the class holds a list of pointers to instantiated exercisers
	static list<devexer_c *> myexercisers;
// also needed to have a list of threads

	parameter_string_c name = parameter_string_c(this, "name", "name", /*readonly*/
	true, "Unique identifier of device exerciser");

	parameter_unsigned_c base_addr = parameter_unsigned_c(this,
			"base_addr", "addr", true, "", "%06o",
			"controller base address in IO page", 18, 8);

	// list of blockaddresses with error info
	vector<event_c> events;
	devexer_c();
	~devexer_c();
};

class blockaddr_disk_c: public blockaddr_c {
public:
	parameter_unsigned_c unit = parameter_unsigned_c(NULL, "unit", "ues", false, "", "%d",
			"disk unit #, start with 0", 8, 10);
	parameter_unsigned_c cylinder = parameter_unsigned_c(NULL, "cylinder", "c", false, "", "%d",
			"cylinder, start with 0", 8, 10);
	parameter_unsigned_c head = parameter_unsigned_c(NULL, "head", "h", false, "", "%d",
			"head, start with 0", 8, 10);
	parameter_unsigned_c sector = parameter_unsigned_c(NULL, "sector", "s", false, "", "%d",
			"sector, start with 0", 8, 10);
	parameter_unsigned_c blocknr = parameter_unsigned_c(NULL, "block", "b", false, "", "%d",
			"block #, start with 0", 8, 10);
};

// disk exerciser has "blockadress_disk"
class disk_c: public devexer_c {
public:
	blockaddr_disk_c max_blockaddr;  // geometry:
	uint32_t sector_size; // bytes!

	// working position
	blockaddr_c cur_blockaddr;

	// initialize drive
	virtual void init(unsigned unitnr) = 0;
	// read access
	virtual void readtrack(unsigned unitnr, uint8_t *data) = 0;
	virtual void readsector(unsigned unitnr, uint8_t *data) = 0;

};

// list of params

}// namespace
