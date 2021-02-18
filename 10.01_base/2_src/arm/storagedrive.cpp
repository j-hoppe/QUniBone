/* storagedrive.cpp: disk or tape drive, with an image file as storage medium.

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

 A storagedrive is a disk or tape drive, with an image file as storage medium.
 a couple of these are connected to a single "storagecontroler"
 supports the "attach" command
 */
#include <assert.h>

#include <fstream>
#include <ios>
using namespace std;

#include "logger.hpp"
#include "storagedrive.hpp"

storagedrive_c::storagedrive_c(storagecontroller_c *controller) :
    device_c() {
    this->controller = controller;
    /*
     // parameters for all drices
     param_add(&unitno) ;
     param_add(&capacity) ;
     param_add(&image_filepath) ;
     */
}

// implements params, so must handle "change"
bool storagedrive_c::on_param_changed(parameter_c *param) {
    // no own "enable" logic
    return device_c::on_param_changed(param);
}

// http://www.cplusplus.com/doc/tutorial/files/

// open a file, if possible.
// set the file_readonly flag
// creates file, if not existing
// result: OK= true, else false
bool storagedrive_c::file_open(string image_fname, bool create) {

    // 1st: if file not exists, try to unzip it from <image_fname>.gz
    int retries = 2 ;
    while (retries > 0) {
        file_readonly = false;
        if (file_is_open())
            file_close(); // after RL11 INIT
        f.open(image_fname, ios::in | ios::out | ios::binary | ios::ate);
        if (f.is_open())
            return true;

        // is readonly? try open for read only

        // try again readonly
        f.open(image_fname, ios::in | ios::binary | ios::ate);
        if (f.is_open()) {
            file_readonly = true;
            return true;
        }

        retries-- ;
        if (retries > 0) {
            // file could not be opened, neither rw nor read only
            // try to unzip, then retry opening
            string compressed_image_fname = image_fname + ".gz" ;
            if (FILE *fz = fopen(compressed_image_fname.c_str(), "r")) {
                fclose(fz);
                string uncompress_cmd = "zcat " + compressed_image_fname + " >" + image_fname ;
                printf("Only compressed image file %s found, expanding \"%s\" ...", image_fname.c_str(), uncompress_cmd.c_str()) ;
                int ret = system(uncompress_cmd.c_str()) ;
                if (ret != 0) {
                    printf(" FAILED!\n") ;
                    retries = 0 ; // not again
                } else
                    printf(" complete.\n") ;

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
    f.open(image_fname, ios::out);
    f.close();
    f.open(image_fname, ios::in | ios::out | ios::binary | ios::ate);
	if (f.is_open()) {
		INFO("Created empty image file %s.", image_fname.c_str()) ;
		return true ;
	} else {
		INFO("Creating empty image file %s FAILED.", image_fname.c_str()) ;
	    return false;
	}
}

bool storagedrive_c::file_is_open() {
    return f.is_open();
}

/* read "len" bytes from file into buffer
 * if file is too short, 00s are read
 * it is assumed that buffer has at least a size of "len"
 */
void storagedrive_c::file_read(uint8_t *buffer, uint64_t position, unsigned len) {
    assert(file_is_open());
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
void storagedrive_c::file_write(uint8_t *buffer, uint64_t position, unsigned len) {
    int64_t write_pos = (int64_t) position;  // unsigned-> int
    const int max_chunk_size = 0x40000; //256KB: trade-off between performance and mem usage
    uint8_t *fillbuff = NULL;
    int64_t file_size, p;

    assert(file_is_open());
    assert(!file_readonly); // caller must take care

    // enlarge file in chunks until filled up to "position"
    f.clear(); // clear fail bit
    f.seekp(0, ios::end); // move to current EOF
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
        f.seekp(file_size, ios::beg); // move to end
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
        f.seekp(write_pos, ios::beg);
        p = f.tellp(); // position now < target?
        assert(p == write_pos);
    }

    // 3. write data
    f.write((const char*) buffer, len);
    if (f.fail())
        ERROR("file_write() failure on %s", name.value.c_str());
    f.flush();
}

uint64_t storagedrive_c::file_size(void) {
    f.seekp(0, ios::end);
    return f.tellp();
}

void storagedrive_c::file_close(void) {
    assert(file_is_open());
    f.close();
    file_readonly = false;
}

// fill buffer with test data to be placed at "file_offset"
void storagedrive_selftest_c::block_buffer_fill(unsigned block_number) {
    assert((block_size % 4) == 0); // whole uint32
    for (unsigned i = 0; i < block_size / 4; i++) {
        // i counts dwords in buffer
        // pattern: global incrementing uint32
        uint32_t pattern = i + (block_number * block_size / 4);
        ((uint32_t*) block_buffer)[i] = pattern;
    }
}

// verify pattern generated by fillbuff
void storagedrive_selftest_c::block_buffer_check(unsigned block_number) {
    assert((block_size % 4) == 0);	// whole uint32
    for (unsigned i = 0; i < block_size / 4; i++) {
        // i counts dwords in buffer
        // pattern: global incrementing uint32
        uint32_t pattern_expected = i + (block_number * block_size / 4);
        uint32_t pattern_found = ((uint32_t*) block_buffer)[i];
        if (pattern_expected != pattern_found) {
            printf(
                "ERROR storage_drive selftest: Block %d, dword %d: expected 0x%x, found 0x%x\n",
                block_number, i, pattern_expected, pattern_found);
            exit(1);
        }
    }
}

// self test of random access file interface
// test file has 'block_count' blocks with 'block_size' bytes capacity each.
void storagedrive_selftest_c::test() {
    unsigned i;
    bool *block_touched = (bool *) malloc(block_count * sizeof(bool)); // dyn array
    int blocks_to_touch;

    /*** fill all blocks with random accesses, until all blcoks touched ***/
    file_open(imagefname, true);

    for (i = 0; i < block_count; i++)
        block_touched[i] = false;

    blocks_to_touch = block_count;
    while (blocks_to_touch > 0) {
        unsigned block_number = random() % block_count;
        block_buffer_fill(block_number);
        file_write(block_buffer, /*position*/block_size * block_number, block_size);
        if (!block_touched[block_number]) { // mark
            block_touched[block_number] = true;
            blocks_to_touch--;
        }
    }
    file_close();

    /*** verify all blocks with random accesses, until all blcoks touched ***/
    file_open(imagefname, true);

    for (i = 0; i < block_count; i++)
        block_touched[i] = false;

    blocks_to_touch = block_count;
    while (blocks_to_touch > 0) {
        unsigned block_number = random() % block_count;
        file_read(block_buffer, /*position*/block_size * block_number, block_size);
        block_buffer_check(block_number);
        if (!block_touched[block_number]) { // mark
            block_touched[block_number] = true;
            blocks_to_touch--;
        }
    }

    file_close();

    free(block_touched);
}

