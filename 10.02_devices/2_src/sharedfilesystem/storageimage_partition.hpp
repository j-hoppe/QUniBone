/* image_partition.hpp - partof image visible to DEC file system

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


  03-nov-2022 JH      created

  A partition is a sub-area on a disk/tape image.
  It prenesents a filesystem a logical block list.
  It has an offset against physical image start, a size,
  and a block_size being a multiple of the images physical block size.

  Examples:
    512 blocks on 256 block devices (RX11 on RX02) ??

    Unix: immer 1k blocks, also miniunix on RX02 256 blocks?

    Bad sector areas DEC Std144 is also a partition

    What about interleaved sectors in physical image?

  TODO:
    block_cache working always on partition???
    layout info of devcies /"blocks:_num, monitor block
    *ALWAYS* in partition units.

 */
#ifndef _STORAGEIMAGE_PARTITION_HPP_
#define _STORAGEIMAGE_PARTITION_HPP_

#include <stdint.h>
#include <vector>

#include <stdint.h>
#include <string.h>
#include <string>
#include <iostream>
#include <assert.h>

#include "logsource.hpp"
#include "bytebuffer.hpp"
#include "driveinfo.hpp"
#include "storageimage.hpp"
#include "filesystem_base.hpp"



namespace sharedfilesystem {


//class storageimage_base_c ;
class storageimage_shared_c ;

class storageimage_partition_c: public logsource_c {
public:
    storageimage_base_c *image ;
    uint64_t image_position; // offset on image in bytes
    uint64_t size ; // len in bytes
    filesystem_type_e filesystem_type  ;

    // logical file system blocks, composed of multiple physical disk blocks
    unsigned block_size ; // must be a multiple of physical image->block_size
    unsigned block_count ; // size is block_size * block_count

    storageimage_partition_c(storageimage_base_c *image, uint64_t _image_byte_offset, uint64_t _image_partition_size,
                             filesystem_type_e _filesystem_type, drive_info_c _drive_info, unsigned drive_unit) ;
    ~storageimage_partition_c()	;

    drive_info_c drive_info ; // copy from image
    unsigned drive_unit ;

    void init(unsigned _block_size) ;

    /*
        // convert position on partition to image
        unsigned get_image_byte_position(unsigned block_nr) const {
            return image_position + block_nr * block_size ;
        }
    */
    std::vector<bool> changed_blocks ;

    bool on_image_sector_write(uint64_t byte_offset) ;
    void clear_changed_flags() {
        changed_blocks.assign(changed_blocks.size(), false);
    }

    uint64_t get_image_position_from_physical_sector_nr(unsigned phy_sector_nr) const ;
    unsigned get_physical_sector_nr_from_image_position(uint64_t image_byte_offset) const ;

    std::vector<unsigned> get_physical_sector_nrs_from_blocks(uint32_t _start_block_nr, uint32_t _block_count) const ;

    void get_blocks(byte_buffer_c *byte_buffer, uint32_t _start_block_nr, uint32_t _block_count) const ;
    void set_blocks(byte_buffer_c *byte_buffer, uint32_t _start_block_nr) ;

    void set_blocks_zero(uint32_t _start_block_nr, uint32_t _block_count) ; // clear area

    char *block_nr_info(unsigned block_nr) ;
    char *block_nr_list_info(unsigned _start_block_nr, unsigned _block_count) ;
	

	bool is_interleaved() const {
			return log_sector_nr_to_phy.size() > 0 ;
	}

private:
    unsigned sectors_per_block ; // # of image sector for a partition block

    // index: linear logical nr of sector on disk
    // result: interleaved physical nr of that sector in the image
    // all sector_nr relative to partition start, not to image start.
    std::vector<unsigned> log_sector_nr_to_phy ;

    // reverse table: for each sector in the image, given
    // index: interleaved physical nr of that sector in the image
    // result: linear logical nr of sector on disk
    std::vector<unsigned> phy_sector_nr_to_log ;

    void  build_interleave_table(const std::vector<unsigned> &track_phy_to_log_pattern,   unsigned cylinder_skew, unsigned head_skew) ;
	
	void  save_to_file(std::string _host_filename) ;

} ;

}

#endif // _STORAGEIMAGE_PARTITION_HPP_


