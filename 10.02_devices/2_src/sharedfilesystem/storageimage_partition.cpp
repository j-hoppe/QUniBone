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
  3. phys blocks to logical blocks: sectors = block = 128 bytes => 4 sectors for one RT11 block.

  Bad sector areas DEC Std144 is also a partition

  Interleave on fictive disk with 9 sectors and single head
  (blocks on other heads may have offset against head 0!)
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

 */
#include "limits.h"
#include "storageimage_shared.hpp"

#include "storageimage_partition.hpp"

namespace sharedfilesystem {

storageimage_partition_c::storageimage_partition_c(storageimage_base_c*_image, uint64_t _image_byte_offset, uint64_t _image_partition_size,
        filesystem_type_e _filesystem_type, drive_info_c _drive_info, unsigned _drive_unit)
{
    image = _image ;
    image_position = _image_byte_offset ;
    size = _image_partition_size ;

    filesystem_type = _filesystem_type ;
    drive_info = _drive_info ;
    drive_unit = _drive_unit ;

    // set later by file system
    block_size = 0 ;
    block_count = 0 ;

    // partition start at sector boundary?
    assert( (image_position % drive_info.sector_size) == 0) ;

    // fits onto disk?
    assert(image_position + size <= drive_info.capacity);

}


storageimage_partition_c::~storageimage_partition_c()
{
}

// geometry set by file system, after construction.
// Also build interleave tables
void storageimage_partition_c::init(unsigned _block_size)
{
    assert(size % _block_size == 0) ; // size in whole blocks
    block_size = _block_size ;
    block_count = size / _block_size ;
    sectors_per_block = block_size / drive_info.sector_size ; // cached

    changed_blocks.assign(block_count, false); // create and clear all flags

    // fill interleave table depending on disk type and filesystem type

    // default: tables empty, no reordering
    log_sector_nr_to_phy.clear() ; // size = 0
    phy_sector_nr_to_log.clear() ;

    // offset sector_nrs by partition image start
    //	unsigned image_position_sector_nr = image_position / drive_info.sector_size ;

    // interleave of 1, all cylinders
    unsigned sector_count = 0 ;

#ifdef INTERLEAVE_IDENTITY_TEST
    sector_count = block_count * sectors_per_block ; // sectors in partition
    // test: make 1:1 map
    phy_sector_nr_to_log.resize(sector_count)		 ;
    for (unsigned phy_sector_nr = 0 ; phy_sector_nr < sector_count ; phy_sector_nr++) {
        phy_sector_nr_to_log[phy_sector_nr] = phy_sector_nr ;
    }
#endif

    sector_count = size / drive_info.sector_size ;
    if (filesystem_type == fst_rt11
            && (drive_info.drive_type ==  devRX01
                || drive_info.drive_type ==  devRX02)) {
        phy_sector_nr_to_log.resize(sector_count) ;
        log_sector_nr_to_phy.resize(sector_count) ;
        // interleave 1: one empty sector between 2 logical ones
        // 6 empty sector after track change
        // http://www.bitsavers.org/pdf/dec/pdp11/rt11/v5.6_Aug91/AA-PE7VA-TC_RT-11_Device_Handlers_Manual_Aug91.pdf
        // Appendix A-4
        // FSX sources: d:\RetroCmp\dec\QUniBone\10.02_devices\1_doc\filesystems\FSX-master\RT11.cs, line #370 ff

        // phy_sector_nr == pattern[log_sector_nr]
        std::vector<unsigned> track_interleave_pattern = {
            0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24,
            1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25
        };
        build_interleave_table(track_interleave_pattern, 6, 0) ;

#ifdef INTERLEAVE_IDENTITY_RL02
    } else if ( drive_info.drive_type ==  devRL02) {
        // TEST: no interleave
        phy_sector_nr_to_log.resize(sector_count) ;
        log_sector_nr_to_phy.resize(sector_count) ;
        std::vector<unsigned> track_interleave_pattern = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                         10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                         20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
                         30, 31, 32, 33, 34, 35, 36, 37, 38, 39
                                                    } ;
        build_interleave_table(track_interleave_pattern, 0, 0) ;

        // verify: 1:1 table generated?
        for (unsigned phy_sector_nr=0 ; phy_sector_nr < sector_count ; phy_sector_nr++) {
            unsigned log_sector_nr = phy_sector_nr_to_log.at(phy_sector_nr) ;
            assert(phy_sector_nr == log_sector_nr) ;
        }

        // its just a test: remove result table, no interleave mapping
        sector_count = 0 ;
        phy_sector_nr_to_log.resize(0) ;
#endif

    } else
        sector_count = 0 ; // no table, 1:1 mapping

    // build the inverted table
    if (sector_count > 0) {
        log_sector_nr_to_phy.resize(sector_count) ;
        for (unsigned phy_sector_nr = 0 ; phy_sector_nr < sector_count ; phy_sector_nr++) {
            unsigned log_sector_nr = phy_sector_nr_to_log[phy_sector_nr] ;
            log_sector_nr_to_phy[log_sector_nr] = phy_sector_nr ;
        }
    }

    // save_to_file("partition.bin") ;
}

// Convert position in partition via partiiton-relative to position to absolute image offset
uint64_t storageimage_partition_c::get_image_position_from_physical_sector_nr(unsigned phy_sector_nr) const
{
    uint64_t byte_offset = phy_sector_nr * drive_info.sector_size ; // offset inside partition
    return image_position + byte_offset ;
}

// Convert image position to position in partition
unsigned storageimage_partition_c::get_physical_sector_nr_from_image_position(uint64_t image_byte_offset) const
{
    uint64_t byte_offset = image_byte_offset - image_position ; // offset inside partition
    return byte_offset / drive_info.sector_size ;
}


// for logical partition blocks, return the non-linear interleaved sectors
// candidate for optimizing, often called.
std::vector<unsigned> storageimage_partition_c::get_physical_sector_nrs_from_blocks(uint32_t _start_block_nr, uint32_t _block_count) const
{
    std::vector<unsigned> result ;
    // iterate over all partition sectors of block range, for each get nr on image
    unsigned log_sector_nr_start = _start_block_nr * sectors_per_block ;
    unsigned log_sector_nr_end = log_sector_nr_start + _block_count * sectors_per_block ;
    for (unsigned log_sector_nr = log_sector_nr_start ; log_sector_nr < log_sector_nr_end ; log_sector_nr++ ) {
        if (log_sector_nr_to_phy.size() == 0)
            result.push_back(log_sector_nr) ; // no table: phy == log
        else
            result.push_back( log_sector_nr_to_phy[log_sector_nr] ) ;
    }
    return result ;
}

// !! The interleaving logic isn't required to be super-performant.
// !! Its only used for small and slow devcies, not RA disks

// read partition blocks to a buffer
void storageimage_partition_c::get_blocks(byte_buffer_c *byte_buffer, uint32_t _start_block_nr, uint32_t _block_count) const
{
    unsigned sector_size = drive_info.sector_size ; //alias

    if (! is_interleaved() ) {
        // no interlacing: sector order in parttion = sector order in image,
        // whole buffer can be read in one stream
        // "partion offset on image" + "offset in partition"
        image->get_bytes(byte_buffer, image_position + _start_block_nr * block_size, _block_count * block_size) ;
        return ;
    }

    // list of physical image sectors to receive data buffer
    std::vector<unsigned> phy_sector_nrs = get_physical_sector_nrs_from_blocks(_start_block_nr, _block_count) ;
    // list of contiguous blocks -> list of non-contiguous sectors

    assert(phy_sector_nrs.size() * sector_size == _block_count * block_size) ;

    byte_buffer->set_size(_block_count * block_size) ;
    uint8_t *wp = byte_buffer->data_ptr() ;
    byte_buffer_c sector_buffer ;
    sector_buffer.set_size(sector_size) ;

    // concat all sectors in non-linear order to buffer
    for (unsigned i=0 ; i < phy_sector_nrs.size() ; i++) {
        // read data into sector buffer, then copy to result
        uint64_t imgpos = get_image_position_from_physical_sector_nr(phy_sector_nrs[i]) ;
        image->get_bytes(&sector_buffer, imgpos, sector_size) ;
        memcpy(wp, sector_buffer.data_ptr(), sector_size) ;
        wp += sector_size ;
    }
}

// write partition blocks to a buffer
void storageimage_partition_c::set_blocks(byte_buffer_c *byte_buffer, uint32_t _start_block_nr)
{
    unsigned sector_size = drive_info.sector_size ; //alias

//    set_bytes(byte_buffer, _start_block_nr * block_size) ;
    if (! is_interleaved() ) {
        image->set_bytes(byte_buffer, image_position + _start_block_nr * block_size) ;
        return ;
    }
    unsigned _block_count = byte_buffer->size() / block_size ;
    assert( byte_buffer->size() % block_size == 0) ;

    // list of physical image sectors to receive data buffer
    std::vector<unsigned> phy_sector_nrs = get_physical_sector_nrs_from_blocks(_start_block_nr, _block_count) ;
    // list of contiguous blocks -> list of non-contiguous sectors

    assert(phy_sector_nrs.size() * drive_info.sector_size == _block_count * block_size) ;

    // concat all sectors in non-linear order to buffer
    uint8_t *rp = byte_buffer->data_ptr() ;
    byte_buffer_c sector_buffer ;
    sector_buffer.set_size(sector_size) ;

    for (unsigned i=0 ; i < phy_sector_nrs.size() ; i++) {
        memcpy(sector_buffer.data_ptr(), rp, sector_size) ;
        uint64_t imgpos = get_image_position_from_physical_sector_nr(phy_sector_nrs[i]) ;
        image->set_bytes(&sector_buffer, imgpos) ;
        rp += sector_size ;
    }
}

// clear sectors on image, possibly non-contiguous by interleaving
void storageimage_partition_c::set_blocks_zero(uint32_t _start_block_nr, uint32_t _block_count)
{
    byte_buffer_c buffer ;
    buffer.init_zero(_block_count *block_size) ;
    set_blocks(&buffer, _start_block_nr) ;
    // do not directly access image ... interleaving
    // image->set_zero(get_image_position_from_physical_sector_nr(_start_block_nr * block_size), _block_count * block_size) ;
}


// the disk driver changed a data block = sector on the image
// result: true => position inside partition, caller should try other partition
bool storageimage_partition_c::on_image_sector_write(uint64_t changed_position)
{
    // changed bytes in this partition ?
    if (changed_position < image_position || changed_position >= (image_position + block_count * block_size))
        return false ;

    // a single byte position maps to a physical image sector,
    // which maps to a partition section via interleaving,
    // which maps to a partition block
    unsigned block_nr ;
    unsigned phy_sector_nr = get_physical_sector_nr_from_image_position(changed_position) ;
    if (phy_sector_nr_to_log.size() == 0)
        block_nr = phy_sector_nr / sectors_per_block ; // no interleaving
    else {
        unsigned log_sector_nr = phy_sector_nr_to_log[phy_sector_nr] ;
        block_nr = log_sector_nr / sectors_per_block ;
    }
    changed_blocks.at(block_nr) = true ;
    return true ;

//    printf("storageimage_partition_c::on_image_sector_write(): image_position =0x%llx, partition block = %s\n",
//           changed_position, block_nr_info(block_nr)) ;
}


// use like: printf("Logical block %s", block_nr_info(block_nr)) ;
// list the physical sectors of a block of image (not: of partition!)
char *storageimage_partition_c::block_nr_info(unsigned block_nr)
{
    static char result[256] ;
    char sector_list_text[256] ;
    // get all physical sectors of block, then offset by partition image start

    unsigned image_position_sector_nr = image_position / drive_info.sector_size ;
    // list of physical sectors, relative to image. not to partition!
    std::vector<unsigned> phy_sector_nrs = get_physical_sector_nrs_from_blocks(block_nr, 1) ;
    for (unsigned i=0 ; i < phy_sector_nrs.size() ; i++)
        phy_sector_nrs[i] += image_position_sector_nr ; // now sectors in whole image

    // "<block> is physical image sectors@offset 12 @ x00d000, 14 @ x00d800, 18 @ x00e000, 20 @ x00e800"
    // comma list of sector numbers
    sector_list_text[0] = 0;
    for (unsigned i=0 ; i < phy_sector_nrs.size() ; i++) {
        char buffer[80] ;
        sprintf(buffer, "%d @ 0x%x", phy_sector_nrs[i], phy_sector_nrs[i] * drive_info.sector_size) ;
        if (i > 0)
            strcat(sector_list_text, ", ") ;
        strcat(sector_list_text, buffer) ;
    }
    if (phy_sector_nrs.size() == 1)
        sprintf(result, "%u is physical image sector %s", block_nr, sector_list_text) ;
    else
        sprintf(result, "%u is physical image sectors %s", block_nr, sector_list_text) ;

    return result ;
}


// return list of physical sectors in image
// short list physical image sectors of many blocks in the image (not: of partition!)
char *storageimage_partition_c::block_nr_list_info(unsigned _start_block_nr, unsigned _block_count)
{
    static char result[256] ;

    unsigned image_position_sector_nr = image_position / drive_info.sector_size ;

    // list of physical sectors, relative to image. not to partition!
    std::vector<unsigned> phy_sector_nrs = get_physical_sector_nrs_from_blocks(_start_block_nr, _block_count) ;
    for (unsigned i=0 ; i < phy_sector_nrs.size() ; i++)
        phy_sector_nrs[i] += image_position_sector_nr ; // now sectors in whole image

    result[0] = 0 ;
    unsigned chars_written = 0 ;
    for (unsigned i=0 ; i < phy_sector_nrs.size() ; i++) {
        char buffer[80] ;
        if (i > 0)
            strcat(result, ", ") ;
        chars_written += 2 ;
        chars_written += sprintf(buffer, "%d", phy_sector_nrs[i]) ;
        assert(chars_written < sizeof(result)) ; // caller must limit blockcount
        strcat(result, buffer) ;
    }

    return result ;
}


/*
  Reorder disk sectors depending on disktype and filesystem
 ! *All* sector numbers (log, phy) are *relative+ to partition start = image-position !

 Parameters:
    sectors_per_track
 	skew = pattern shift for each track (extra timet on head or cylcinder move)
 	interleave = empty phy sectors between to sequential log sectors
 	phy_sector_nr_1st = 1st phy sector of track to use

   Example 1: sector_count = 10, interleave = 1, phy_sector_nr_1st = 0, skew = 0
   log_sector_nr = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
   phy_sector_nr = 0, 2, 4, 6, 8, 1, 3, 5, 7, 9 // empty gaps = 1

   Example 2: sector_count = 10, interleave = 1, phy_sector_nr_1st = 100, skew = 0
   log_sector_nr = 100, 101, 102, 103, 104, 105, 106, 107, 108, 109
   phy_sector_nr = 100, 102, 104, 106, 108, 101, 103, 105, 107, 109 // empty gaps = 1

   Example 3: sector_count = 10, interleave = 1, phy_sector_nr_1st = 0, skew = 1
   log_sector_nr = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
   phy_sector_nr = 1, 3, 5, 7, 9, 2, 4, 6, 8, 0

   Example 4: sector_count = 10, interleave = 2, phy_sector_nr_1st = 0, skew = 0
   log_sector_nr = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
   phy_sector_nr = 0, 7, 4, 1, 8, 5, 2, 9, 6, 3 // empty gaps = 2

*/


// build phy_sector_nr_to_log[] table for all tracks of partition
// input: base layout of logical sector in std track
// cylinder_skew: extra sectors after cylinder change (head assembly move)
// head_skew: extra sectors after head switch on same cylinder
// result: phy_sector_nr_to_log[]
void  storageimage_partition_c::build_interleave_table(
    const std::vector<unsigned> &track_log_to_phy_pattern,
    unsigned cylinder_skew, unsigned head_skew)
{

    unsigned sectors_per_track = drive_info.sector_count ;
    assert(track_log_to_phy_pattern.size() == sectors_per_track) ;

    // which cylinder/head range to itarate
    unsigned cylinder_first, cylinder_last, head_first, head_last, sector, sector_offset ;

    drive_info.get_chs(image_position, &cylinder_first, &head_first, &sector, &sector_offset) ;
    // partition must start at track boundary (not cylinder, STD144)
    assert(sector == 0) ;
    assert(sector_offset == 0) ;

    // get last track
    drive_info.get_chs(image_position + size - 1, &cylinder_last, &head_last, nullptr, nullptr) ;

    unsigned track_skew = 0 ; // each track offset against previous one

    unsigned cyl_image = cylinder_first ; // image absolute
    unsigned head_image = head_first ;

    // iterate all tracks from "first" to "last" disk address
    // stop after cyl_last/head_last
    while ( drive_info.get_track_nr(cyl_image, head_image) <= drive_info.get_track_nr(cylinder_last, head_last)) {

        // 1. offset sector numbers from "track local" to "partition global", fill table
        // track relative to this partition
        unsigned track_nr_partition = (cyl_image - cylinder_first) * drive_info.head_count + head_image - head_first ;

        for (unsigned log_sector_nr_track = 0 ;  log_sector_nr_track < sectors_per_track ; log_sector_nr_track++) {
            unsigned log_sector_nr_partition = track_nr_partition * sectors_per_track + log_sector_nr_track ;
            unsigned phy_sector_nr_partition = track_nr_partition * sectors_per_track + (track_log_to_phy_pattern[log_sector_nr_track] + track_skew) % sectors_per_track;
            log_sector_nr_to_phy.at(log_sector_nr_partition) = phy_sector_nr_partition ; // both tables
            phy_sector_nr_to_log.at(phy_sector_nr_partition) = log_sector_nr_partition ;
        }
        // 2. diagnostic printout
#ifdef INTERLEAVE_DIAGNOSTIC        
        if (! logger->ignored(this, LL_DEBUG)) {
            char buffer1[256], buffer2[256] ;
            buffer1[0] = buffer2[0] = 0 ;
            for (unsigned phy_sector_nr_track=0 ;  phy_sector_nr_track < sectors_per_track ; phy_sector_nr_track++) {
                char buffer[80] ;
                unsigned phy_sector_nr_partition = track_nr_partition * sectors_per_track + phy_sector_nr_track ;
                // unsigned partition_log_sector_nr = phy_sector_nr_1st + (track_log_sector_nr + track_skew)  % sectors_per_track ;
                sprintf(buffer, "%2u ", phy_sector_nr_partition) ;
                strcat (buffer1, buffer) ;
                sprintf(buffer, "%2u ", phy_sector_nr_to_log[phy_sector_nr_partition]) ;
                strcat(buffer2, buffer) ;
            }
            printf("image cyl/head %3d/%-2d: phy sector_nr = %s\n", cyl_image, head_image, buffer1) ;
            printf("                       log sector_nr = %s\n", buffer2) ;
            // DEBUG("image cyl/head %3d/%-2d: phy sector_nr =%s", cyl_image, head_image, buffer1) ;
            // DEBUG("                       log sector_nr =%s", buffer2) ;

            // reverse ordered by log sector
            buffer1[0] = buffer2[0] = 0 ;
            for (unsigned log_sector_nr_track=0 ;  log_sector_nr_track < sectors_per_track ; log_sector_nr_track++) {
                char buffer[80] ;
                unsigned log_sector_nr_partition = track_nr_partition * sectors_per_track + log_sector_nr_track ;
                sprintf(buffer, "%2u ", log_sector_nr_partition) ;
                strcat (buffer1, buffer) ;
                sprintf(buffer, "%2u ", log_sector_nr_to_phy[log_sector_nr_partition]) ;
                strcat(buffer2, buffer) ;
            }
            printf("                       phy sector_nr = %s\n", buffer2) ;
            printf("                       log sector_nr = %s\n", buffer1) ;

        }
#endif
        // step to next track
        head_image++ ;
        if (head_image >= drive_info.head_count) {
            head_image = 0 ;
            cyl_image++ ;
        }

        if (head_image == 0)
            track_skew += cylinder_skew ; // head_image rollaround: move to next cyl_image
        else
            track_skew += head_skew ; // not after track move, we had cylinder_skew already

    }
}

// save logical blockstream to file
// this is what the filesystem sees
void  storageimage_partition_c::save_to_file(std::string _host_filename)
{

    std::string host_filename = absolute_path(&_host_filename) ;
    std::ofstream fout(host_filename, std::ios::binary) ;
    for (unsigned block_nr = 0 ; block_nr < block_count ; block_nr++) {
        byte_buffer_c buffer ;
        get_blocks(&buffer, block_nr, 1) ;
        fout.write((char *)buffer.data_ptr(), buffer.size()) ;
    }
    fout.close() ;
}


} ;

