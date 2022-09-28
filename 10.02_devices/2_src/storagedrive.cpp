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
 supports the "attach" command.

 The image maybe an plain binary file, or a shared host directory holding an unpacked DEC filesystem.
 */
#include <assert.h>

#include <fstream>
#include <ios>
using namespace std;

#include "logger.hpp"
#include "gpios.hpp" // drive_activity_led

#include "sharedfilesystem/filesystem_base.hpp"
#include "sharedfilesystem/storageimage_shared.hpp"

#include "storagedrive.hpp"

storagedrive_c::storagedrive_c(storagecontroller_c *_controller) :
    device_c() 
{

    memset(zeros, 0, sizeof(zeros));

    controller = _controller;
    /*
     // parameters for all drices
     param_add(&unitno) ;
     param_add(&capacity) ;
     param_add(&image_filepath) ;
     */

    image = nullptr ; // create on parameter setting
    // or pure "shared" directory, or syncronizing share<->binary image

    // default: shared filesystem not (yet) implementable for this disk type (MSCP)
    sharedfilesystem_drivetype = sharedfilesystem::devNONE ;
}

storagedrive_c::~storagedrive_c() 
{
    image_delete() ;
}

// control readonly status of all image-relevant parameters
void storagedrive_c::image_params_readonly(bool readonly) 
{
    image_filepath.readonly = readonly ;
    image_filesystem.readonly = readonly ;
    image_shareddir.readonly = readonly ;
}

bool storagedrive_c::image_is_param(parameter_c *param) {
    return (param == &image_filepath)
           || (param == &image_filesystem)
           || (param == &image_shareddir) ;
}

// implements params, so must handle "change"
bool storagedrive_c::on_param_changed(parameter_c *param) {
    // no own "enable" logic
    return device_c::on_param_changed(param);
}

// free image, todo: atomic via mutex
void storagedrive_c::image_delete() {
    if (image == nullptr)
        return ;
    storageimage_base_c *tmpimage = image ;
    image = nullptr ; // semi-atomic
    delete tmpimage ;
}



// one of the parameters used for image implementation changed:
// "param" must be one of the image describing parameters
// (execute image_is_param()) first
// then it is checked wether a new image can/must be created
// via param.new_value and <other-params>.value
// then instantiate image of correct type. do not yet open() !
//	result true: parameter accepted. 
//	      image may be nullptr, when more parameters are needed
bool storagedrive_c::image_recreate_on_param_change(parameter_c *param) 
{
    bool accepted = false ;
    if (param == &image_filepath) {
        // binary image?
        // todo: well-formed path? else later open() fails
        image_delete() ;
		accepted = image_recreate_shared_on_param_change(image_filepath.new_value, image_filesystem.value, image_shareddir.value) ;
	    if (image == nullptr)  // not enough params for shared dir: try regular image
	        image = new storageimage_binfile_c(image_filepath.new_value) ; // dyn size
        accepted = (image != nullptr) ;
    } else if (param == &image_filesystem) {
        // shared image file system change?
        if ( !strcasecmp(image_filesystem.new_value.c_str(), "XXDP")
                || !strcasecmp(image_filesystem.new_value.c_str(), "RT11"))
            // valid fs:
            accepted = image_recreate_shared_on_param_change(image_filepath.value, image_filesystem.new_value, image_shareddir.value) ;
    } else if (param == &image_shareddir) {
        // shared image host root dir change?
        accepted = image_recreate_shared_on_param_change(image_filepath.value, image_filesystem.value, image_shareddir.new_value) ;
    }
    return accepted ;
}

// eval parameter set for shared image
// result: parameter accepted, image recreated
bool storagedrive_c::image_recreate_shared_on_param_change(string image_path, string filesystem_paramval, string shareddir_paramval) 
{
    bool ok = false ;

    if (! filesystem_paramval.empty() && ! shareddir_paramval.empty()) {
        image_delete() ;
        WARNING("TODO: drive size (trunc!) and unitno may change? Propagate to shared image!") ;
		enum sharedfilesystem::filesystem_type_e filesystem_type ;
		filesystem_type = sharedfilesystem::filesystem_type2text(filesystem_paramval) ;
		assert(filesystem_type != sharedfilesystem::fst_none) ; // to be checked by param entry

        image = new sharedfilesystem::storageimage_shared_c(
			image_path,
			/*use_syncer_thread*/true,
            filesystem_type, sharedfilesystem_drivetype, unitno.value,
            capacity.value, shareddir_paramval) ;
		if (image != nullptr)
	        image->log_level_ptr = log_level_ptr ; // same log level as drive
        // filesystem_dec has lifetime between open() and close()

        ok = true ;
    }
    return ok;
}


// wrap actual image driver
bool storagedrive_c::image_open(bool create) 
{
    if (image == nullptr)
        return false ;

    // virtual method of implementation
    return image->open(create) ;
}

void storagedrive_c::image_close(void) {
    if (image == nullptr)
        return ;
    image->close() ;
}

bool storagedrive_c::image_is_open(void) {
    if (image == nullptr)
        return false ;
    return image->is_open() ;
}

bool storagedrive_c::image_is_readonly() {
    if (image == nullptr)
        return false ;
    return image->is_readonly() ;
}


bool storagedrive_c::image_truncate(void) {
    if (image == nullptr)
        return false ; // is_open
    return image->truncate() ;
}

uint64_t storagedrive_c::image_size(void) {
    if (image == nullptr)
        return 0 ;
    return image->size() ;
}

void storagedrive_c::image_read(uint8_t *buffer, uint64_t position, unsigned len) {
    if (image == nullptr)
        return ;
    set_activity_led(true) ; // indicate only read/write access
    image->read(buffer, position, len) ;
    set_activity_led(false) ;
}

void storagedrive_c::image_write(uint8_t *buffer, uint64_t position, unsigned len) {
    if (image == nullptr)
        return ;
    set_activity_led(true) ;
    image->write(buffer, position, len) ;
    set_activity_led(false) ;
}
// Service function for disk drive who need to clear unwritten bytes in last block of transaction
// Sometimes when writing incomplete disk blocks, the remaining bytes must be filled with 00s
// Some disk are guaranteed to write only whole blocks, then always unused_byte_count=0
// (test with MSCP, KED under RT11)
void storagedrive_c::image_clear_remaining_block_bytes(unsigned block_size_bytes, uint64_t position, unsigned len) {
    // asume last transaction worte "len" bytes to offset "position"
    uint64_t unused_block_bytes_start = position + len ; // first unused
    // round to block end
    // example for calculation: blocks of 0x100, written to pos=0x100, len=0xfd -> must clear 1fe,1ff
    uint64_t unused_block_bytes_end = ((unused_block_bytes_start + block_size_bytes - 1) / block_size_bytes) * block_size_bytes ;
    assert(unused_block_bytes_end >= unused_block_bytes_start) ;
    unsigned unused_byte_count = unused_block_bytes_end - unused_block_bytes_start ;
    if (unused_byte_count > 0) {
        assert(sizeof(zeros) >= unused_byte_count) ;
        image_write(zeros,/*pos=*/unused_block_bytes_start, unused_byte_count) ;
    }
}

void storagedrive_c::set_activity_led(bool onoff) {
    // only 4 leds: if larger number, then supress display
    if (activity_led.value >= 4)
        return ;
    gpios->drive_activity_led.set(activity_led.value, onoff) ;
}




// fill buffer with test data to be placed at "file_offset"
void storagedrive_selftest_c::block_buffer_fill(unsigned block_number) {
    assert((block_size % 4) == 0); // whole uint32_t
    for (unsigned i = 0; i < block_size / 4; i++) {
        // i counts dwords in buffer
        // pattern: global incrementing uint32_t
        uint32_t pattern = i + (block_number * block_size / 4);
        ((uint32_t*) block_buffer)[i] = pattern;
    }
}

// verify pattern generated by fillbuff
void storagedrive_selftest_c::block_buffer_check(unsigned block_number) {
    assert((block_size % 4) == 0);	// whole uint32_t
    for (unsigned i = 0; i < block_size / 4; i++) {
        // i counts dwords in buffer
        // pattern: global incrementing uint32_t
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

    /*** fill all blocks with random accesses, until all blocks touched ***/
    image_open(true);

    for (i = 0; i < block_count; i++)
        block_touched[i] = false;

    blocks_to_touch = block_count;
    while (blocks_to_touch > 0) {
        unsigned block_number = random() % block_count;
        block_buffer_fill(block_number);
        image_write(block_buffer, /*position*/block_size * block_number, block_size);
        if (!block_touched[block_number]) { // mark
            block_touched[block_number] = true;
            blocks_to_touch--;
        }
    }
    image_close();

    /*** verify all blocks with random accesses, until all blcoks touched ***/
    image_open(true);

    for (i = 0; i < block_count; i++)
        block_touched[i] = false;

    blocks_to_touch = block_count;
    while (blocks_to_touch > 0) {
        unsigned block_number = random() % block_count;
        image_read(block_buffer, /*position*/block_size * block_number, block_size);
        block_buffer_check(block_number);
        if (!block_touched[block_number]) { // mark
            block_touched[block_number] = true;
            blocks_to_touch--;
        }
    }

    image_close();

    free(block_touched);
}

