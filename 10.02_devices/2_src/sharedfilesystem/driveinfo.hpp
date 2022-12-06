/* driveinfo.hpp - basic data about disk drives

  Copyright (c) 2022, Joerg Hoppe
  j_hoppe@t-online.de, www.retrocmp.com

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

  Filesystem related drive database

  06-jan-2022 JH      created
 */
#ifndef _SFS_DRIVEINFO_HPP_
#define _SFS_DRIVEINFO_HPP_

#include <stdint.h>
#include <assert.h>
#include "logger.hpp"

namespace sharedfilesystem {

enum dec_drive_type_e {
    devNONE = 0,
    devTU58, devRP0456, devRK035, devRL01, devRL02, devRK067, devRP023,  devRM,
    devRS, devTU56, devRX01, devRX02, devRF,
    devRX50, devRX33, devRD51, devRD31, devRC25, devRC25F,
    devRD52, devRD32, devRD53, devRA80, devRD54, devRA60, devRA70,
    devRA81, devRA82, devRA71, devRA72, devRA90, devRA92, devRA73
} ;


class drive_info_c: logsource_c {
public:
    enum dec_drive_type_e drive_type;
    std::string 	device_name; // "RL02"
    std::string 	mnemonic; // "DL"
    uint64_t capacity ; // full surface
    unsigned cylinder_count, head_count, sector_count, sector_size ;
    uint64_t bad_sector_file_offset ; // optional start of bad sector track
    unsigned mscp_block_count ;
    unsigned mscp_rct_size ;  // Replacement and Caching Table

    drive_info_c() {   }
    drive_info_c(enum dec_drive_type_e _drive_type) {
        drive_type = _drive_type ;
        capacity = 0 ; // maybe overwritten by emulation
        bad_sector_file_offset = 0 ; // most drives don't have it
        mscp_block_count = 0 ;
        mscp_rct_size = 0 ;

        switch (drive_type) {
        case devRK035:
            device_name = "RK035" ;
            mnemonic = "DK" ;
            cylinder_count = 400 ; // total 403 tracks, RK05F has 806 tracks
            head_count = 1 ;
            sector_count = 12 ;
            sector_size = 512 ;
            capacity = cylinder_count * head_count * sector_count * sector_size ;
            break ;
        case devRL01:
            device_name = "RL01" ;
            mnemonic = "DL" ;
            cylinder_count = 256 ; // total 403 tracks, RK05F has 806 tracks
            head_count = 2 ;
            sector_count = 40 ;
            sector_size = 256 ;
            capacity = cylinder_count * head_count * sector_count * sector_size ;
            // last track reserved
            bad_sector_file_offset = (head_count*cylinder_count-1) * sector_count * sector_size ;
            break ;
        case devRL02:
            device_name = "RL02" ;
            mnemonic = "DL" ;
            cylinder_count = 512 ; // total 403 tracks, RK05F has 806 tracks
            head_count = 2 ;
            sector_count = 40 ;
            sector_size = 256 ;
            capacity = cylinder_count * head_count * sector_count * sector_size ;
            // last track reserved
            bad_sector_file_offset = (head_count*cylinder_count-1) * sector_count * sector_size ;
            break ;
        case devRX01:
            device_name = "RX01" ;
            mnemonic = "DX" ;
            cylinder_count = 77 ; // total 403 tracks, RK05F has 806 tracks
            head_count = 1 ;
            sector_count = 26 ;
            sector_size =128 ;
            capacity = cylinder_count * head_count * sector_count * sector_size ;
            break ;
        case devRX02: // RX02 single density is RX01
            device_name = "RX02" ;
            mnemonic = "DY" ;
            cylinder_count = 77 ; // total 403 tracks, RK05F has 806 tracks
            head_count = 1 ;
            sector_count = 26 ;
            sector_size = 256 ;
            capacity = cylinder_count * head_count * sector_count * sector_size ;
            break ;


            /*
            				case devTU58: // tag
            					device_name = "TU58" ;
            					mnemonic = "DD" ;
            					break ;
            				case devRP0456:
            					device_name = "RP0456" ;
            					mnemonic = "DB" ;
            					break ;
                    case devRK067:
                        device_name = "RK067" ;
                        mnemonic = "DM" ;
                        bad_sector_file_offset
                        break ;
                        		case RP023:
                        		device_name = "RP02,3" ;
                        		mnemonic = "DP" ;
                        		break ;
                    case devRM:
                        device_name = "RM03" ;
                        mnemonic = "DR" ;
                        break ;
                    case devRS:
                        device_name = "RS034" ;
                        mnemonic = "DS" ;
                        break ;
                    case devTU56:
                        device_name = "TU56" ; // DECTAPE
                        mnemonic = "DT" ;
                        break ;
                        	case devRF:
                        		device_name = "RF11" ;
                        		mnemonic = "RF" ;
                        		break ;
                        */
            // Duplicate of list from mscp_drive.hpp, DriveInfo g_driveTable[21] { ... }
        case devRX50:
            device_name = mnemonic = "RX50" ;
            mscp_block_count = 800 ;
            mscp_rct_size = 0 ;
            break ;
        case devRX33:
            device_name = mnemonic = "RX33" ;
            mscp_block_count = 2400 ;
            mscp_rct_size = 0 ;
            break ;
        case devRD51:
            device_name = mnemonic = "RD51" ;
            mscp_block_count = 21600 ;
            mscp_rct_size = 36 ;
            break ;
        case devRD31:
            device_name = mnemonic = "RD31" ;
            mscp_block_count = 41560 ;
            mscp_rct_size = 3 ;
            break ;
        case devRC25:
            device_name = mnemonic = "RD251" ;
            mscp_block_count = 50902 ;
            mscp_rct_size = 0 ;
            break ;
        case devRC25F:
            device_name = mnemonic = "RD25F" ;
            mscp_block_count = 50902 ;
            mscp_rct_size = 0 ;
            break ;
        case devRD52:
            device_name = mnemonic = "RD52" ;
            mscp_block_count = 60480 ;
            mscp_rct_size = 4 ;
            break ;
        case devRD32:
            device_name = mnemonic = "RD32" ;
            mscp_block_count = 83236 ;
            mscp_rct_size = 4 ;
            break ;
        case devRD53:
            device_name = mnemonic = "RD53" ;
            mscp_block_count = 138672 ;
            mscp_rct_size = 5 ;
            break ;
        case devRA80:
            device_name = mnemonic = "RD80" ;
            mscp_block_count = 237212 ;
            mscp_rct_size = 0 ;
            break ;
        case devRD54:
            device_name = mnemonic = "RD53" ;
            mscp_block_count = 311200 ;
            mscp_rct_size = 7 ;
            break ;
        case devRA60:
            device_name = mnemonic = "RD60" ;
            mscp_block_count = 400176 ;
            mscp_rct_size = 1008 ;
            break ;
        case devRA70:
            device_name = mnemonic = "RD70" ;
            mscp_block_count = 547041 ;
            mscp_rct_size = 198 ;
            break ;
        case devRA81:
            device_name = mnemonic = "RD81" ;
            mscp_block_count = 891072 ;
            mscp_rct_size = 2856 ;
            break ;
        case devRA82:
            device_name = mnemonic = "RD82" ;
            mscp_block_count = 1216665 ;
            mscp_rct_size = 3420 ;
            break ;
        case devRA71:
            device_name = mnemonic = "RD71" ;
            mscp_block_count = 1367310 ;
            mscp_rct_size = 1428 ;
            break ;
        case devRA72:
            device_name = mnemonic = "RD72" ;
            mscp_block_count = 1953300 ;
            mscp_rct_size = 2040 ;
            break ;
        case devRA90:
            device_name = mnemonic = "RD72" ;
            mscp_block_count = 2376153 ;
            mscp_rct_size = 1794 ;
            break ;
        case devRA92:
            device_name = mnemonic = "RD92" ;
            mscp_block_count = 2940951 ;
            mscp_rct_size = 949 ;
            break ;
        case devRA73:
            device_name = mnemonic = "RD73" ;
            mscp_block_count = 3920490 ;
            mscp_rct_size = 198 ;
            break ;
        default:
            FATAL("Unhandled drive type") ;
        }
        if (mscp_block_count > 0)
            capacity = mscp_block_count * 512 ; // all MSCP drives
    }


  //
    uint64_t get_track_capacity() const {
        return sector_count * sector_size ;
    }

    uint64_t get_cylinder_capacity() const {
        return head_count * get_track_capacity() ;
    }

    unsigned get_track_nr(unsigned cylinder, unsigned head) const
    {
        return cylinder * head_count + head ;
    }


    // for a gicen position in the image, return cylinder, head within cylinder,
    // sector within track and offset within sector
    void get_chs(uint64_t image_offset, unsigned *cylinder, unsigned *head, unsigned *sector, unsigned *sector_offset) const
    {
        if (sector_offset != nullptr)
            *sector_offset = image_offset % sector_size ;
        unsigned image_sector_nr = image_offset / sector_size ;
        if (sector != nullptr)
            *sector = image_sector_nr % sector_count ;
        unsigned image_track_nr = image_sector_nr / sector_count ;
        if (head != nullptr)
            *head =image_track_nr % head_count ;
        unsigned cylinder_nr = image_track_nr / head_count ;
        if (cylinder != nullptr)
            *cylinder = cylinder_nr ;
    }

};

} // namespace

#endif // _SFS_DRIVEINFO_HPP_
