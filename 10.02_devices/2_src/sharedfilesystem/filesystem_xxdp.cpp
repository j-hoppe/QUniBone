/* filesystem_xxdp.cpp - XXDP file system


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

  06-jan-2022 JH      created from tu58fs

 The logical structure of the DOS-11 file system
 is represented by linked data structures.
 The logical filesystem is indepented of the physical image,
 file content and blocklist are hold in own buffers.

 API
  _init():
 Clear all data from the filesystem and preload
 layout parameters (length of preallocated areas, interleave etc.)
 from device-specific "Random Access Device Information" table.

 _parse():
  build the logical filesystem from a physical binary image.
  After that, user files and boot files can be read.

  _add_boot(), _add_file():
  add boot files and user files to the logical image,
  (allocate blocks and update blocklists)

  _render()
  Produce the binary image from logical filesystem.


 */

#include <string.h>
#include <inttypes.h> // PRI* formats

#include "logger.hpp"

#include "filesystem_xxdp.hpp"


// pseudo file for volume parameters
#define XXDP_VOLUMEINFO_BASENAME  "$VOLUM" // valid XXDP file name
#define XXDP_VOLUMEINFO_EXT 	  "INF"
// boot block and monitor blocks are pseudo files
#define XXDP_BOOTBLOCK_BASENAME   "$BOOT"  // valid XXDP file names
#define XXDP_BOOTBLOCK_EXT		  "BLK"
#define XXDP_MONITOR_BASENAME 	  "$MONI" // valid XXDP file names
#define XXDP_MONITOR_EXT		  "TOR"





namespace sharedfilesystem {

file_xxdp_c::file_xxdp_c() :file_dec_stream_c(this, "")
{
    basename = "" ;
    ext= "" ;
}

// clone constructor. only metadata
file_xxdp_c::file_xxdp_c(file_xxdp_c *f) : file_base_c(f), file_dec_c(f), file_dec_stream_c(this,"")
{
    basename = f->basename ;
    ext = f->ext ;
//	internal = f->internal ;
}

file_xxdp_c::~file_xxdp_c() {
    // host_file_streams[] automatically destroyed
}


// NAME.EXT
string file_xxdp_c::get_filename()
{
    return filesystem_xxdp_c::make_filename(basename, ext) ;
}


string file_xxdp_c::get_host_path()
{
    // let host build the linux path, using my get_filename()
    // result is just "/filename"
    return filesystem_host_c::get_host_path(file) ;
}

// have file attributes or data content changed?
// filename not compared, speed!
// Writes to the image set the change flag
// this->has_changed(cmp) != cmp->has_changed(this)
bool file_xxdp_c::data_changed(file_base_c *_cmp)
{
    auto cmp = dynamic_cast <file_xxdp_c*>(_cmp) ;
    return changed
//			basename.compare(cmp->basename) != 0
//			|| ext.compare(cmp->ext) != 0
           ||memcmp(&modification_time, &cmp->modification_time, sizeof(modification_time)) // faster
           //	|| mktime(modification_time) == mktime(cmp->modification_time)
           || readonly != cmp->readonly
           || file_size != cmp->file_size ;
}



// dir::copy_metadata_to() not polymorph .. limits of C++ or my wisdom
// instances for each filesystem, only rt11 and xxdp, not needed for host
void directory_xxdp_c::copy_metadata_to(directory_base_c *_other_dir)
{
    auto other_dir = dynamic_cast<directory_xxdp_c *>(_other_dir) ;
    // start condition: other_dir already updated ... recursive by clone-constructor
    assert(other_dir != nullptr) ;

    // directory recurse not necessary for RT11 ... but this may serve as template
    for(unsigned i = 0; i < subdirectories.size(); ++i) {
        auto subdir = dynamic_cast<directory_xxdp_c *>(subdirectories[i]) ;
        // add to other filesystem a new copy-instance
        other_dir->filesystem->add_directory(other_dir, new directory_xxdp_c(subdir));
    }
    for(unsigned i = 0; i < files.size(); ++i) {
        auto f  = dynamic_cast<file_xxdp_c *>(files[i]) ;
        other_dir->add_file(new file_xxdp_c(f)) ;
    }
}



static int leapyear(int y) {
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static int monthlen_noleap[] = { 31, 28, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static int monthlen_leap[] = { 31, 29, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

// convert a DOS-11 data to time_t
// day = 5 bits, month= 4bits, year = 9bits
struct tm filesystem_xxdp_c::dos11date_decode(uint16_t w)
{
    int *monthlen;
    struct tm result;
    int y = (w / 1000) + 1970;
    int m;
    int d = w % 1000; // starts as day of year

    memset(&result, 0, sizeof(result)); //clear

    // use correct table
    monthlen = leapyear(y) ? monthlen_leap : monthlen_noleap;

    m = 0;
    while (d > monthlen[m]) {
        d -= monthlen[m];
        m++;
    }
    result.tm_year = y - 1900;
    result.tm_mon = m; // 0..11
    result.tm_mday = d; // 1..31

    assert(w == dos11date_encode(result)); // cross check
    return result;
}


// year after allowed: trunc to this.
int dos11date_overflow_year = 1999;
uint16_t filesystem_xxdp_c::dos11date_encode(struct tm t)
{
    int *monthlen;
    uint16_t result = 0;
    int doy;
    int y = 1900 + t.tm_year; // year is easy
    int m;

    monthlen = leapyear(y) ? monthlen_leap : monthlen_noleap;

    for (doy = m = 0; m < t.tm_mon; m++)
        doy += monthlen[m];

    result = doy + t.tm_mday;
    result += 1000 * (y - 1970);
    return result;
}

// join basename and ext
// with "." on empty extension "FILE."
// used as key for file map
string filesystem_xxdp_c::make_filename(string& basename, string& ext)
{
    string result = trim_copy(basename);
    result.append(".") ;
    result.append(trim_copy(ext)) ;
    std::transform(result.begin(), result.end(),result.begin(), ::toupper);
    return result ;
}


filesystem_xxdp_c::filesystem_xxdp_c(drive_info_c &_drive_info,
                                     storageimage_base_c *_image_partition, uint64_t _image_partition_size)
    : filesystem_dec_c(_drive_info, _image_partition, _image_partition_size)
{
    layout_info = get_documented_layout_info(_drive_info.drive_type) ;
    changed_blocks = new boolarray_c(layout_info.blocks_num) ;


    // create root dir.
    add_directory(nullptr, new directory_xxdp_c() ) ;
    assert(rootdir->filesystem == this) ;
//    rootdir = new directory_xxdp_c() ; // simple list of files
//    rootdir->filesystem = this ;

    // boot block, monitor_core_image, volume info
    special_file_count = 3 ;

    // sort order for files. For regexes the . must be escaped by \.
    // and a * is .*"
    sort_group_regexes.reserve(20) ; // speed up, no relloc
    sort_add_group_pattern("XXDPSM\\.SYS") ;
    sort_add_group_pattern("XXDPXM\\.SYS") ;
    sort_add_group_pattern("DRSSM\\.SYS") ;
    sort_add_group_pattern("DRSXM\\.SYS") ; // monitor_core_image 1st on disk
    sort_add_group_pattern(".*\\.SYS") ; // the drivers
    sort_add_group_pattern("START\\..*") ; // startup script
    sort_add_group_pattern("HELP\\..*") ; // help texts
    sort_add_group_pattern(".*\\.CCC") ; // other chain files
    sort_add_group_pattern(".*\\.BIC") ;  // *.bin and *.bic
    sort_add_group_pattern(".*\\.BIN") ;  // *.bin and *.bic

    // available block = full disk capacity minus bad sector info
    assert(_image_partition_size <= _drive_info.capacity) ;
    blockcount = needed_blocks(_drive_info.get_usable_capacity());

    // if image is enlarged, the precoded layout params of the device are not sufficient
    // for the enlarged blockcount.
    //see TU58: normally 1 prealloced block for 256KB tape
    if (layout_info.blocks_num < blockcount) {
        // calculate new layout params in .layout_info
        try {
            recalc_layout_info(blockcount) ;
        } catch(filesystem_exception &e) {
            FATAL("filesystem_xxdp: Can not calculate new layout params");
            return ;
        }
    }

    // files setup by filesystem_base_c

    init();
}



filesystem_xxdp_c::~filesystem_xxdp_c()
{
    init() ; // free files

    delete changed_blocks ;
    changed_blocks = nullptr ;  // signal to base class destructor
    delete rootdir ;
    rootdir = nullptr ;  // signal to base class destructor
}

// copy filesystem, but without file content
// needed to get a snapshot for change compare
void filesystem_xxdp_c::copy_metadata_to(filesystem_base_c *metadata_copy)
{
    auto _rootdir = dynamic_cast<directory_xxdp_c *>(rootdir) ;
    _rootdir->copy_metadata_to(metadata_copy->rootdir) ;
}


// free / clear all structures,
// set default values from .layout_info
void filesystem_xxdp_c::init()
{

    // empty block bitmap
    bitmap.blocklist.clear(); // position not known
    for (unsigned i = 0; i < XXDP_MAX_BLOCKCOUNT; i++)
        bitmap.used[i] = 0;

    // set device params
    // blockcount = layout_info.blocks_num;
    // trunc large devices, only 64K blocks addressable = 32MB
    if (blockcount > XXDP_MAX_BLOCKCOUNT)
        blockcount = XXDP_MAX_BLOCKCOUNT;

    preallocated_blockcount = layout_info.prealloc_blocks_num;
    bootblock.blocknr = layout_info.boot_block;
    bootblock.blockcount = 0; // may not exist
    monitor_core_image.blocknr = layout_info.monitor_core_image_start_block;

    interleave = layout_info.interleave;
    monitor_core_image.blockcount = 0; // may not exist
    mfd_blocklist.clear();

    // which sort of MFD?
    if (layout_info.mfd2 >= 0) {
        mfd_variety = 1; // 2 blocks
        mfd_blocklist.resize(2);
        mfd_blocklist.at(0) = layout_info.mfd1;
        mfd_blocklist.at(1) = layout_info.mfd2;
    } else {
        mfd_variety = 2; // single block format
        mfd_blocklist.resize(1);
        mfd_blocklist.at(0) = layout_info.mfd1;
    }
    ufd_blocklist.clear();

    clear_rootdir() ; // clears file list
}

/*
 Filesystem parameter for specific drive.
 AC-S866B-MC_CHQFSB0_XXDP+_File_Struct_Doc_Oct84.pdf
 page 9,10
 Modified  by parse of actual disc image.
*/

filesystem_xxdp_c::layout_info_t filesystem_xxdp_c::get_documented_layout_info(enum dec_drive_type_e _drive_type)
{
    layout_info_t result ;
    result.drive_type = _drive_type ;
    result.block_size = 512 ; // for all drives
    result.monitor_block_count = 16 ; // for XXDP+, others?

    switch (_drive_type) {
    case devTU58:
        result.ufd_block_1 = 3 ;
        result.ufd_blocks_num = 4 ;
        result.bitmap_block_1 = 7 ;
        result.bitmaps_num = 1 ;
        result.mfd1 = 1 ;
        result.mfd2 = 2 ;
        /* DEC defines the XXDP tape to have 511 blocks.
        * But after decades of XXDPDIR and friends,
        * 512 seems to be the standard.
        */
        result.blocks_num = 512 ; // 511,
        result.prealloc_blocks_num = 40 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 8 ;
        break ;
    case devRP0456:
        result.ufd_block_1 = 3 ;
        result.ufd_blocks_num = 170 ;
        result.bitmap_block_1 = 173 ;
        result.bitmaps_num = 50 ;
        result.mfd1 = 1 ;
        result.mfd2 = 2 ;
        result.blocks_num = 48000 ;
        result.prealloc_blocks_num = 255 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 223 ;
        break ;
    case devRK035:
        result.ufd_block_1 = 3 ;
        result.ufd_blocks_num = 16 ;
        result.bitmap_block_1 = 4795 ; // ??
        result.bitmaps_num = 5 ;
        result.mfd1 = 1 ;
        result.mfd2 = 4794 ;
        result.blocks_num = 4800 ;
        result.prealloc_blocks_num = 69 ;
        result.interleave = 5 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 30 ;
        break ;
    case devRL01:
        result.ufd_block_1 = 24 ;
        result.ufd_blocks_num = 146 ; // 24..169 fiche bad, don north
        result.bitmap_block_1 = 2 ;
        result.bitmaps_num = 22 ;
        result.mfd1 = 1 ;
        result.mfd2 = -1 ;
        result.blocks_num = 10200 ; // differs from drive_info.get_usable_capacity() ?
        result.prealloc_blocks_num = 200 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 170 ;
        break ;
    case devRL02:
        result.ufd_block_1 = 24 ; // actual 2 on XXDP25 image
        result.ufd_blocks_num = 146 ; // 24..169 fiche bad, don north
        result.bitmap_block_1 = 2 ; // actual 24 on XXDP25 image
        result.bitmaps_num = 22 ;
        result.mfd1 = 1 ;
        result.mfd2 = -1 ;
        result.blocks_num = 20460 ;
        result.prealloc_blocks_num = 200 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 170 ;
        break ;
    case devRK067:
        result.ufd_block_1 = 31 ;
        result.ufd_blocks_num = 96 ;
        result.bitmap_block_1 = 2 ;
        result.bitmaps_num = 29 ;
        result.mfd1 = 1 ;
        result.mfd2 = -1 ;
        result.blocks_num = 27104 ;
        result.prealloc_blocks_num = 157 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 127 ;
        break ;
    case devRP023:
        result.ufd_block_1 = 3 ;
        result.ufd_blocks_num = 170 ;
        result.bitmap_block_1 = 173 ;
        result.bitmaps_num = 2 ;
        result.mfd1 = 1 ;
        result.mfd2 = 2 ;
        result.blocks_num = -1 ; // unknown, bad fiche
        result.prealloc_blocks_num = 255 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 223 ;
        break ;
    case devRM:
        result.ufd_block_1 = 52 ;
        result.ufd_blocks_num = 170 ;
        result.bitmap_block_1 = 2 ;
        result.bitmaps_num = 50 ;
        result.mfd1 = 1 ;
        result.mfd2 = -1 ;
        result.blocks_num = 48000 ;
        result.prealloc_blocks_num = 255 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 222 ;
        break ;
    case devRS:
        result.ufd_block_1 = 3 ;
        result.ufd_blocks_num = 4 ;
        result.bitmap_block_1 = 7 ;
        result.bitmaps_num = 2 ;
        result.mfd1 = 1 ;
        result.mfd2 = 2 ;
        result.blocks_num = 989 ;
        result.prealloc_blocks_num = 41 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 9 ;
        break ;
    case devTU56:
        result.ufd_block_1 = 102 ;
        result.ufd_blocks_num = 2 ;
        result.bitmap_block_1 = 104 ;
        result.bitmaps_num = 1 ;
        result.mfd1 = 100 ;
        result.mfd2 = 101 ;
        result.blocks_num = 576 ;
        result.prealloc_blocks_num = 69 ;
        result.interleave = 5 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 30 ; // bad fiche, don north
        break ;
    case devRX01:
        result.ufd_block_1 = 3 ;
        result.ufd_blocks_num = 4 ;
        result.bitmap_block_1 = 7 ;
        result.bitmaps_num = 1 ;
        result.mfd1 = 1 ;
        result.mfd2 = 2 ;
        result.blocks_num = 494 ;
        result.prealloc_blocks_num = 40 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 8 ;
        break ;
    case devRX02:
        result.ufd_block_1 = 3 ;
        result.ufd_blocks_num = 16 ;
        result.bitmaps_num = 4 ;
        result.bitmap_block_1 = 19 ;
        result.mfd1 = 1 ;
        result.mfd2 = 2 ;
        result.blocks_num = 988 ;
        result.prealloc_blocks_num = 55 ;
        result.interleave = 1 ;
        result.boot_block = 0 ;
        result.monitor_core_image_start_block = 23 ;
        break ;
    default:
        FATAL("storageimage_xxdp_c::get_drive_info(): invalid drive") ;
    }
    return result ;
}




// calculate new layout params .layout_info from block_count
// .layout_info must be already initialized for device,
// result: success
void filesystem_xxdp_c::recalc_layout_info(unsigned _blockcount)
{
    xxdp_blocknr_t curblk; // iterates through image

    layout_info.interleave = 1; // not used

    // 1) BOOT
    assert(layout_info.boot_block == 0); // always
    curblk = 1; // always next free block.

    // 2) MFD
    layout_info.mfd1 = 1;
    if (layout_info.mfd2 <= 0)
        curblk++; // MFD1/2
    else {
        layout_info.mfd2 = 2;
        curblk += 2; // MFD1 + MFD2
    }

    // 3) UFD
    layout_info.ufd_block_1 = curblk;
    // # of initial userdir entries
    // from XXDP: 1 UFD block serves ca. 280 file blocks
    // do not assign less than DEC table
    unsigned ufd_blocks_num = needed_blocks(image_partition_size) / 280;
    if (ufd_blocks_num > layout_info.ufd_blocks_num)
        layout_info.ufd_blocks_num = ufd_blocks_num;
    curblk += layout_info.ufd_blocks_num;

    // 4) bitmap size: blocks/8 bytes
    layout_info.bitmap_block_1 = curblk;
    // one bitmap block has entries for 960 blocks
    layout_info.bitmaps_num = needed_blocks(960, _blockcount);

    curblk += layout_info.bitmaps_num;

    // 5) monitor_core_image
    layout_info.monitor_core_image_start_block = curblk;

    // accept larger monitor core images on parse, but assert noninal size on layout info
    if (layout_info.monitor_block_count	+ curblk >= layout_info.prealloc_blocks_num)
        ERROR("Layout_info.prealloc_blocks_num not large enough for monitor core") ;
    // normal monitor_core_image size is 16, in RL02 image found as 32 (39 if interleave 5)
//    curblk += 32;
//    layout_info.prealloc_blocks_num = curblk;

    // adapt block flags to new size
    delete changed_blocks ;
    changed_blocks = new boolarray_c(layout_info.blocks_num) ;

}


/*************************************************************
 * low level operators
 *************************************************************/

// ptr to first byte of block
#define IMAGE_BLOCKNR2PTR(blocknr) (image_partition_data + (XXDP_BLOCKSIZE *(blocknr)))

// fetch/write a 16bit word in the image
// LSB first
uint16_t filesystem_xxdp_c::xxdp_image_get_word(xxdp_blocknr_t blocknr, uint8_t wordnr)
{
#ifdef TODO
    uint32_t idx = XXDP_BLOCKSIZE * blocknr + (wordnr * 2);
    assert((idx + 1) < image_partition_size);

    return image_partition_data[idx] | (image_partition_data[idx + 1] << 8);
#endif
}

void filesystem_xxdp_c::xxdp_image_set_word(xxdp_blocknr_t blocknr,         uint8_t wordnr, uint16_t val)
{
#ifdef TODO
    uint32_t idx = XXDP_BLOCKSIZE * blocknr + (wordnr * 2);
    assert((idx + 1) < image_partition_size);

    image_partition_data[idx] = val & 0xff;
    image_partition_data[idx + 1] = (val >> 8) & 0xff;
    //fprintf(stderr, "set 0x%x to 0x%x\n", idx, val) ;
#endif
}

// scan linked list at block # 'start'
// bytes 0,1 = 1st word in block are # of next. Last blck has link "0".
void filesystem_xxdp_c::xxdp_blocklist_get(xxdp_blocklist_c *bl, xxdp_blocknr_t start)
{
    xxdp_blocknr_t blocknr = start;
    bl->clear() ;
    do {
        bl->push_back(blocknr);
        // follow link to next block
        blocknr = xxdp_image_get_word(blocknr, 0);
    } while (bl->size() < XXDP_MAX_BLOCKS_PER_LIST && blocknr > 0);
    if (blocknr)
        throw filesystem_exception("xxdp_blocklist_get(): block list too long or recursion");
}

void filesystem_xxdp_c::xxdp_blocklist_set(xxdp_blocklist_c *bl)
{
    // write link field of each block with next
    for (unsigned i = 0; i < (bl->size() - 1); i++)
        xxdp_image_set_word(bl->at(i), 0, bl->at(i + 1));

    // clear link field of last block
    xxdp_image_set_word(bl->at(bl->size() - 1), 0, 0);
}

// get count of used blocks
int filesystem_xxdp_c::xxdp_bitmap_count()
{
    int result = 0;
    for (unsigned i = 0; i < bitmap.blocklist.size(); i++) {
        xxdp_blocknr_t map_blknr = bitmap.blocklist[i];
        unsigned map_wordcount = xxdp_image_get_word(map_blknr, 2);
        for (unsigned j = 0; j < map_wordcount; j++) {
            uint16_t map_flags = xxdp_image_get_word(map_blknr, j + 4);
            // 16 flags per word. count "1" bits
            if (map_flags == 0xffff)
                result += 16; // most of time
            else
                for (unsigned k = 0; k < 16; k++)
                    if (map_flags & (1 << k))
                        result++;
        }
    }
    return result;
}


// set file->changed from the changed block map
void filesystem_xxdp_c::calc_file_change_flags()
{
    for (unsigned i = 0; i < file_count(); i++) {
        file_xxdp_c *f = file_get(i);
        f->changed = false ;
        assert(changed_blocks) ;
        for (unsigned j = 0; !f->changed && j < f->blocklist.size(); j++) {
            unsigned blknr = f->blocklist[j];
            f->changed |= changed_blocks->bit_get(blknr);
        }
    }
}


/**************************************************************
 * _layout()
 * arrange objects on volume
 **************************************************************/

// quick test, if a new file of "data-size" would fit onto the volume
void filesystem_xxdp_c::xxdp_filesystem_layout_test(int data_size)
{
    unsigned ufd_blocks_needed;
    unsigned data_blocks_needed;
    unsigned available_blocks;

    // 1 UFD = 28 files, blocks are 510
    ufd_blocks_needed = needed_blocks(XXDP_UFD_ENTRIES_PER_BLOCK, file_count() + 1);
    // sum up existing files
    data_blocks_needed = 0;
    for (unsigned i = 0; i < file_count(); i++) {
        file_xxdp_c *f = file_get(i);
        int n = needed_blocks(XXDP_BLOCKSIZE-2, f->file_size);
        // fprintf(stderr,"layout_test(): file %d %s.%s start from %d, needs %d blocks\n", i, f->basename, f->ext,
        // 			 preallocated_blockcount+ data_blocks_needed, n);
        data_blocks_needed += n;
    }
    // space for the new file
    data_blocks_needed += needed_blocks(XXDP_BLOCKSIZE-2, data_size);

    // we have the space after the preallocated area.
    // It holds all file data and excess UFD blocks
    // the first "ufd_blocks_num (from .layout_info) fit in preallocated space
    assert(blockcount > preallocated_blockcount) ;
    available_blocks = blockcount - preallocated_blockcount;
    if (ufd_blocks_needed > layout_info.ufd_blocks_num)
        data_blocks_needed += ufd_blocks_needed - layout_info.ufd_blocks_num;
    if (data_blocks_needed > available_blocks)
        throw filesystem_exception("xxdp_filesystem_layout_test(): Filesystem full");
}

// calculate blocklists for monitor, bitmap,mfd, ufd and files
// total blockcount may be enlarged
void filesystem_xxdp_c::xxdp_filesystem_layout()
{
    unsigned i, j, n;
    int overflow;
    xxdp_blocknr_t blknr;

    // mark preallocated blocks in bitmap
    // this covers boot, monitor_core_image, bitmap, mfd and default sized ufd
    memset(bitmap.used, 0, sizeof(bitmap.used));
    for (i = 0; i < preallocated_blockcount; i++)
        bitmap.used[i] = 1;

    // BOOT
    if (bootblock.size()) {
        bootblock.blockcount = 1;
        blknr = layout_info.boot_block; // set start
        bootblock.blocknr = blknr;
    } else
        bootblock.blockcount = 0;

    // MONITOR
    if (monitor_core_image.size()) {
        n = needed_blocks(monitor_core_image.size());
        blknr = layout_info.monitor_core_image_start_block; // set start
        monitor_core_image.blocknr = blknr;
        monitor_core_image.blockcount = n;
    } else
        monitor_core_image.blockcount = 0;

    // BITMAP
    n = layout_info.bitmaps_num ;
    bitmap.blocklist.resize(n);
    blknr = layout_info.bitmap_block_1; // set start
    for (i = 0; i < bitmap.blocklist.size(); i++) { // enumerate sequential
        bitmap.used[blknr] = 1;
        bitmap.blocklist[i] = blknr++;
    }

    // MFD
    if (mfd_variety == 1) {
        mfd_blocklist.resize(2);
        mfd_blocklist[0] = layout_info.mfd1;
        mfd_blocklist[1] = layout_info.mfd2;
        bitmap.used[layout_info.mfd1] = 1;
        bitmap.used[layout_info.mfd2] = 1;
    } else if (mfd_variety == 2) {
        mfd_blocklist.resize(1);
        mfd_blocklist[0] = layout_info.mfd1;
        bitmap.used[layout_info.mfd1] = 1;
    } else
        FATAL("MFD variety must be 1 or 2");
    // UFD
    // starts in preallocated area, may extend into freespace
    n = needed_blocks(XXDP_UFD_ENTRIES_PER_BLOCK, file_count());
    if (n < layout_info.ufd_blocks_num)
        n = layout_info.ufd_blocks_num; // RADI defines minimum
    ufd_blocklist.resize(n);
    // last UFD half filled
    blknr = layout_info.ufd_block_1;	 // start
    i = 0;
    // 1) fill UFD into preallocated space
    while (i < n && i < layout_info.ufd_blocks_num) {
        bitmap.used[blknr] = 1;
        ufd_blocklist[i++] = blknr++;
    }
    // 2) continue in free space, if larger than RADI defines
    blknr = preallocated_blockcount; // 1st in free space
    while (i < n) {
        bitmap.used[blknr] = 1;
        ufd_blocklist[i++] = blknr++;
    }
    // blknr now 1st block behind UFD.

    // FILES

    // files start in free space
    if (blknr < preallocated_blockcount)
        blknr = preallocated_blockcount;

    overflow = 0;
    for (unsigned file_idx = 0; !overflow && file_idx < file_count(); file_idx++) {
        file_xxdp_c *f = file_get(file_idx);
        // amount of 510 byte blocks
        n = needed_blocks(XXDP_BLOCKSIZE-2, f->file_size);
        f->blocklist.resize(n) ;
        // fprintf(stderr,"layout(): file %d %s.%s start from %d, needs %d blocks\n", i, f->basename, f->ext, blknr, n);
        for (j = 0; !overflow && j < n; j++) {
            if (j >= XXDP_MAX_BLOCKS_PER_LIST) {
                throw filesystem_exception("Filesystem overflow. File %s.%s too large, uses more than %d blocks",
                                           f->basename.c_str(), f->ext.c_str(), XXDP_MAX_BLOCKS_PER_LIST);
                overflow = 1;
            } else if (blknr >= blockcount) {
                throw filesystem_exception("File system overflow, can hold max %d blocks.", blockcount);
                overflow = 1;
            } else {
                bitmap.used[blknr] = 1;
                f->blocklist.at(j) = blknr++;  // at() has range check
            }
        }
        f->block_count = n;
        if (overflow)
            throw filesystem_exception("File system overflow");
    }

    // expand file system size if needed.
    // later check wether physical image can be expanded.
    if (blknr >= blockcount)
        throw filesystem_exception("File system overflow");
}




/**************************************************************
 * parse()
 * convert byte array of image into logical objects
 **************************************************************/

// read the "Master File Directory" MFD, produce MFD, Bitmap and UFD
void filesystem_xxdp_c::parse_mfd()
{
    unsigned n;
    xxdp_blocknr_t blknr, mfdblknr;

    xxdp_blocklist_get(&mfd_blocklist, layout_info.mfd1);
    if (mfd_blocklist.size() == 2) {
        // 2 blocks. first predefined, fetch 2nd from image
        // Prefer MFD data over linked list scan, why?
        if (mfd_variety != 1)
            WARNING("MFD is 2 blocks, variety 1 expected, but variety %d defined",
                    mfd_variety);
        /*
         mfd_blocklist->count = 2;
         mfd_blocklist->blocknr[0] = layout_info.mfd1;
         mfd_blocklist->blocknr[1] = xxdp_image_get_word(_this,
         mfd_blocklist->blocknr[0], 0);
         */
        mfdblknr = mfd_blocklist[0];

        interleave = xxdp_image_get_word(mfdblknr, 1);
        // build bitmap,
        // word[2] = bitmap start block, word[3] = pointer to bitmap #1, always the same?
        // a 0 terminated list of bitmap blocks is in MFD1, word 2,3,...
        // Prefer MFD data over linked list scan, why?

        n = 0;
        bitmap.blocklist.clear() ;
        do {
            assert(n + 3 < 255);
            blknr = xxdp_image_get_word(mfdblknr, n + 3);
            if (blknr > 0) {
                bitmap.blocklist.push_back(blknr);
                assert(bitmap.blocklist[n] == blknr);
                n++;
            }
        } while (blknr > 0);
        assert(bitmap.blocklist.size() == n);

        assert(mfd_blocklist.size() > 1) ; // was tested to be 2
        mfdblknr = mfd_blocklist[1];
        // Get start of User File Directory from MFD2, word 2, then scan linked list
        blknr = xxdp_image_get_word(mfdblknr, 2);
        xxdp_blocklist_get(&ufd_blocklist, blknr);
    } else if (mfd_blocklist.size() == 1) {
        // var 2: "MFD1/2" RL01/2 ?
        if (mfd_variety != 2)
            WARNING( "MFD is 1 blocks, variety 2 expected, but variety %d defined",
                     mfd_variety);

        mfdblknr = mfd_blocklist[0];

        // Build UFD block list
        blknr = xxdp_image_get_word(mfdblknr, 1);
        xxdp_blocklist_get(&ufd_blocklist, blknr);
        // verify len
        n = xxdp_image_get_word(mfdblknr, 2);
        if (n != ufd_blocklist.size())
            WARNING( "UFD block count is %u, but %d in MFD1/2",
                     ufd_blocklist.size(), n);
        // best choice is len of disk list

        // Build Bitmap block list
        blknr = xxdp_image_get_word(mfdblknr, 3);
        xxdp_blocklist_get(&bitmap.blocklist, blknr);
        // verify len
        n = xxdp_image_get_word(mfdblknr, 4);
        if (n != bitmap.blocklist.size())
            WARNING("Bitmap block count is %u, but %d in MFD1/2",
                    bitmap.blocklist.size(), n);

        // total num of blocks
        n = blockcount = xxdp_image_get_word(mfdblknr, 7);
        if (n != blockcount)
            WARNING("Device blockcount is %u in layout_info, but %d in MFD1/2",
                    blockcount, n);

        n = preallocated_blockcount = xxdp_image_get_word(mfdblknr, 8);
        if (n != layout_info.prealloc_blocks_num)
            WARNING("Device preallocated blocks are %u in layout_info, but %d in MFD1/2",
                    layout_info.prealloc_blocks_num, n);

        interleave = xxdp_image_get_word(mfdblknr, 9);
        if (interleave != layout_info.interleave)
            WARNING("Device interleave is %u in layout_info, but %d in MFD1/2",
                    layout_info.interleave, interleave);

        n = monitor_core_image.blocknr = xxdp_image_get_word(mfdblknr, 11);
        if (n != layout_info.monitor_core_image_start_block)
            WARNING("Monitor core start is %u in layout_info, but %d in MFD1/2",
                    layout_info.monitor_core_image_start_block, n);
        // last monitor_core_image block = preallocated_blockcount
        monitor_core_image.blockcount = preallocated_blockcount - monitor_core_image.blocknr ;
        if (monitor_core_image.blockcount != layout_info.monitor_block_count)
            WARNING("Monitor core len %d blocks, should be %u",monitor_core_image.blockcount,
                    layout_info.monitor_block_count) ;

        WARNING("Position of bad block file not yet evaluated");
    } else
        FATAL("Invalid block count in MFD: %d", mfd_blocklist.size());
}

// bitmap blocks known, produce "used[]" flag array
void filesystem_xxdp_c::parse_bitmap()
{
    unsigned i, j, k;
    xxdp_blocknr_t blknr; // enumerates the block flags
    // assume consecutive bitmap blocks encode consecutive block numbers
    // what about map number?
    blknr = 0;
    for (i = 0; i < bitmap.blocklist.size(); i++) {
        unsigned map_wordcount;
        xxdp_blocknr_t map_blknr = bitmap.blocklist[i];
        xxdp_blocknr_t map_start_blknr; // block of 1st map block
        // mapnr = xxdp_image_get_word(blknr, 2);
        map_wordcount = xxdp_image_get_word(map_blknr, 2);
        assert(map_wordcount == XXDP_BITMAP_WORDS_PER_MAP);
        // verify link to start
        map_start_blknr = xxdp_image_get_word(map_blknr, 3);
        assert(map_start_blknr == bitmap.blocklist[0]);
        // hexdump(flog, IMAGE_BLOCKNR2OFFSET(map_blknr), 512, "Block %u = bitmap %u", map_blknr, i);
        for (j = 0; j < map_wordcount; j++) {
            uint16_t map_flags = xxdp_image_get_word(map_blknr, j + 4);
            // 16 flags per word. LSB = lowest blocknr
            for (k = 0; k < 16; k++) {
                assert(blknr == (i * XXDP_BITMAP_WORDS_PER_MAP + j) * 16 + k);
                if (map_flags & (1 << k))
                    bitmap.used[blknr] = 1;
                else
                    bitmap.used[blknr] = 0;
                blknr++;
            }
        }
    }
    // blknr is now count of defined blocks
    // bitmap.blockcount = blknr;
}

// read block[start] ... block[start+blockcount-1] into data[]
void filesystem_xxdp_c::parse_stream(xxdp_multiblock_c *_multiblock,
                                     xxdp_blocknr_t _start, xxdp_blocknr_t _blockcount)
{
#ifdef TODO
    _multiblock->blockcount = _blockcount;
    _multiblock->blocknr = _start;
    unsigned data_size = XXDP_BLOCKSIZE * _blockcount;
    _multiblock->set(IMAGE_BLOCKNR2OFFSET(_start), data_size);
#endif
}

// UFD blocks known, produce filelist
void filesystem_xxdp_c::parse_ufd()
{
    unsigned i, j;
    xxdp_blocknr_t blknr; // enumerates the directory blocks
    uint32_t file_entry_start_wordnr;
    uint16_t w;
    for (i = 0; i < ufd_blocklist.size(); i++) {
        blknr = ufd_blocklist[i];
        // hexdump(flog, IMAGE_BLOCKNR2OFFSET(blknr), 512, "Block %u = UFD block %u", blknr, i);
        // 28 dir entries per block
        for (j = 0; j < XXDP_UFD_ENTRIES_PER_BLOCK; j++) {
            file_entry_start_wordnr = 1 + j * XXDP_UFD_ENTRY_WORDCOUNT; // point to start of file entry in block
            // filename: 2*3 radix50 chars
            w = xxdp_image_get_word(blknr, file_entry_start_wordnr + 0);
            if (w == 0)
                continue; // invalid entry
            if (file_count() >= XXDP_MAX_FILES_PER_IMAGE)
                throw filesystem_exception("Filesystem overflow. XXDP UFD read: more than %d files!", XXDP_MAX_FILES_PER_IMAGE);

            // create file entry
            file_xxdp_c *f = new file_xxdp_c();
            f->basename[0] = 0;
            f->changed = 0;
            f->internal = 0;
            // basename: 6 chars

            f->basename.assign(rad50_decode(w)) ;

            w = xxdp_image_get_word(blknr, file_entry_start_wordnr + 1);
            f->basename.append(rad50_decode(w)) ;

            // extension: 3 chars
            w = xxdp_image_get_word(blknr, file_entry_start_wordnr + 2);
            f->ext.assign(rad50_decode(w));

            w = xxdp_image_get_word(blknr, file_entry_start_wordnr + 3);
            f->modification_time = dos11date_decode(w);

            // start block, scan blocklist
            w = xxdp_image_get_word(blknr, file_entry_start_wordnr + 5);
            xxdp_blocklist_get(&(f->blocklist), w);

            // check: filelen?
            f->block_count = xxdp_image_get_word(blknr, file_entry_start_wordnr + 6);
            if (f->block_count != f->blocklist.size())
                WARNING("XXDP UFD read: file %s.%s: saved file size is %d, blocklist len is %d.\n",
                        f->basename.c_str(), f->ext.c_str(), f->block_count, f->blocklist.size());

            // check: lastblock?
            w = xxdp_image_get_word(blknr, file_entry_start_wordnr + 7);

            f->host_path = f->get_host_path() ;

            rootdir->add_file(f) ;  // save, owned by directory now
        }
    }
}

// load and allocate file data from blocklist
// da is read in 510 byte chunks, actual size not known
void filesystem_xxdp_c::parse_file_data(file_xxdp_c *f)
{
#ifdef TODO
    unsigned block_datasize = XXDP_BLOCKSIZE - 2; // data amount in block, after link word
    uint8_t *src, *dst;
    unsigned data_size = f->blocklist.size() * block_datasize;
    f->set_size(data_size) ;
    f->file_size = data_size ;
    dst = f->data_ptr();
    for (unsigned i = 0; i < f->blocklist.size(); i++) {
        // cat data from all blocks
        int blknr = f->blocklist[i];
        src = IMAGE_BLOCKNR2OFFSET(blknr) + 2; // skip link
        memcpy(dst, src, block_datasize);
        dst += block_datasize;
        assert(dst <= f->data_ptr() + f->file_size);
    }
#endif
}


// analyse the image, build filesystem data structure
// parameters already set by _reset()
// return: true = OK
void filesystem_xxdp_c::parse()
{
    std::exception_ptr eptr;

    // events in the queue references streams, which get invalid on re-parse.
    assert(event_queue.empty()) ;

    init();

    try {

        // parse structure info first, will update lay_out info
        parse_mfd();
        parse_bitmap();
        parse_ufd();

        // read bootblock 0. may consists of 00's
        parse_stream(&bootblock, bootblock.blocknr, 1);
        // TODO: file_size, bootblock = file?
        // read monitor_core_image:
        // Size as defined. Alternative: from defined start until end of preallocated area, about 32
        parse_stream(&monitor_core_image, monitor_core_image.blocknr, monitor_core_image.blockcount);
        // TODO: file_size, monitor = file?

        // read data for all user files
        for (unsigned i = 0; i < file_count(); i++)
            parse_file_data(file_get(i));
    }
    catch (filesystem_exception &e) {
        eptr = std::current_exception() ;
    }


    calc_file_change_flags();

    if (eptr)
        std::rethrow_exception(eptr);
}


/**************************************************************
 * render
 * create an binary image from logical datas structure
 **************************************************************/

void filesystem_xxdp_c::render_multiblock(xxdp_multiblock_c *multiblock)
{
#ifdef TODO
    uint8_t *dst = IMAGE_BLOCKNR2OFFSET(multiblock->blocknr);
    // write into sequential blocks.
    memcpy(dst, multiblock->data_ptr(), multiblock->size());
#endif
}

// write the bitmap words of all used[] blocks
// blocklist already calculated
void filesystem_xxdp_c::render_bitmap()
{
    xxdp_blocknr_t blknr;

    // link blocks
    xxdp_blocklist_set(&(bitmap.blocklist));

    for (blknr = 0; blknr < blockcount; blknr++) {
        // which bitmap block resides the flag in?
        int map_blkidx = blknr / (XXDP_BITMAP_WORDS_PER_MAP * 16); // # in list
        int map_blknr = bitmap.blocklist[map_blkidx]; // abs pos bitmap blk
        int map_flags_bitnr = blknr % (XXDP_BITMAP_WORDS_PER_MAP * 16);

        // set metadata for the whole bitmap block only if first flag is processed
        if (map_flags_bitnr == 0) {
            xxdp_image_set_word(map_blknr, 1, map_blkidx + 1); // "map number":	enumerates map blocks
            xxdp_image_set_word(map_blknr, 2, XXDP_BITMAP_WORDS_PER_MAP); // 60
            xxdp_image_set_word(map_blknr, 3, bitmap.blocklist[0]); // "link to first map"
        }

        // set a single block flag
        if (bitmap.used[blknr]) {
            // word and bit in this block containing the flag
            int map_flags_wordnr = map_flags_bitnr / 16;
            int map_flag_bitpos = map_flags_bitnr % 16;
            uint16_t map_flags = xxdp_image_get_word(map_blknr, map_flags_wordnr + 4);
            map_flags |= (1 << map_flag_bitpos);
            xxdp_image_set_word(map_blknr, map_flags_wordnr + 4, map_flags);
        }
    }
}

// blocklist already calculated
void filesystem_xxdp_c::render_mfd()
{
    int i, n;
    xxdp_blocknr_t blknr;
    // link blocks
    xxdp_blocklist_set(&mfd_blocklist);
    if (mfd_variety == 1) {
        // two block MFD1, MFD2
        assert(mfd_blocklist.size() == 2);
        // write MFD1
        blknr = mfd_blocklist[0];
        xxdp_image_set_word(blknr, 1, interleave);
        xxdp_image_set_word(blknr, 2, bitmap.blocklist[0]);
        // direct list of bitmap blocks
        n = bitmap.blocklist.size();
        assert(n < 252); // space for 256 - 3 - 2 = 251 bitmap blocks
        for (i = 0; i < n; i++)
            xxdp_image_set_word(blknr, i + 3, bitmap.blocklist[i]);
        // terminate list
        xxdp_image_set_word(blknr, n + 3, 0);

        // write MFD2
        blknr = mfd_blocklist[1];
        xxdp_image_set_word(blknr, 1, 0401); // UIC[1,1]
        xxdp_image_set_word(blknr, 2, ufd_blocklist[0]); // start of UFDs
        xxdp_image_set_word(blknr, 3, XXDP_UFD_ENTRY_WORDCOUNT);
        xxdp_image_set_word(blknr, 4, 0);
    } else if (mfd_variety == 2) {
        // MFD1/2
        assert(mfd_blocklist.size() == 1);
        blknr = mfd_blocklist[0];
        xxdp_image_set_word(blknr, 1, ufd_blocklist[0]); // ptr to 1st UFD blk
        xxdp_image_set_word(blknr, 2, ufd_blocklist.size());
        xxdp_image_set_word(blknr, 3, bitmap.blocklist[0]); // ptr to bitmap
        xxdp_image_set_word(blknr, 4, bitmap.blocklist.size());
        xxdp_image_set_word(blknr, 5, blknr); // to self
        xxdp_image_set_word(blknr, 6, 0);
        xxdp_image_set_word(blknr, 7, blockcount); // # of supported blocks
        xxdp_image_set_word(blknr, 8, preallocated_blockcount);
        xxdp_image_set_word(blknr, 9, interleave);
        xxdp_image_set_word(blknr, 10, 0);
        xxdp_image_set_word(blknr, 11, monitor_core_image.blocknr); // 1st of monitor_core_image core img
        xxdp_image_set_word(blknr, 12, 0);
        // bad sector position: needs to be define in RADI for each device
        xxdp_image_set_word(blknr, 13, 0);
        xxdp_image_set_word(blknr, 14, 0);
        xxdp_image_set_word(blknr, 15, 0);
        xxdp_image_set_word(blknr, 16, 0);
    } else
        FATAL("MFD variety must be 1 or 2");
}

void filesystem_xxdp_c::render_ufd()
{
    // link blocks
    xxdp_blocklist_set(&ufd_blocklist);
    //
    for (unsigned file_idx = 0; file_idx < file_count(); file_idx++) {
        char buff[80];
        xxdp_blocknr_t ufd_blknr = layout_info.ufd_block_1 + (file_idx / XXDP_UFD_ENTRIES_PER_BLOCK);
        // word nr of cur entry in cur block. skip link word.
        int ufd_word_offset = 1 + (file_idx % XXDP_UFD_ENTRIES_PER_BLOCK) * XXDP_UFD_ENTRY_WORDCOUNT;
        file_xxdp_c *f = file_get(file_idx);

        xxdp_blocklist_set(&f->blocklist);

        // filename chars 0..2
        strncpy(buff, f->basename.c_str(), 3);
        buff[3] = 0;
        xxdp_image_set_word(ufd_blknr, ufd_word_offset + 0, rad50_encode(buff));
        // filename chars 3..5
        if (strlen(f->basename.c_str()) < 4)
            buff[0] = 0;
        else
            strncpy(buff, f->basename.c_str() + 3, 3);
        buff[3] = 0;
        xxdp_image_set_word(ufd_blknr, ufd_word_offset + 1, rad50_encode(buff));
        // ext
        xxdp_image_set_word(ufd_blknr, ufd_word_offset + 2, rad50_encode((char *)f->ext.c_str()));

        xxdp_image_set_word(ufd_blknr, ufd_word_offset + 3, dos11date_encode(f->modification_time));

        xxdp_image_set_word(ufd_blknr, ufd_word_offset + 4, 0); // ACT-11 logical end
        xxdp_image_set_word(ufd_blknr, ufd_word_offset + 5, f->blocklist[0]); // 1st block
        xxdp_image_set_word(ufd_blknr, ufd_word_offset + 6, f->blocklist.size()); // file length in blocks
        unsigned n = f->blocklist[f->blocklist.size() - 1];
        xxdp_image_set_word(ufd_blknr, ufd_word_offset + 7, n); // last block
        xxdp_image_set_word(ufd_blknr, ufd_word_offset + 8, 0); // ACT-11 logical 52
    }
}

// write file->data[] into blocks of blocklist
void filesystem_xxdp_c::render_file_data(file_xxdp_c *f)
{
#ifdef TODO
    unsigned bytestocopy = f->file_size;
    uint8_t *src, *dst;
    src = f->data_ptr();
    for (unsigned i = 0; i < f->blocklist.size(); i++) {
        unsigned blknr = f->blocklist[i];
        unsigned block_datasize; // data amount in this block
        assert(bytestocopy);
        dst = IMAGE_BLOCKNR2OFFSET(blknr) + 2; // write behind link word

        // data amount =n block without link word
        block_datasize = XXDP_BLOCKSIZE - 2;
        // default: transfer full block
        if (bytestocopy < block_datasize) // EOF?
            block_datasize = bytestocopy;
        memcpy(dst, src, block_datasize);
        src += block_datasize;
        bytestocopy -= block_datasize;
        assert(src <= f->data_ptr() + f->file_size);
    }
    assert(bytestocopy == 0);
#endif
}

// write filesystem into image
// Assumes all file data and blocklists are valid
// return: 0 = OK
void filesystem_xxdp_c::render()
{
#ifdef TODO
    unsigned needed_size = blockcount * XXDP_BLOCKSIZE;

    // calc blocklists and sizes
    xxdp_filesystem_layout() ; // throws

    if (needed_size > image_partition_size)
        // more bytes needed than image provides. Expand?
        throw filesystem_exception("Image only %d bytes large, filesystem needs %d *%d = %d.", image_partition_size,
                                   blockcount, XXDP_BLOCKSIZE, needed_size);

    // format media, all 0's
//     memset(image_partition_data, 0, image_partition_size);
    if (bootblock)
        render_multiblock(&bootblock);
    else
        image_partition->write(nullptr, bootblock start offset, bootblock len) ;
    if (monitor_core_image)
        render_multiblock(&monitor_core_image);
    else
        image_partition->write(nullptr, monitor tart offset, monitor len) ;

    render_bitmap();
    render_mfd();
    render_ufd();

    // read data for all user files
    for (unsigned file_idx = 0; file_idx < file_count(); file_idx++)
        render_file_data(file_get(file_idx));

    parse_volumeinfo() ;

#endif
}


/**************************************************************
 * FileAPI
 * add / get files in logical data structure
 **************************************************************/

// fill the pseudo file with textual volume information
// it never changes
void filesystem_xxdp_c::xxdp_filesystem_render_volumeinfo(string *text_buffer)
{
    char line[1024];

    text_buffer->clear();
    sprintf(line, "# %s.%s - info about XXDP volume on %s device.\n",
            specialfile_volumeinfo.basename.c_str(), specialfile_volumeinfo.ext.c_str(), drive_info.device_name.c_str());
    text_buffer->append(line);

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(line, "# Produced by QUnibone at %d-%d-%d %d:%d:%d\n", tm.tm_year + 1900,
            tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    text_buffer->append(line);

    sprintf(line, "\n# Blocks on device\nblockcount=%d\n", blockcount);
    text_buffer->append(line);
    sprintf(line, "# But XXDP doc says %d\n", layout_info.blocks_num);
    text_buffer->append(line);

    sprintf(line, "prealloc_blocks_num=%d (%d by disk type)\n", preallocated_blockcount, layout_info.prealloc_blocks_num);
    text_buffer->append(line);
    sprintf(line, "interleave=%d\n", layout_info.interleave);
    text_buffer->append(line);
    sprintf(line, "boot_block=%d\n", layout_info.boot_block);
    text_buffer->append(line);
    sprintf(line, "monitor_block=%d\n", layout_info.monitor_core_image_start_block);
    text_buffer->append(line);
    sprintf(line, "monitor_blockcount=%d (all remaining preallocated blocks, specified as %d) \n",
            monitor_core_image.blockcount, layout_info.monitor_block_count);
    text_buffer->append(line);

    sprintf(line, "\n# Master File Directory %s\n",
            layout_info.mfd2 < 0 ? "Var2: MFD1/2" : "Var1: MFD1+MFD2");
    text_buffer->append(line);
    sprintf(line, "mfd1=%d\n", layout_info.mfd1);
    text_buffer->append(line);
    if (layout_info.mfd2 > 0) {
        sprintf(line, "mfd2=%d\n", layout_info.mfd2);
        text_buffer->append(line);
    }
    sprintf(line, "\n# Bitmap of used blocks:\nbitmap_block_1=%d\n",
            layout_info.bitmap_block_1);
    text_buffer->append(line);
    sprintf(line, "bitmaps_num=%d\n", layout_info.bitmaps_num);
    text_buffer->append(line);

    sprintf(line, "\n# User File Directory:\nufd_block_1=%d\n", layout_info.ufd_block_1);
    text_buffer->append(line);
    sprintf(line, "ufd_blocks_num=%d\n", layout_info.ufd_blocks_num);
    text_buffer->append(line);

    for (unsigned file_idx = 0; file_idx < file_count(); file_idx++) {
        file_xxdp_c *f = file_get(file_idx);
        sprintf(line, "\n# File %2d \"%s.%s\".", file_idx, f->basename.c_str(), f->ext.c_str());
        text_buffer->append(line);
        assert(f->blocklist.size() > 0) ;
        sprintf(line, " Data %u = 0x%X bytes, start block %d @ 0x%X.", f->file_size,
                f->file_size, f->blocklist[0],
                f->blocklist[0] * XXDP_BLOCKSIZE);
        text_buffer->append(line);
    }
    text_buffer->append("\n");
}



// take a file of the shared dir, push it to the filesystem
// a PDP file can have several streams, "streamname" is
// the host file is only one stream of a PDP filesystem file
// special indexes:
// -1: bootblock
// -2: monitor
// -3: volume information text file
// else regular file
// fname: basename.ext
void filesystem_xxdp_c::import_host_file(file_host_c *host_file)
{
    string host_fname = host_file->get_filename(); // XXDP:  path == name
    host_file->data_open(/*write?*/ false) ;

    if (!strcasecmp(host_fname.c_str(), XXDP_VOLUMEINFO_BASENAME "." XXDP_VOLUMEINFO_EXT)) {
        // evaluate parameter file ? is read only to user!
    } else if (!strcasecmp(host_fname.c_str(), XXDP_BOOTBLOCK_BASENAME "." XXDP_BOOTBLOCK_EXT)) {
        if (host_file->file_size != XXDP_BLOCKSIZE)
            throw filesystem_exception("Boot block not %d bytes",  XXDP_BLOCKSIZE);
        bootblock.set(&host_file->data, host_file->file_size) ;
    } else if (!strcasecmp(host_fname.c_str(), XXDP_MONITOR_BASENAME "." XXDP_MONITOR_EXT)) {
        // assume max len of monitor_core_image
        unsigned n = XXDP_BLOCKSIZE * layout_info.monitor_block_count;
        // monitor_core_image defined to be 16block=8K
        // Larger monitor files can be added, but will be overwritten
        if (host_file->file_size > n)
            WARNING ("Monitor core image too big: is %d bytes, only %d allowed; will be trunc'ed", host_file->file_size, n);
        monitor_core_image.set(&host_file->data, host_file->file_size);
    } else {
        string basename, ext;
        // regular file
        if (file_count() + 1 >= XXDP_MAX_FILES_PER_IMAGE)
            throw filesystem_exception("Too many files, only %d allowed", XXDP_MAX_FILES_PER_IMAGE);
        try {
            xxdp_filesystem_layout_test(host_file->file_size) ;
        } catch(filesystem_exception &e) {
            throw filesystem_exception("Disk full, file \"%s\" with %d bytes too large", host_fname.c_str(), host_file->file_size);
        }

        // make filename.extension to "FILN  .E  "
        filename_from_host(&host_fname, &basename, &ext);

        string filename = make_filename(basename, ext) ;
        // duplicate file name? Likely! because of trunc to six letters
        file_xxdp_c *f = dynamic_cast<file_xxdp_c *>(file_by_path.get(filename)) ; // already hashed?
        if (f != nullptr)
            throw filesystem_exception("Duplicate filename %s.%s", basename.c_str(), ext.c_str());
        /*
            for (unsigned file_idx = 0; file_idx < file_count(); file_idx++) {
                file_xxdp_c *f1 = file_get(file_idx);
                if (!strcasecmp(basename.c_str(), f1->basename.c_str()) && !strcasecmp(ext.c_str(), f1->ext.c_str()))
                    return error_set(ERROR_FILESYSTEM_DUPLICATE, "Duplicate filename %s.%s", basename.c_str(),
                                     ext.c_str());
            }
            */
        // now insert
        f = new file_xxdp_c();
        f->changed = false;
        f->internal = false;
        f->host_path = host_file->path ;
        f->set(&host_file->data, host_file->file_size);
        f->file_size = f->size() ; // from inherited stream
        f->basename = basename ;
        f->ext = ext ;
        f->modification_time = host_file->modification_time;

        // only range 1970..1999 allowed
        if (f->modification_time.tm_year < 70)
            f->modification_time.tm_year = 70;
        else if (f->modification_time.tm_year > 99)
            f->modification_time.tm_year = 99;

        rootdir->add_file(f) ; // add, now owned by rootdir
    }
    host_file->data_close() ;
}


void filesystem_xxdp_c::delete_host_file(string host_path)
{
}



// access file streams, bootblock and monitor in an uniform way
// -3 = volume info, -2 = monitor, -1 = boot block
// bootblock or monitor are NULL, if empty

file_xxdp_c *filesystem_xxdp_c::file_get(int fileidx)
{
    file_xxdp_c *f = nullptr;
    if (fileidx == -3) {
        f = &specialfile_volumeinfo;
        f->basename = XXDP_VOLUMEINFO_BASENAME;
        f->ext = XXDP_VOLUMEINFO_EXT;
        f->internal = true; // can not be deleted on shared dir
        memset(&f->modification_time, 0, sizeof(f->modification_time));

        // volume info is synthetic, maps not from disk area
        // so own buffer.  freed by ~file_dec_stream_c()
        string text_buffer ; // may grow large, but data kept on heap
        xxdp_filesystem_render_volumeinfo(&text_buffer);
        f->set(&text_buffer) ;

        // VOLUM INF is "changed", if home block or directories changed
        f->internal = true;
        f->changed = true ; // always? struct_changed?
    } else if (fileidx == -2 && !monitor_core_image.is_zero_data(0)) {
        f = &specialfile_monitor;
        f->basename = XXDP_MONITOR_BASENAME;
        f->ext = XXDP_MONITOR_EXT;
        f->set(&monitor_core_image) ;
        memset(&f->modification_time, 0, sizeof(f->modification_time));
        f->internal = 0;
    } else if (fileidx == -1  && !bootblock.is_zero_data(0)) {
        f = &specialfile_bootblock;
        f->basename = XXDP_BOOTBLOCK_BASENAME ;
        f->ext = XXDP_BOOTBLOCK_EXT;
        f->set(&bootblock) ;
        memset(&f->modification_time, 0, sizeof(f->modification_time));
        f->internal = 0;
    } else if (fileidx >= 0 && fileidx < (int)file_count()) {
        // regular file. Must've been added with rootdir->add_file()
        f = dynamic_cast< file_xxdp_c *> (rootdir->files[fileidx]) ;
    } else
        return nullptr;

    return f ;
}


// result ist basename.ext, without spaces
// "filname" and "ext" contain components WITH spaces, if != NULL
// "bla.foo.c" => "BLA.FO", "C	", result = "BLA.FO.C"
// "bla" => "BLA."
string filesystem_xxdp_c::filename_from_host(string *hostfname, string *result_basename, string *result_ext)
{
    string pathbuff = *hostfname ;

    // upcase and replace forbidden characters
    for (unsigned i = 0 ; i < pathbuff.length() ; i++) {
        char c ;
        switch(c = pathbuff[i]) {
        case '_' :
            c = ' ' ;
            break ;
        case 'a' ... 'z':
            c = toupper(c) ;
            break ;
        case 'A' ... 'Z':
        case '$':
        case '.':
        case '0' ... '9':
            c = c ;
            break ;
        default:
            c = '%' ;
        }
        pathbuff[i] = c ;
    }

    // make it 6.3. can use Linux function
    string _basename, _ext ;
    split_path(pathbuff, nullptr, nullptr, &_basename, &_ext) ;
    _ext = _ext.substr(0, 3) ;
    trim(_ext) ;
    _basename = _basename.substr(0, 6) ;
    trim(_basename) ;



    if (result_basename != nullptr)
        *result_basename = _basename;
    if (result_ext != nullptr)
        *result_ext = _ext;

    // with "." on empty extension "FILE."
    return make_filename(_basename, _ext) ;
}



// sort files in rootdir according to order,
// set by "sort_add_group_pattern()"
void filesystem_xxdp_c::sort()
{
    // non recursive
    filesystem_base_c::sort(rootdir->files) ;
}



/**************************************************************
 * Display structures
 **************************************************************/




string filesystem_xxdp_c::xxdp_date_text(struct tm t) {
    string mon[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV",
                     "DEC"
                   };
    char buff[80];
    sprintf(buff, "%02d-%3s-%02d", t.tm_mday, mon[t.tm_mon].c_str(), t.tm_year);
    return string(buff);
}
// output like XXDP2.5
// ENTRY# BASENAME.EXT     DATE		  LENGTH  START   VERSION
//
//	   1  XXDPSM.SYS	   1-MAR-89 		29	  000050   E.0
//	   2  XXDPXM.SYS	   1-MAR-89 		39	  000105
string filesystem_xxdp_c::xxdp_dir_line(int fileidx)
{
    char buff[80];
    file_xxdp_c *f;
    if (fileidx < 0)
        return "ENTRY# BASENAME.EXT		 DATE		   LENGTH  START   VERSION";
    f = file_get(fileidx);
    assert(f->blocklist.size() > 0);
    sprintf(buff, "%5d	%6s.%-3s%15s%11d	%06o", fileidx + 1, f->basename.c_str(), f->ext.c_str(),
            xxdp_date_text(f->modification_time).c_str(), (unsigned)f->blocklist.size(), f->blocklist[0]);
    return string(buff) ;
}

void filesystem_xxdp_c::print_dir(FILE *stream)
{
    // print a DIR like XXDP

    // header
    fprintf(stream, "%s\n", xxdp_dir_line(-1).c_str());
    fprintf(stream, "\n");
    // files
    for (int file_idx = 0; (unsigned)file_idx < file_count(); file_idx++)
        fprintf(stream, "%s\n", xxdp_dir_line(file_idx).c_str());

    fprintf(stream, "\n");
    fprintf(stream, "FREE BLOCKS: %d\n", blockcount - xxdp_bitmap_count());
}


void filesystem_xxdp_c::print_diag(FILE *stream)
{
#ifdef TODO
    unsigned  n;
    char line[256];
    char buff[256];
    int used;
    fprintf(stream, "Filesystem has %d blocks, usage:\n", blockcount);
    for (xxdp_blocknr_t blknr = 0; blknr < blockcount; blknr++) {
        line[0] = 0;
        if (blknr == 0) {
            sprintf(buff, " BOOTBLOCK \"%s.%s\"", XXDP_BOOTBLOCK_BASENAME, XXDP_BOOTBLOCK_EXT);
            strcat(line, buff);
        }

        // monitor_core_image
        n = monitor_core_image.blockcount;
        int blkoffset = blknr - monitor_core_image.blocknr;
        if (blkoffset >= 0 && blkoffset < (int)n) {
            sprintf(buff, " MONITOR \"%s.%s\" - %d/%d", XXDP_MONITOR_BASENAME, XXDP_MONITOR_EXT,
                    blkoffset, n);
            strcat(line, buff);
        }

        // search mfd1, mfd2
        n = mfd_blocklist.size() ;
        for (unsigned i = 0; i < n; i++)
            if (mfd_blocklist[i] == blknr) {
                sprintf(buff, " MFD - %d/%d", i, n);
                strcat(line, buff);
            }
        // search ufd
        n = ufd_blocklist.size();
        for (unsigned i = 0; i < n; i++)
            if (ufd_blocklist[i] == blknr) {
                sprintf(buff, " UFD - %d/%d", i, n);
                strcat(line, buff);
            }
        // search bitmap
        n = bitmap.blocklist.size();
        for (unsigned i = 0; i < n; i++)
            if (bitmap.blocklist[i] == blknr) {
                sprintf(buff, " BITMAP - %d/%d", i, n);
                strcat(line, buff);
            }
        // search file
        for (unsigned file_idx = 0; file_idx < file_count(); file_idx++) {
            file_xxdp_c *f = file_get(file_idx);
            n = f->blocklist.size();
            for (unsigned j = 0; j < n; j++)
                if (f->blocklist[j] == blknr) {
                    sprintf(buff, " file #%d: \"%s.%s\" - %d/%d", file_idx, f->basename.c_str(), f->ext.c_str(), j, n);
                    strcat(line, buff);
                }
        }
        // block marked in bitmap?
        used = bitmap.used[blknr];
        if ((!used && line[0]) || (used && !line[0])) {
            sprintf(buff, " Bitmap mismatch, marked as %s!", used ? "USED" : "NOT USED");
            strcat(line, buff);
        }
        if (line[0]) {
            int offset = (IMAGE_BLOCKNR2OFFSET(blknr) - IMAGE_BLOCKNR2OFFSET(0));
            fprintf(stream, "%5d @ 0x%06x = %#08o:	%s\n", blknr, offset, offset, line);
        }
    }
    n = xxdp_bitmap_count();
    fprintf(stream, "Blocks marked as \"used\" in bitmap: %d. Free: %d - %d = %d.\n", n,
            blockcount, n, blockcount - n);
#endif
}



} // namespace


