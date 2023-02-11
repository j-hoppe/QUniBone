/* storageimage.cpp: bianry SimH comatible image file as storage medium.

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

 A storagedrive is a disk or tape drive, with an image file as storage medium.
 a couple of these are connected to a single "storagecontroler"
 supports the "attach" command
 */
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <ios>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef O_BINARY
#define O_BINARY 0		// for linux compatibility
#endif

#include "logger.hpp"
#include "utils.hpp"
#include "storageimage.hpp"


// BIG use of memory
void storageimage_base_c::set_zero(uint64_t position, unsigned len)
{
    uint8_t *zeros = (uint8_t*) malloc(len) ;
    memset(zeros, 0, len) ;
    write(zeros, position, len);
    free(zeros) ;
}

bool storageimage_base_c::is_zero(uint64_t position, unsigned len)
{
    bool result = true ;
    uint8_t *buffer= (uint8_t*) malloc(len) ;
    read(buffer, position, len);
    for (unsigned i=0 ; i < len ; i++)
        if (buffer[i] != 0)
            result = false ;
    free(buffer) ;
    return result ;
}


// http://www.cplusplus.com/doc/tutorial/files/

// open a file, if possible.
// set the file_readonly flag
// creates file, if not existing
// result: OK= true, else false
bool storageimage_binfile_c::open(storagedrive_c *_drive, bool create) 
{
	drive = _drive ;
    // 1st: if file not exists, try to unzip it from <image_fname>.gz
    int retries = 2 ;
    while (retries > 0) {
        readonly = false;
        if (is_open())
            close(); // after RL11 INIT
        if (image_fname.empty())
            return true ; // ! is_open
        f.open(image_fname, std::ios::in | std::ios::out | std::ios::binary | std::ios::ate);
        if (f.is_open())
            return true;

        // is readonly? try open for read only

        // try again readonly
        f.open(image_fname, std::ios::in | std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            readonly = true;
            return true;
        }

        retries-- ;
        if (retries > 0) {
            // file could not be opened, neither rw nor read only
            // try to unzip, then retry opening
            std::string compressed_image_fname = image_fname + ".gz" ;
            if (FILE *fz = fopen(compressed_image_fname.c_str(), "r")) {
                fclose(fz);
                std::string uncompress_cmd = "zcat " + compressed_image_fname + " >" + image_fname ;
                printf("Only compressed image file %s found, expanding \"%s\" ...\n", image_fname.c_str(), uncompress_cmd.c_str()) ;
                int ret = system(uncompress_cmd.c_str()) ;
                if (ret != 0) {
                    printf(" FAILED!\n") ;
                    retries = 0 ; // not again
                } else
                    printf("... complete.\n") ;

            } else
                retries = 0 ; // not again
        }
    }

    // definitely no image file neither plain nor zipped
    // create one?
    if (!create)
        return false;

    // try to create
    // https://stackoverflow.com/questions/17260394/fstream-not-creating-new-file/18160837
    f.open(image_fname, std::ios::out);
    f.close();
    f.open(image_fname, std::ios::in | std::ios::out | std::ios::binary | std::ios::ate);
    if (f.is_open()) {
        INFO("Created empty image file %s.", image_fname.c_str()) ;
        return true ;
    } else {
        INFO("Creating empty image file %s FAILED.", image_fname.c_str()) ;
        return false;
    }
}

bool storageimage_binfile_c::is_open() 
{
    return f.is_open();
}

// set file size to 0
bool storageimage_binfile_c::truncate() 
{
    assert(is_open());
    assert(!readonly); // caller must take care

    f.close();
    // reopen with "trunc" option
    f.open(image_fname, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    return  f.is_open() ;
}


/* read "len" bytes from file into buffer
 * if file is too short, 00s are read
 * it is assumed that buffer has at least a size of "len"
 */
void storageimage_binfile_c::read(uint8_t *buffer, uint64_t position, unsigned len) 
{
    assert(is_open());
    assert(buffer != nullptr) ;
    assert(len) ;
    // 1. fill the buffer with 00s
    memset(buffer, 0, len);

    // 2. move read pointer
    f.seekg(position);
    // may be at eof now, doesn't matter

    // 3. read as byte count, may abort at end of file
    f.read((char *) buffer, len);
}

/* write "len" bytes from buffer into file at position "offset"
 * if file too short, it is extended
 */
void storageimage_binfile_c::write(uint8_t *buffer, uint64_t position, unsigned len) 
{
    int64_t write_pos = (int64_t) position;  // unsigned-> int
    const int max_chunk_size = 0x40000; //256KB: trade-off between performance and mem usage
    uint8_t *fillbuff = NULL;
    int64_t file_size, p;

    assert(buffer);
    assert(is_open());
    assert(!readonly); // caller must take care

    // enlarge file in chunks until filled up to "position"
    f.clear(); // clear fail bit
    f.seekp(0, std::ios::end); // move to current EOF
    file_size = f.tellp(); // current file len
    if (file_size < 0)
        file_size = 0; // -1 on emtpy files
    while (file_size < write_pos) {
        // fill in '00' 'chunks up to desired end, but limit to max_chunk_size
        int chunk_size = std::min(max_chunk_size, (int) (write_pos - file_size));
        if (!fillbuff) {
            // allocate 00-buffer only once
            fillbuff = (uint8_t *) malloc(max_chunk_size);
            assert(fillbuff);
            memset(fillbuff, 0, max_chunk_size);
        }
        f.clear(); // clear fail bit
        f.seekp(file_size, std::ios::beg); // move to end
        f.write((const char *) fillbuff, chunk_size);
        file_size += chunk_size;
    }
    if (fillbuff)
        free(fillbuff); // has been used, discard

    if (file_size == 0)
        // p = -1 error after seekp(0) on empty files?
        assert(write_pos == 0);
    else {
        // move write pointer to target position
        f.clear(); // clear fail bit
        f.seekp(write_pos, std::ios::beg);
        p = f.tellp(); // position now < target?
        assert(p == write_pos);
    }

    // 3. write data
    f.write((const char*) buffer, len);
    if (f.fail())
        ERROR("storageimage_binfile_c.write() failure on %s", image_fname.c_str());
    f.flush();
}

uint64_t storageimage_binfile_c::size(void) 
{
    f.seekp(0, std::ios::end);
    return f.tellp();
}

void storageimage_binfile_c::close(void) 
{
    if (!is_open())
        return ;
    f.close();
    readonly = false;
}

// read data from image into memory buffer (cache)
void storageimage_binfile_c::get_bytes(byte_buffer_c* byte_buffer, uint64_t byte_offset, uint32_t len)
{
    byte_buffer->set_size(len) ;
    read(byte_buffer->data_ptr(), byte_offset, len) ;
}

// write cache buffer to image
void storageimage_binfile_c::set_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset)
{
    write(byte_buffer->data_ptr(), byte_offset, byte_buffer->size()) ;
}


// make a snapshot
// must be locked against parallel read()/write()/close()
void storageimage_binfile_c::save_to_file(std::string _host_filename)
{
    std::string host_filename = absolute_path(&_host_filename) ;
    assert(is_open()) ;

    try {
        std::streampos current_pos = f.tellg() ;

        // make a stream copy, then resore the position
        std::ofstream dest(host_filename, std::ios::binary);
        f.seekg(0) ; // pos source to begin

        // copy
        dest << f.rdbuf();

        // restore pos
        f.seekg(current_pos) ;
    }
    catch(std::exception& e) {
        ERROR(e.what()) ;
    }
}



// result: OK= true, else false
bool storageimage_memory_c::open(storagedrive_c *_drive, bool create)
{
	drive = _drive ;
    UNUSED(create);
    if (is_open())
        close(); // after RL11 INIT
    if (data_size)
        data = (uint8_t *)malloc(data_size) ;
    opened = true ;
    return true ;
}

bool storageimage_memory_c::is_open(	void)
{
    return opened ;
}

bool storageimage_memory_c::truncate(void)
{
    assert(is_open()) ;
    // fixed size

    free(data) ;
    data = nullptr ;
    data_size = 0;
    return true ;
}

void storageimage_memory_c::read(uint8_t *buffer, uint64_t position, unsigned len)
{
    assert(is_open()) ;
    assert(buffer != nullptr) ;
    assert(len) ;
    // if len > data_size, fill up with 00s

    uint8_t *dest = buffer ;
    unsigned bytes_copied = 0 ;
    if (position < data_size) {
        // copy bytes from data[] to buffer
        uint8_t *src = &data[position] ;
        uint64_t stop_pos = position + len ; // idx of byte after last byte to fill
        if (stop_pos <= data_size)
            bytes_copied = len ; // all wanted bytes in data[]
        else
            bytes_copied = data_size - position ;
        // copy
        memcpy(dest, src, bytes_copied) ;
        dest += bytes_copied ;
    }
    // fill up 00s
    assert (bytes_copied <= len) ;
    if (bytes_copied != len)
        memset(dest, 0, len - bytes_copied) ;
}


void storageimage_memory_c::write(uint8_t *buffer, uint64_t position, unsigned len)
{
    assert(buffer) ;
    assert(is_open()) ;
    // re-allocate?
    uint64_t new_size = position + len - 1 ;
    if (new_size > data_size) {
        data = (uint8_t *)realloc(data, data_size) ; // conent preserved
        data_size = new_size ;
    }
    uint8_t *dest = &(data[position]);
    memcpy(dest, buffer, len) ;
}

uint64_t storageimage_memory_c::size(void)
{
    assert(is_open()) ;
    return data_size ;
}

// data volatile
void storageimage_memory_c::close(void)
{
    assert(is_open()) ;
    free(data) ;
    data = nullptr ;
    // data size remains for next open()
    opened = false ;
}

// extract a smaller buffer, required by storage_image_base_c
void storageimage_memory_c::get_bytes(byte_buffer_c* byte_buffer, uint64_t byte_offset, uint32_t len)
{
    byte_buffer->set_size(len) ;
    assert(byte_offset < data_size) ;
    uint8_t *src = &(data[byte_offset]) ;
    uint8_t *dest = byte_buffer->data_ptr() ;
    assert(src+len <= data+size()) ; // no overrun allowed
    memcpy(dest, src, len) ;
}

// write and free cache buffer
void storageimage_memory_c::set_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset)
{
    uint8_t *src = byte_buffer->data_ptr() ;
    assert(byte_offset < data_size) ;
    uint8_t *dest = &(data[byte_offset]);
    memcpy(dest, src, byte_buffer->size()) ;
}



// load complete image from a host file
// if result_file_created:
bool storageimage_memory_c::load_from_file(std::string _host_filename,
        bool allowcreate, bool *result_file_created)
{
    std::string host_filename ;

    bool result ;
    bool file_created = false ;
    try {
        host_filename = absolute_path(&_host_filename) ;

        // opens image file or creates it
        int32_t file_descriptor;
        struct stat file_status; // timestamps and size

        if (!readonly) // check writability here.
            file_descriptor = ::open(host_filename.c_str(), O_BINARY | O_RDWR, 0666);
        else
            file_descriptor = ::open(host_filename.c_str(), O_BINARY | O_RDONLY);

        // create file if it does not exist
        if (file_descriptor < 0 && allowcreate) {
            file_descriptor = ::creat(host_filename.c_str(), 0666);
            file_created = true;
        }
        if (file_descriptor < 0)
            throw printf_exception("image_data_load_from_disk(): cannot open or create \"%s\"", host_filename.c_str()) ;

        // get timestamps, to monitor changes
        ::stat(host_filename.c_str(), &file_status);

        // clear image
        memset(data, 0, data_size);

        if (!file_created) {
            // read existing file
            if (!is_fileset(&host_filename, 0, data_size))
                if (file_status.st_size > (off_t)data_size) { // trunc ?
                    FATAL("storageimage_memory_c::load_from_disk(): File \"%s\" is %" PRId64 " bytes, shall be trunc'd to %" PRId64 " bytes, non-zero data would be lost",
                          host_filename.c_str(), (uint64_t)file_status.st_size, data_size) ;
                }

            int res = ::read(file_descriptor, data, data_size);
            ::close(file_descriptor) ;

            // read file to memory
            if (res < 0)
                throw printf_exception("storageimage_memory_c::load_from_disk(): cannot read \"%s\"", host_filename.c_str()) ;
            if (res < file_status.st_size)
                throw printf_exception("storageimage_memory_c::load_from_disk(): cannot read %" PRId64 " bytes from \"%s\"",
                                       (uint64_t)file_status.st_size, host_filename.c_str()) ;
        }

        result = true ;
    }
    catch(std::exception& e) {
        ERROR(e.what()) ;
        result = false ;
    }
    if (result_file_created)
        *result_file_created = file_created ;
    return result ;
}

// needs to be locked against image changes
void storageimage_memory_c::save_to_file(std::string _host_filename)
{
    std::string host_filename = absolute_path(&_host_filename) ;

    try {
        // opens image file for full rewrite or creates it
        int32_t file_descriptor;
        file_descriptor = ::open(host_filename.c_str(), O_BINARY | O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (file_descriptor < 0)
            throw printf_exception("storageimage_memory_c::data_save_to_disk() cannot open \"%s\"",
                                   host_filename.c_str());
        ::write(file_descriptor, data, data_size);
        ::close(file_descriptor);
    }
    catch(std::exception& e) {
        ERROR(e.what()) ;
    }
}



