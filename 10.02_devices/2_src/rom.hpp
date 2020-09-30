/* rom.hpp - emulate a ROM in QBUS/UNIBUS IO page

 Copyright (c) 2020, Joerg Hoppe, j_hoppe@t-online.de, www.retrocmp.com

 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 - Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.

 - Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.

 - Neither the name of the copyright holder nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 7-feb-2020   JH      begin

 */
#ifndef _ROM_HPP_
#define _ROM_HPP_

#include <stdio.h>
#include <vector>
#include "logsource.hpp"
#include "memoryimage.hpp"

class rom_c: public logsource_c {
private:

public:
	string name; // identifier for user

	uint32_t baseaddress; // start of code in QBUS/UNIBUS address space
	uint32_t wordcount; // length in 16 bit words

	vector<uint16_t> cells; // data

	codelabel_map_c codelabels; // smybolic addresses

	rom_c(string name, unsigned wordcount, uint32_t baseaddress);

	void fill(uint16_t value);

	bool load_macro11_listing(const char *fname);

	uint16_t get_data(uint32_t addr); // get a cell, or exception
	void set_data(uint32_t addr, uint16_t value); // set a cell
	void relocate(uint32_t new_base_addr);
	void install(void); // implemement on QBUS/UNIBUS
	void uninstall(void); // remove from QBUS/UNIBUS

	void dump(FILE *f);
};

#endif
