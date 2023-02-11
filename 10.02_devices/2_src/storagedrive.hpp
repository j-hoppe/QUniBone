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

#include <stdint.h>
#include <string>
#include <fstream>
#include <assert.h>

#include "utils.hpp"
#include "storageimage.hpp"
#include "device.hpp"
#include "parameter.hpp"

#include "storagedrive_geometry.hpp"
#include "sharedfilesystem/storageimage_shared.hpp"



enum class drive_type_e {
    NONE = 0,
    TU58, RP0456, RK035, RL01, RL02, RK067, RP023, RM,
    RS, TU56, RX01, RX02, RF,
    // from here only MSCP drives
    RX50, RX33, RD51, RD31, RC25, RC25F,
    RD52, RD32, RD53, RA80, RD54, RA60, RA70,
    RA81, RA82, RA71, RA72, RA90, RA92, RA73
} ;

// helper
class drive_type_c {
public:
    static bool is_RL(enum drive_type_e drive_type) {
        return drive_type == drive_type_e::RL01 || drive_type == drive_type_e::RL02 ;
    }
    static bool is_RX(enum drive_type_e drive_type) {
        return drive_type == drive_type_e::RX01 || drive_type == drive_type_e::RX02 ;
    }

    static bool is_MSCP(enum drive_type_e drive_type) {
        return drive_type >= drive_type_e::RX50 ;
    }
};





class storagecontroller_c;


class storagedrive_c: public device_c {
    friend class storagedrive_selftest_c ;
private:
    uint8_t	zeros[4096] ; // a block of 00s

    // several implementations of the "magnetic surface" possible
    // hide from devices
    storageimage_base_c	*image = nullptr ;

public:
    storagecontroller_c *controller; // link to parent

    // some filesystems need the disk type for their layouts
    enum drive_type_e drive_type ;

    storagedrive_geometry_c geometry ;

    // identifying number at controller
    parameter_unsigned_c unitno = parameter_unsigned_c(this, "unit", "unit", /*readonly*/
                                  true, "", "%d", "Unit # of drive", 3, 10); // 3 bits = 0..7 allowed

    // capacity of medium (disk/tape) in bytes. Info only!
    parameter_unsigned64_c capacity = parameter_unsigned64_c(this, "capacity", "cap", /*readonly*/
                                      true, "byte", "%d", "Storage capacity", 64, 10);

    // if binary image
    parameter_string_c image_filepath = parameter_string_c(this, "image", "img", /*readonly*/
                                        false, "Path to binary image file. Empty to detach. \".gz\" archive also searched.");
    // if shared host dir
//		image_shareddir - path to directory root of shared host file tree
    parameter_string_c image_shareddir = parameter_string_c(this, "shared_dir", "shd", /*readonly*/
                                         false, "Path to directory with shared files. Created on demand, empty to disable sharing.");
    parameter_string_c image_filesystem = parameter_string_c(this, "shared_filesystem", "shfs", /*readonly*/
                                          false, "Encode shared dir in this file system (empty, RT11, XXDP).");

    parameter_unsigned_c activity_led = parameter_unsigned_c(this, "activityled", "al", /*readonly*/
                                        false, "", "%d", "Number of LED to used for activity display.", 8, 10);

    virtual bool on_param_changed(parameter_c *param) override;

//	parameter_bool_c writeprotect = parameter_bool_c(this, "writeprotect", "wp", /*readonly*/false, "Medium is write protected, different reasons") ;

    storagedrive_c(storagecontroller_c *controller);
    virtual ~storagedrive_c() ;



    // wrap actual image driver
//    bool image_create() ;
    bool image_is_param(parameter_c *param) ;
    void image_params_readonly(bool readonly) ;
    bool image_recreate_on_param_change(parameter_c *param) ;
    void image_delete() ;
private:
    bool image_recreate_shared_on_param_change(std::string image_path, std::string filesystem_paramval, std::string shareddir_paramval);

public:
    bool image_open(bool create) ;
    void image_close(void) ;
    bool image_is_open(void) ;
    bool image_is_readonly() ;
    bool image_truncate(void) ;
    uint64_t image_size(void) ;
    void image_read(uint8_t *buffer, uint64_t position, unsigned len) ;
    void image_write(uint8_t *buffer, uint64_t position, unsigned len) ;
    void image_clear_remaining_block_bytes(unsigned block_size_bytes, uint64_t position, unsigned len) ;

    void set_activity_led(bool onoff) ;
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
    storagedrive_selftest_c(const char *_imagefname, unsigned _block_size, unsigned _block_count) :
        storagedrive_c(NULL) {
        assert((block_size % 4) == 0); // whole uint32s
        // this->image_filepath.set(string(imagefname)) ;

        imagefname = _imagefname;
        block_size = _block_size;
        block_count = _block_count;
        image = new storageimage_binfile_c(imagefname) ;

        block_buffer = (uint8_t *) malloc(block_size);
    }
    ~storagedrive_selftest_c() {
        free(block_buffer);
        delete image ;
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
