/* storagedrive_geometry.hpp - data about disk drive cylinders, tracks and sectors

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

  Uniform data set for all storage drives, also used by image_partition

  21-dec-2022 JH      created
 */
#ifndef _STORAGEDRIVE_GEOMETRY_HPP_
#define _STORAGEDRIVE_GEOMETRY_HPP_

#include <stdint.h>
#include <assert.h>
#include "logger.hpp"

//
class storagedrive_geometry_c: logsource_c {
public:
    unsigned cylinder_count, head_count, sector_count ;
    unsigned sector_size_bytes ;
    uint64_t bad_sector_file_offset ; // optional start of bad sector track

    unsigned mscp_block_count ; // cyl/head/sector struct hidden by MSCP drives

    // for storageimage_shared:
    // RX01,02 may have has unused track #0
    uint64_t filesystem_offset ;


    storagedrive_geometry_c() {
        cylinder_count = head_count = sector_count = 0 ;
        sector_size_bytes = 0 ;
        bad_sector_file_offset = 0 ; // most drives don't have it
        mscp_block_count = 0 ;
        filesystem_offset = 0 ;
    }

    // full surface, including reserved tracks
    uint64_t get_raw_capacity() const {
        if (mscp_block_count > 0)
            return mscp_block_count * sector_size_bytes ;
        else
            return cylinder_count * head_count * sector_count * sector_size_bytes ;
    }


    unsigned get_block_count() const {
        if (mscp_block_count > 0)
            return mscp_block_count ;
        else
            return cylinder_count * head_count * sector_count ;
    }

    //
    uint64_t get_track_capacity() const {
        return sector_count * sector_size_bytes ;
    }

    uint64_t get_cylinder_capacity() const {
        return head_count * get_track_capacity() ;
    }

    unsigned get_track_nr(unsigned cylinder, unsigned head) const
    {
        return cylinder * head_count + head ;
    }

    uint64_t get_image_offset(unsigned cylinder, unsigned head, unsigned sector) const
    {
        return sector_size_bytes  * (
                   (cylinder * head_count * sector_count) + (head * sector_count)  + sector
               );
    }


    // for a given position in the image, return cylinder, head within cylinder,
    // sector within track and offset within sector
    void get_chs(uint64_t image_offset, unsigned *cylinder, unsigned *head, unsigned *sector, unsigned *sector_offset) const
    {
        if (sector_offset != nullptr)
            *sector_offset = image_offset % sector_size_bytes ;
        unsigned image_sector_nr = image_offset / sector_size_bytes ;
        if (sector != nullptr)
            *sector = image_sector_nr % sector_count ;
        unsigned image_track_nr = image_sector_nr / sector_count ;
        if (head != nullptr)
            *head =image_track_nr % head_count ;
        unsigned cylinder_nr = image_track_nr / head_count ;
        if (cylinder != nullptr)
            *cylinder = cylinder_nr ;
    }
} ;


#endif // _STORAGEDRIVE_GEOMETRY_HPP_
