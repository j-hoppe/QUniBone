/* storagedrive.hpp: disk or tape drive, with an image file as storage medium.

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

 may-2019		JD		file_size()
 12-nov-2018  JH      entered beta phase

 */
#ifndef _STORAGEDRIVE_HPP_
#define _STORAGEDRIVE_HPP_

using namespace std;

#include <stdint.h>
#include <string>
#include <fstream>
#include <assert.h>

#include "utils.hpp"
#include "device.hpp"
#include "parameter.hpp"

class storagecontroller_c;

class storagedrive_c: public device_c {
private:
	fstream f; // image file

public:
	storagecontroller_c *controller; // link to parent

	// identifying number at controller
	parameter_unsigned_c unitno = parameter_unsigned_c(this, "unit", "unit", /*readonly*/
	true, "", "%d", "Unit # of drive", 3, 10); // 3 bits = 0..7 allowed

	// capacity of medium (disk/tape) in bytes
	parameter_unsigned64_c capacity = parameter_unsigned64_c(this, "capacity", "cap", /*readonly*/
	true, "byte", "%d", "Storage capacity", 64, 10);

	parameter_string_c image_filepath = parameter_string_c(this, "image", "img", /*readonly*/
	false, "Path to image file");

	virtual bool on_param_changed(parameter_c *param) override;

//	parameter_bool_c writeprotect = parameter_bool_c(this, "writeprotect", "wp", /*readonly*/false, "Medium is write protected, different reasons") ;

	bool file_readonly;bool file_open(std::string imagefname, bool create);bool file_is_open(
			void);
	void file_read(uint8_t *buffer, uint64_t position, unsigned len);
	void file_write(uint8_t *buffer, uint64_t position, unsigned len);
	uint64_t file_size(void);
	void file_close(void);

	storagedrive_c(storagecontroller_c *controller);

};

class storagedrive_selftest_c: public storagedrive_c {
private:
	const char *imagefname;
	unsigned block_size;
	unsigned block_count;
	uint8_t *block_buffer;

	void block_buffer_fill(unsigned block_number);
	void block_buffer_check(unsigned block_number);

public:
	storagedrive_selftest_c(const char *imagefname, unsigned block_size, unsigned block_count) :
			storagedrive_c(NULL) {
		assert((block_size % 4) == 0); // whole uint32s
		this->imagefname = imagefname;
		this->block_size = block_size;
		this->block_count = block_count;

		this->block_buffer = (uint8_t *) malloc(block_size);
	}
	~storagedrive_selftest_c() {
		free(block_buffer);
	}

	// fill abstracts
	virtual void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
		UNUSED(aclo_edge) ;
		UNUSED(dclo_edge) ;
	}
	virtual void on_init_changed(void) {
	}

	void test(void);
};

#endif
