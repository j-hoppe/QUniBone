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
    devTU58,
    devRP0456,
    devRK035,
    devRL01,
    devRL02,
    devRK067,
    devRP023,
    devRM,
    devRS,
    devTU56,
    devRX01,
    devRX02,
    devRF = 13
} ;


class drive_info_c: logsource_c {
public:
    enum dec_drive_type_e drive_type;
    string 	device_name; // "RL02"
    string 	mnemonic; // "DL"
    uint64_t	capacity ; // full surface
    unsigned heads, cylinders, sectors, sector_size ;
    uint64_t	bad_sector_file_offset ; // optional start of bad sector track

	drive_info_c() {   }
    drive_info_c(enum dec_drive_type_e _drive_type) {
        drive_type = _drive_type ;
        capacity = 0 ; // maybe overwritten by emulation
        bad_sector_file_offset = 0 ; // most drives don't have it

        switch (drive_type) {
        case devRK035:
            device_name = "RK035" ;
            mnemonic = "DK" ;
            cylinders = 400 ; // total 403 tracks, RK05F has 806 tracks
            heads = 1 ;
            sectors = 12 ;
            sector_size = 512 ;
            capacity = cylinders * heads * sectors * sector_size ;
            break ;
        case devRL01:
            device_name = "RL01" ;
            mnemonic = "DL" ;
            cylinders = 256 ; // total 403 tracks, RK05F has 806 tracks
            heads = 2 ;
            sectors = 40 ;
            sector_size = 256 ;
            capacity = cylinders * heads * sectors * sector_size ;
            // last track reserved
            bad_sector_file_offset = (heads*cylinders-1) * sectors * sector_size ;
            break ;
        case devRL02:
            device_name = "RL02" ;
            mnemonic = "DL" ;
            cylinders = 512 ; // total 403 tracks, RK05F has 806 tracks
            heads = 2 ;
            sectors = 40 ;
            sector_size = 256 ;
            capacity = cylinders * heads * sectors * sector_size ;
            // last track reserved
            bad_sector_file_offset = (heads*cylinders-1) * sectors * sector_size ;
            break ;
        case devRX01:
            device_name = "RX01" ;
            mnemonic = "DX" ;
            cylinders = 77 ; // total 403 tracks, RK05F has 806 tracks
            heads = 1 ;
            sectors = 26 ;
            sector_size =128 ;
            capacity = cylinders * heads * sectors * sector_size ;
            break ;
        case devRX02: // RX02 single density is RX01
            device_name = "RX02" ;
            mnemonic = "DY" ;
            cylinders = 77 ; // total 403 tracks, RK05F has 806 tracks
            heads = 1 ;
            sectors = 26 ;
            sector_size = 256 ;
            capacity = cylinders * heads * sectors * sector_size ;
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
        default:
            FATAL("Unhandled drive type") ;
        }
    }


    // return capacity without bad sector area in bytes
    unsigned get_usable_capacity() {
        if (bad_sector_file_offset > 0)
            return bad_sector_file_offset ;
        else
            return capacity ;
    }
};

} // namespace

#endif // _SFS_DRIVEINFO_HPP_
