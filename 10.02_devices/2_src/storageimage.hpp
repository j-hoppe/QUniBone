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

//using namespace std;

#include <stdint.h>
#include <string>
#include <fstream>
#include "logsource.hpp"
#include "bytebuffer.hpp"

// generic interface to emulated drive
// storage is accessed as stream of bytes
class storageimage_base_c: public logsource_c  {
public:
    // pure interface
    virtual ~storageimage_base_c() {} ; // google for "abstract destructor" for fun
    virtual bool is_readonly() = 0 ;
    virtual bool open(bool create) = 0;
    virtual bool is_open(	void)= 0;
    virtual bool truncate(void)= 0 ;
    virtual void read(uint8_t *buffer, uint64_t position, unsigned len)=0;
    virtual void write(uint8_t *buffer, uint64_t position, unsigned len)= 0;
	virtual void set_zero(uint64_t position, unsigned len) ;
	virtual bool is_zero(uint64_t position, unsigned len) ;
	
    virtual uint64_t size(void)= 0;
    virtual void close(void)= 0;
	virtual void get_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset, uint32_t data_size) = 0;
	virtual void set_bytes(byte_buffer_c *byte_buffer)= 0 ;
	
	//	bool image_load_from_disk(string host_filename, 		bool allowcreate, bool *filecreated) ;
	virtual void save_to_file(string host_filename) = 0 ; // make a snapshot
	
} ;



// a monolitic binary disk file containing the byte stream, SimH compatible
class storageimage_binfile_c: public storageimage_base_c {
private:
    bool readonly ;
    fstream f; // image file
    std::string image_fname ;

public:
	storageimage_binfile_c(std::string _image_fname) {
		image_fname = _image_fname ;
	}

	// nothing to free
	virtual ~storageimage_binfile_c() override { 
		// handle recreation via param change with open images
		close() ; 
	} 
	
    virtual bool is_readonly() override {
        return readonly ;
    }
    virtual bool open(bool create) override;
    virtual bool is_open(	void) override;
    virtual bool truncate(void) override;
    virtual void read(uint8_t *buffer, uint64_t position, unsigned len) override;
    virtual void write(uint8_t *buffer, uint64_t position, unsigned len) override;
    virtual uint64_t size(void) override;
    virtual void close(void) override;
	virtual void get_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset, uint32_t data_size) override;
	virtual void set_bytes(byte_buffer_c *byte_buffer) override ;
	virtual void save_to_file(string host_filename) override ; 
	

} ;

// in-memory version of disk image file
class storageimage_memory_c: public storageimage_base_c {
private:
    bool readonly ;
    fstream f; // image file
    // std::string image_fname ;
    uint8_t 	*data ; // the disk image content
	uint64_t	data_size ;
	bool opened ; // between open() and close()

public:
	storageimage_memory_c(unsigned _capacity) {
		opened = false;
		data = nullptr ;
		data_size = _capacity ;
	}

	// nothing to free
	virtual ~storageimage_memory_c() override { 
		close() ; 
	} 
	
    virtual bool is_readonly() override {
        return readonly ;
    }
    virtual bool open(bool create) override;
    virtual bool is_open(	void) override;
    virtual bool truncate(void) override;
    virtual void read(uint8_t *buffer, uint64_t position, unsigned len) override;
    virtual void write(uint8_t *buffer, uint64_t position, unsigned len) override;
    virtual uint64_t size(void) override;
    virtual void close(void) override;
	virtual void get_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset, uint32_t data_size) override;
	virtual void set_bytes(byte_buffer_c *byte_buffer) override ;
	bool load_from_file(string _host_filename,  	 bool allowcreate, bool *result_file_created);
	void save_to_file(string _host_filename) override ;
	

} ;

#endif

