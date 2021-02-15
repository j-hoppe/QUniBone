/* rx0102drive.cpp: implementation of RX01/RX02 disk drive, attached to RX0102 uCPU

 Copyright (c) 2020, Joerg Hoppe
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


 5-jan-2020	JH      start

 The microCPU board contains all logic and state for the RX01/02 subsystem.
 It is connected on one side two to "dump" electro-mechanical drives,
 on the other side two a RX11/RXV11/RX211/RXV21 UNIBUS/QBUS interface.
 */

#include <assert.h>

#include <array>

using namespace std;

#include "logger.hpp"
#include "timeout.hpp"
#include "utils.hpp"
//#include "rx11.hpp"
#include "rx0102ucpu.hpp"
#include "rx0102drive.hpp"



RX0102drive_c::RX0102drive_c(RX0102uCPU_c *uCPU) :
    storagedrive_c(nullptr) { // no controller assigned, command by box uCPU
    this->uCPU = uCPU ; // link to micro CPU board
    log_label = "RXDRV"; // to be overwritten by RX11 on create
    type_name.set("RX01") ;

    // some rxdp floppy images start at track #1 instead of #0
    track0image.value = true ;

    // "enable" and power switch are controlled by uCPU in case box
    enabled.readonly = true ;
}

// return false, if illegal parameter value.
// verify "new_value", must output error messages
bool RX0102drive_c::on_param_changed(parameter_c *param) {
    if (param == &density_name) {
        if (!strcasecmp(density_name.new_value.c_str(), "SD"))
            set_density(0);
        else if (!strcasecmp(density_name.new_value.c_str(), "DD"))
            set_density(1);
        else {
//		throw bad_parameter_check("drive type must be RX01 or RX02") ;
            ERROR("drive density SD or DD");
            return false;
        }
    } else if (param == &image_filepath) {
        // change of file image changes state
        if (file_is_open())
            file_close();
        // assume new "floppy" has no delete marks set
        memset(deleted_data_marks, 0, sizeof(deleted_data_marks)) ;
        file_open(image_filepath.new_value, true) ; // may fail
    }
    return storagedrive_c::on_param_changed(param); // more actions (for enable)
}

void RX0102drive_c::set_density(uint8_t density) {
    this->density = density;
    switch (density) {
    case 0: // RX01, RX02 FM encoding
        sector_size_bytes = block_size_bytes = 128; // in byte
        density_name.value = "SD"; // no "on change" callbacks
        break;
    case 1: // RX02 MFM encoding = Double Density floppy
        sector_size_bytes = block_size_bytes = 256; // in byte
        density_name.value = "DD"; // no "on change" callbacks
        // density_name.set("DD");
    }
    block_count = cylinder_count * sector_count;
    capacity.value = block_size_bytes * block_count;

//    uCPU->on_drive_state_changed(this) ;
}

// drive contains Double density floppy?
bool RX0102drive_c::get_density(void) {
    // RX02DD ?
    return (density == 1) ;
}

// uCPU may query the "ready/ door open state"
// true: file image loaded: "door closed" + "floppy inserted"
bool RX0102drive_c::check_ready(void) {
    return file_is_open() ;
}


void RX0102drive_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) {
    UNUSED(dclo_edge) ;
    UNUSED(aclo_edge) ;
    uCPU->on_drive_state_changed(this) ; // not needed, uCPU controls power
}

// if seeking or ontrack: retrack head to 0
void RX0102drive_c::on_init_changed(void) {
}

bool RX0102drive_c::check_disk_address(unsigned track, unsigned sector) {
    error_illegal_track = (track >= cylinder_count) ;
    error_illegal_sector = (sector < 1 || sector > sector_count) ;
    return !error_illegal_track && !error_illegal_sector ;
}

// sector offset in image file in bytes
int RX0102drive_c::get_sector_image_offset(unsigned track, unsigned sector) {
    return ((track * sector_count) + (sector-1)) * sector_size_bytes ;
}

// false: error
bool RX0102drive_c::sector_read(uint8_t *sector_buffer, bool *deleted_data_mark, unsigned track, unsigned sector) {
    if (!check_disk_address(track,sector))
        return false ;
    if (!check_ready())
        return false ; // no floppy image

    // wait for 1 sector to pass, else ZRXB failures
    unsigned rotation_ms =  (1000*60) / full_rpm ; // time for index hole to pass
    unsigned sector_millis = rotation_ms / sector_count ; // about 6.5 ms
    timeout_c().wait_ms(sector_millis / emulation_speed.value) ;

    *deleted_data_mark = deleted_data_marks[track][sector] ;

    if (! track0image.value) {
        // file image does not contain track 0: skip it
        if (track == 0) {
            memset(sector_buffer, 0, sector_size_bytes);
            return true ;
        } else {
            track-- ;
        }
    }

    int offset = get_sector_image_offset(track, sector) ;
    file_read(sector_buffer, (unsigned) offset, sector_size_bytes) ;
    return true ;
}

// false: error
bool RX0102drive_c::sector_write(uint8_t *sector_buffer, bool deleted_data_mark, unsigned track, unsigned sector) {
    if (!check_disk_address(track,sector))
        return false ;
    if (!check_ready())
        return false ; // no floppy image

    if (file_readonly) {
        // No means to detect write-protected floppies?
        WARNING("Write access to readonly floppy image file ignored") ;
        return true ;
    }

    // wait for 1 sector to pass, else ZRXB failures
    unsigned rotation_ms =  (1000*60) / full_rpm ; // time for index hole to pass
    unsigned sector_millis = rotation_ms / sector_count ; // about 6.5 ms
    timeout_c().wait_ms(sector_millis / emulation_speed.value) ;

    deleted_data_marks[track][sector] = deleted_data_mark ;

    if (!track0image.value) {
        // file image does not contain track 0: skip it

        if (track == 0) {
            // do not write to ignored track
            return true ;
        } else {
            track-- ;
        }
    }
    int offset = get_sector_image_offset(track, sector) ;
    file_write(sector_buffer, (unsigned) offset, sector_size_bytes) ;
    return true ;
}


// no thread, just passive mechnical device
void RX0102drive_c::worker(unsigned instance) {
    UNUSED(instance); // only one
}

