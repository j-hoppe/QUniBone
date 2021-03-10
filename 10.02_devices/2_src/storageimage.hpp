/* storageimage.hpp: geneariv interface and implemention of plain file as storage medium

 Copyright (c) 2021, Joerg Hoppe
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

 07-mar-2021	JH      start

 A disk/tape emulation (storage drive) saves data onto some magnetic surface,
 organized as filesystem.
 On Linux side this is saves a
 - plain binary file (SimH compatible block stream)
 - an unpacked shared directory with file tree


 */
#ifndef _STORAGEIMAGE_HPP_
#define _STORAGEIMAGE_HPP_

using namespace std;

#include <stdint.h>
#include <string>

// generic interface to emulated drive
// storage is accessed as stream of bytes
class storageimage_c {
public:
    // interface
	storageimage_c()  { }
	virtual ~storageimage_c()  { }
    virtual bool is_readonly() = 0 ;
    virtual bool open(std::string imagefname, bool create) = 0;
    virtual bool is_open(	void)= 0;
    virtual bool truncate(void)= 0 ;
    virtual void read(uint8_t *buffer, uint64_t position, unsigned len)=0;
    virtual void write(uint8_t *buffer, uint64_t position, unsigned len)= 0;
    virtual uint64_t size(void)= 0;
    virtual void close(void)= 0;
} ;



// a monolitic binary file containing the byte stream, SimH compatible
#include <fstream>
#include "logsource.hpp"

class storageimage_binfile_c: public storageimage_c, public logsource_c {
private:
    bool readonly ;
    fstream f; // image file
    std::string image_fname ;

public:
    virtual bool is_readonly() {
        return readonly ;
    }
    virtual bool open(std::string imagefname, bool create) ;
    virtual bool is_open(	void) ;
    virtual bool truncate(void) ;
    virtual void read(uint8_t *buffer, uint64_t position, unsigned len) ;
    virtual void write(uint8_t *buffer, uint64_t position, unsigned len) ;
    virtual uint64_t size(void) ;
    virtual void close(void) ;

} ;


#endif

