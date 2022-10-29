/* filesystem_xxdp.hpp -  XXDP file system

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


  06-jan-2022 JH      created
 */
#ifndef _SHAREDFILESYSTEM_XXDP_HPP_
#define _SHAREDFILESYSTEM_XXDP_HPP_

#include <stdint.h>
#include <vector>
#include <iostream>

#include "blockcache_dec.hpp"
#include "filesystem_dec.hpp"
#include "filesystem_xxdp.hpp"

namespace sharedfilesystem {

/* Logical structure of XXDP filesystem.
*  See
 * CHQFSA0 XXDP+ FILE STRUCT DOC
 *
 */
// static const unsigned XXDP_BLOCKSIZE = 512 ; const from layoutinfo
static const unsigned XXDP_MAX_BLOCKCOUNT = 0x10000 ; // block addr only 16 bit

// layout data not in .layout_info
static const unsigned XXDP_BITMAP_WORDS_PER_MAP = 60 ; // 1 map = 16*60 = 960 block bits
static const unsigned XXDP_UFD_ENTRY_WORDCOUNT = 9 ; // len of UFD entry
static const unsigned XXDP_UFD_ENTRIES_PER_BLOCK = 28 ; // 29 file entries per UFD block
// own limits
static const unsigned XXDP_MAX_FILES_PER_IMAGE = 2000 ;	// all xxdp, xxdp22, xxdp25 files
static const unsigned XXDP_MAX_BLOCKS_PER_LIST = 1024 ; //  own: max filesize: * 510

typedef uint16_t xxdp_blocknr_t ;

class file_xxdp_c ;
class filesystem_xxdp_c ;


/*
// a range of block which is treated as one byte stream, not a linked block list.
// - for the bootloader on a xxdp image
// - for the monitor
class xxdp_contiguous_file_c: public byte_buffer_c {
public:
    xxdp_blocknr_t	start_block_nr ;
    xxdp_blocknr_t	block_count ;
};
*/
// a list of block addresses
class xxdp_block_nr_list_c: public vector<xxdp_blocknr_t>  {	} ; // alias


// a single block iamge cache
class xxdp_linked_block_c: public block_cache_dec_c {
public:
    xxdp_linked_block_c(filesystem_xxdp_c *filesystem) ;
    /*
    		unsigned block_nr ; // # of block on image

    	// return a word from the byte buffer, LSB first
    	uint16_t get_word_at_byte_offset(uint32_t _byte_offset) {
    		assert(_byte_offset < size()) ;
    		uint8_t *addr = data_ptr() + _byte_offset ;
    		// LSB first
    		return (uint16_t)addr[0] | (uint16_t)(addr[1] << 8);
    	}
    	uint16_t get_word_at_word_offset(unsigned _word_offset) {
    		return get_word_at_byte_offset(2*_word_offset) ;
    	}

    	void set_word_at_byte_offset(uint32_t _byte_offset, uint16_t val) {
    		assert(_byte_offset < size()) ;
    		uint8_t *addr = data_ptr() + _byte_offset ;
    		addr[0] = val & 0xff;
    		addr[1] = (val >> 8) & 0xff;
    	}

    	void set_word_at_word_offset(uint32_t _word_offset, uint16_t val) {
    		set_word_at_byte_offset(2 * _word_offset, val) ;
    	}
    */
    xxdp_blocknr_t get_block_nr() {
        return start_block_nr ;
    }

    xxdp_blocknr_t get_next_block_nr() {
        return get_word_at_word_offset(0) ;
    }


};

// linked list of logical blocks, as full in-memory copy
// 1st word in each block is nr of next lock, 0 terminates
class xxdp_linked_block_list_c: public vector<xxdp_linked_block_c>  {
    // xxdp_block_nr_list_c block_nr_list ;

    filesystem_xxdp_c *filesystem ;

public:
    void init(filesystem_xxdp_c *_filesystem) {
        filesystem = _filesystem ;
        clear() ;
    }

    void clear() {
        vector<xxdp_linked_block_c>::clear() ;
    }

    unsigned size() {
        return (unsigned) vector<xxdp_linked_block_c>::size() ;
    }

// not implementable, block_cache constructor needs filesystem argument
//    void resize(unsigned count) {
//        vector<block_cache_dec_c>::resize(count) ;
//    }

    xxdp_linked_block_c *next_block(xxdp_linked_block_c *block) {
        xxdp_blocknr_t block_nr = block->get_next_block_nr() ;
        if (block_nr == 0)
            return nullptr ;
        return get_block_by_nr(block_nr) ;
    }


    xxdp_linked_block_c *get_block_by_nr(xxdp_blocknr_t block_nr);

//    xxdp_blocknr_t get_next_block_nr(xxdp_blocknr_t block_nr) ;

    void add_empty_block(xxdp_blocknr_t block_nr) ;
    void add_empty_blocks(xxdp_block_nr_list_c *block_nr_list) ;

    void load_from_image(xxdp_blocknr_t start_block_nr) ;
    void write_to_image() ;
    void write_to_file_buffer(file_xxdp_c *f) ;
    void load_from_file_buffer(file_xxdp_c *f) ;

    void verify() ;

    // build
    void get_block_nr_list(xxdp_block_nr_list_c *block_nr_list);


    void print_diag(std::ostream &stream, const char *info) ;

} ;


// boolean marker for block usage
class xxdp_bitmap_c {
    filesystem_xxdp_c *filesystem ;
public:
    xxdp_linked_block_list_c	block_list ;
    bool used[XXDP_MAX_BLOCKCOUNT]; // later vector<uint8_t>

    void init(filesystem_xxdp_c *_filesystem)	{
        filesystem = _filesystem ;
        block_list.init(_filesystem) ;
        clear() ;
    }

    void clear() ;

    int used_block_count() ;
} ;


// a XXDP file has not multiple streams, so it is itself one
class file_xxdp_c: public file_dec_c, public file_dec_stream_c {
public:
    // these blocks are allocated, but no necessarily all used
    string basename;	// DEC: normally 6 chars, encoded in 2 words RADIX50. Special filenames longer
    string ext; // DEC: normally 3 chars, encoded 1 word

    // two types of files: linkend block lists, and sequential bytes for boot block and monitor
    bool is_contiguous_file ; // false: block list.

    file_xxdp_c() ;
    file_xxdp_c(file_xxdp_c *f) ;
    virtual ~file_xxdp_c() override ;

    xxdp_blocknr_t start_block_nr; // start block, from UFD
    xxdp_blocknr_t last_block_nr ; // from UFD

// these blocks are allocated, but no necessarily all used
    xxdp_block_nr_list_c block_nr_list; // if linked block list
//    bool contiguous; //	1: blocknumbers are numbered start, start+1, start+2, ...
    //	char basename[80];  // normally 6 chars, encoded in 2 words RADIX50. Special filenames longer
    //	char ext[40]; // normally 3 chars, encoded 1 word
    xxdp_blocknr_t block_count ; // saved blockcount from UFD.
    // UFD should not differ from blocklist.count !
    //	uint32_t data_size; // byte count in data_ptr[]
    //	uint8_t *data_ptr; // dynamic array with 'get_size' entries
    //	struct tm date; // file date. only y,m,d valid
    //	uint8_t changed ; // calc'd from image_changed_blocks
    //	int internal ; // is part of filesystem, can not be deleted

    string get_filename() override ;
    string get_host_path() override ;
    bool data_changed(file_base_c *cmp) override ;

    // has only 1 stream: itself
    unsigned get_stream_count() override {
        return 1 ;
    }
    file_dec_stream_c *get_stream(unsigned index) override {
        return index==0 ? this: nullptr ;
    }

};

// implement mandatory virtuals with default
// the root is only a place holder without info, never change
class directory_xxdp_c: public directory_dec_c {
public:
    directory_xxdp_c(): directory_dec_c() {}
    directory_xxdp_c(directory_xxdp_c *d): directory_dec_c(d) {}

    string get_filename() override {
        return "XXDPROOT" ;
    }

    bool data_changed(file_base_c *cmp) override
    {
        UNUSED(cmp);
        return true ;
    }

    // XXDP: no directory, no extra data for directory
    unsigned get_stream_count() override {
        return 0;
    }
    file_dec_stream_c *get_stream(unsigned index) override  {
        UNUSED(index);
        return nullptr;
    }


    void copy_metadata_to(directory_base_c *_other_dir) override ;
} ;



class filesystem_xxdp_c: public filesystem_dec_c {
private:
    static struct tm dos11date_decode(uint16_t w);
    static uint16_t dos11date_encode(struct tm t);
public:
    static string make_filename(string basename, string ext) ;

private:
    typedef struct {
        enum dec_drive_type_e drive_type;
        unsigned block_size	; // 512 bytes for all drives

        // units are in block_size != sector size!
        unsigned ufd_block_1; // 1st UFD block
        unsigned ufd_blocks_num; // number of ufd blocks
        unsigned bitmap_block_1; // 1st bit map block
        unsigned bitmap_block_count; // number of bitmaps
        unsigned mfd1;
        int mfd2; // -1 encodes
        unsigned blocks_num; // # of blocks XXDP uses
        unsigned prealloc_blocks_num; // number of blocks to preallocate
        unsigned interleave;
        unsigned boot_block_nr;
        unsigned monitor_core_image_start_block_nr;
        unsigned monitor_block_count ; // later deduced
    } layout_info_t ;


    layout_info_t layout_info ;
    layout_info_t get_documented_layout_info(enum dec_drive_type_e drive_type) ;
    void  recalc_layout_info(unsigned blockcount) ;


    xxdp_blocknr_t preallocated_blockcount ; // fix blocks at start
    xxdp_blocknr_t monitor_start_block_nr ; // may be dynamic? override static layout info?
    xxdp_blocknr_t monitor_max_block_count ; // calced from start to end of preallocated zone?
    unsigned interleave;
    int mfd_variety; // Master File Directory in format 1 or format 2?

    // in-memory copies of system areas
    xxdp_bitmap_c bitmap;
    // Data in  Master File Directory
    // format 1: linked list of 2.  Variant 2: only 1 block
    xxdp_linked_block_list_c  mfd_block_list;

    // blocks used by User File Directory
    xxdp_linked_block_list_c ufd_block_list;

    // XXDP has no subdirectories
    virtual string get_filepath(file_base_c *f) override {
        return f->get_filename() ;
    }

    unsigned file_count() {
        return rootdir->file_count() ;
    }

    bool struct_changed ; // directories or homeblock changed

public:
    filesystem_xxdp_c(drive_info_c &drive_info, unsigned drive_unit, storageimage_base_c *image_partition,   	 uint64_t image_partition_size) ;

    ~filesystem_xxdp_c() override ;

    string get_name() override {
        return "XXDP" ;
    }

    unsigned get_block_size() override {
        return layout_info.block_size ;// in bytes
    }

    // name of internal special files
    string bootblock_filename ;
    string monitor_filename ;
    string volume_info_filename ;

    virtual void init() override ;
    void copy_metadata_to(filesystem_base_c *metadata_copy) override ;

    // low level image access operators
private:
    /* see block_cache_dec_c accessors
        uint16_t xxdp_image_get_word(xxdp_blocknr_t blocknr,
                                     uint8_t wordnr) ;
        void xxdp_image_set_word(xxdp_blocknr_t blocknr, 		uint8_t wordnr, uint16_t val) ;
    */
    bool is_blocklist_changed(xxdp_linked_block_list_c *block_list) ;
    bool is_file_blocklist_changed(file_xxdp_c *f) ;
    void calc_change_flags() override ;

    // layout of objects on image
    void layout_test(int data_size) ;
    void calc_layout() ;


    // parser
private:
    void parse_mfd_load_bitmap_ufd() ;
    void parse_bitmap() ;
    void parse_internal_contiguous_file(string _basename, string _ext,
                                        xxdp_blocknr_t _start_blocknr, xxdp_blocknr_t _block_count)	 ;
    void parse_ufd() ;
    void parse_file_data(file_xxdp_c *f) ;

public:
    void parse() override ;

    // renderer
private:
    void render_bitmap() ;
    void render_mfd() ;
    void render_ufd() ;
    void render_contiguous_file_data(file_xxdp_c *f) ;
    void render_file_data() ;

public:
    void render() override ;

    // files
    void parse_volumeinfo() ;

    void import_host_file(file_host_c *host_file) override ;
    void delete_host_file(string host_path) override ;

    file_xxdp_c *file_get(int fileidx) override ;

    string filename_from_host(string *hostfname, string *result_filnam, string *result_ext) override ;

    void sort() override ;

private:
    string date_text(struct tm t) ;
    string directory_text_line(int fileidx) ;
public:
    void print_directory(FILE *stream) override ;
    void print_diag(FILE *stream) override;

};

} // namespace
#endif // _SHAREDFILESYSTEM_XXDP_HPP_


