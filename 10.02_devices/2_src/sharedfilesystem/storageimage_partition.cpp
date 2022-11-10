/* image_partition.cpp - part of image visible to DEC file system

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
  It has an block offset against image start, a size,
  and a logical filesystem block_size being a multiple of the images physical block size.

  It serializes also physical sector interleave on the image.

  Worst case: RX01
  1. offset: 1st Track unused -> partition offset against image.
  2. Code interleave: 26 sectors à 128 with interleave
  3. phys blocks to logical blcoks: sectors = block = 128 bytes => 4 sectors for one RT11 block.

 Bad sector areas DEC Std144 is also a partition

  Interleave on fictive disk with 9 sectors and single head
  (blocks on other heads may have offset agaisnt head 0!)
  physical block size = 128
  sector layout in image
  physical block image offset: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
            physical block nr: 0, 5, 1, 6, 2, 7, 3, 8, 4, 9

  Solution: tables to transform "block image offset" into "block numbers"

  interleave_phys_block_offset_to_nr[offset] = [0, 5, 1 ,6, 2, 7, 3, 8, 4, 9]
  interleave_phys_block_nr_to_offset[nr] = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]
  Table contains pattern for all blocks on disk, not just single cylinder.

  Interleave tables are build by partition dependent on drivetype and filesystem
  void build_interlave_table(drive_info_c *drive_info, filesystem_type_e filesystem_type) {... }

  Biggest case: RL02: 512*40 =  20K blocks. Bigger MSCP disk need no tables (no visible interleave)

  TODO:
    layout info of devcies /"blocks:_num, monitor block
    *ALWAYS* in partition units.

 */
#include "storageimage_shared.hpp"

#include "storageimage_partition.hpp"

namespace sharedfilesystem {

storageimage_partition_c::storageimage_partition_c(storageimage_shared_c*_image, uint64_t _image_byte_offset, uint64_t _image_partition_size,
        unsigned _drive_unit)
{
    image = _image ;
    image_position = _image_byte_offset ;
    size = _image_partition_size ;

    drive_info = image->drive_info ; // copy from image
    drive_unit = _drive_unit ;

    // set later by file system
    block_size = 0 ;
    block_count = 0 ;

    // fits onto disk?
    assert(image_position + size <= image->drive_info.capacity);
}


storageimage_partition_c::~storageimage_partition_c()
{
}

// geometry set by file system, after construction
void storageimage_partition_c::set_block_size(unsigned _block_size)
{
    assert(size % _block_size == 0) ; // size in whole blocks
    block_size = _block_size ;
    block_count = size / _block_size ;
    changed_blocks.assign(block_count, false); // create and clear all flags
}

// Convert position in partition to position in image
// TODO: sector interleave!
uint64_t storageimage_partition_c::get_image_position(uint64_t byte_offset) const
{
    return image_position + byte_offset ;
}

// Convert image position to position in partition
// TODO: sector interleave!
uint64_t storageimage_partition_c::get_position_from_image(uint64_t image_byte_offset) const
{
    return image_byte_offset - image_position ;
}


// get bytes from the partition
// access underlying image
// TODO: sector interleave?!
void storageimage_partition_c::get_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset, uint32_t data_size) const
{
    // "partion offset on image" + "offset in partition"
    image->get_bytes(byte_buffer, get_image_position(byte_offset), data_size) ;
}

// write bytes to the partition
// access underlying image
// TODO: sector interleave?!
void storageimage_partition_c::set_bytes(byte_buffer_c *byte_buffer, uint64_t byte_offset)
{
    image->set_bytes(byte_buffer, get_image_position(byte_offset)) ;
}


// again, using block addresses
void storageimage_partition_c::get_blocks(byte_buffer_c *byte_buffer, uint32_t _start_block_nr, uint32_t _block_count) const
{
    get_bytes(byte_buffer, _start_block_nr * block_size, _block_count * block_size) ;
}

void storageimage_partition_c::set_blocks(byte_buffer_c *byte_buffer, uint32_t _start_block_nr)
{
    set_bytes(byte_buffer, _start_block_nr * block_size) ;
}

// clear area
// TODO: interleave
void storageimage_partition_c::set_blocks_zero(uint32_t _start_block_nr, uint32_t _block_count)
{
    image->set_zero(get_image_position(_start_block_nr * block_size), _block_count * block_size) ;
}


// the disk driver changed a data block on the image, block = sector
void storageimage_partition_c::on_image_block_write(uint64_t changed_position) {
    assert(changed_position >= image_position) ;
    assert(changed_position < image_position + block_count * block_size) ;
    // TODO: interleave
    unsigned block_nr = get_position_from_image(changed_position) / block_size ;
    changed_blocks.at(block_nr) = true ;
//    printf("storageimage_partition_c::on_image_block_write(): image_position =0x%llx, partition block = %s\n",
//           changed_position, block_nr_info(block_nr)) ;
}


// use like: printf("Logical block %s", block_nr_info(block_nr)) ;
char *storageimage_partition_c::block_nr_info(unsigned block_nr)
{
    static char buffer[80] ;
    uint64_t image_first_block_position = get_image_position(block_nr * block_size) ;
    uint64_t image_last_block_position = get_image_position((block_nr+1) * block_size) -1 ;
    unsigned image_first_block_nr = image_first_block_position / drive_info.sector_size ;
    unsigned image_last_block_nr = image_last_block_position / drive_info.sector_size ;
    if (image_first_block_nr == image_last_block_nr) {
        // logical block maps to one physical block
        sprintf(buffer, "%u is device block %u at image[0x%llx-0x%llx]", block_nr,
                image_first_block_nr, image_first_block_position, image_last_block_position) ;
    } else {
        // logical block maps to multiple physical blocks
        sprintf(buffer, "%u is device blocks %u-%u at image[0x%llx-0x%llx]", block_nr,
                image_first_block_nr, image_last_block_nr,  image_first_block_position, image_last_block_position) ;
    }
    return buffer ;
}

/*
	interleave_phys_block_offset_to_nr[offset] = [0, 5, 1 ,6, 2, 7, 3, 8, 4, 9]
	interleave_phys_block_nr_to_offset[nr] = [0, 2, 4, 6, 8, 1, 3, 5, 7, 9]

// linear logical blockstream to interleaved physical blocks
void set_byte(byte_buffer, log_block_offset, log_block_count ) {
	if (! interleaved) {
		// write all blocks in one call
		uint64_t image_byte_offset = image_position + byte_offset ;
		image->set_bytes(byte_buffer, image_byte_offset) ;
		return ;
	}

	// write all physical blocks
	bytes_to_write = byte_buffer->size() ;
	rp = byte_buffer ; // read pointer
	phys_block_size = image->drive_info->sector_size
	assert(block_size % phys_block_size == 0) ;
	phys_blocks_per_log_block = block_size / phys_block_size ;

	phys_block_nr = log_block_nr * pyhs_blockg_per_log_block // start
	// all physical blocks
	while(bytes_to_write > 0) {
		// write a single physical block
		assert(phys_block_nr * phys_block_size < image->capacity) ;
		// calc position of physical block in image
		image_position = image_offset + phys_block_size * phys_block_nr_to_offset[phys_block_nr] ;
		write(rp, image_position, phys_block_size ) ;
		rp += phys_block_size ;
		phys_block_nr ++ ;
		bytes_to_write -= phys_block_size ;
	}

}

	*/


} ;

