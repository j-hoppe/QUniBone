/* filesystem_rt11.hpp -  RT11 file tree

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
#ifndef _SHAREDFILESYSTEM_RT11_HPP_
#define _SHAREDFILESYSTEM_RT11_HPP_

#include <stdint.h>
#include <sstream>

#include "filesystem_dec.hpp"

namespace sharedfilesystem {


typedef uint16_t rt11_blocknr_t;

class file_rt11_c ;

// a stream of data
// - for the bootloader on a rt11 image
// - file data, -prefixes and extra dir entries
class rt11_stream_c: public file_dec_stream_c {
public:
    rt11_blocknr_t start_block_nr; // start block
    uint32_t	byte_offset ; // offset in start block
    rt11_stream_c(file_rt11_c *file,  	rt11_stream_c *stream) ;
    rt11_stream_c(file_rt11_c *file, string stream_name) ;
    ~rt11_stream_c() override ;
    void init() override ;

    string get_host_path() override ;
};



class file_rt11_c: public file_dec_c {
public:
    // filenames on Rt11 have afix 6.3 format, with trailing spaces
    // here basename and ext have trailing space removed.
    // however " EMPTY.FIL"" has a leading space!
    string basename; // DEC: normally 6 chars, encoded in 2 words RADIX50.

    string ext; // DEC: normally 3 chars, encoded 1 word

    file_rt11_c() ;
    file_rt11_c(file_rt11_c *f) ;

    ~file_rt11_c() override ;

    // streams only instantiated when part of file
    rt11_stream_c *stream_data;
    // stream_data in file stream_prefix blocks, if any
    rt11_stream_c *stream_prefix ;
    // extra bytes in extended parentdir entry, if any
    rt11_stream_c *stream_dir_ext ;

    uint16_t	status ;

    // position on disk from dir entry
    rt11_blocknr_t block_nr ; // start of stream_data on volume
    rt11_blocknr_t block_count ; // total blocks on volume (stream_prefix + stream_data)

    //
    string get_filename() override ;
    rt11_stream_c **get_stream_ptr(string stream_code) ;
    unsigned get_stream_count() override ;
    file_dec_stream_c *get_stream(unsigned index) override ;

    bool data_changed(file_base_c *cmp) override ;
} ;


// implement mandatory virtuals with default
// the root is only a place holder without info, never change
class directory_rt11_c: public directory_dec_c {
public:
    directory_rt11_c():directory_dec_c() {}
    directory_rt11_c(directory_rt11_c *d):directory_dec_c(d) {}
    bool data_changed(file_base_c *cmp) override
    {
        UNUSED(cmp);
        return true ;
    }
    string get_filename() override {
        return "RT11ROOT" ;
    }

    // RT11: no directory, no extra data for directory
    unsigned get_stream_count() override {
        return 0;
    }
    file_dec_stream_c *get_stream(unsigned index) override  {
        UNUSED(index);
        return nullptr;
    }

    void copy_metadata_to(directory_base_c *_other_dir) override ;
} ;


class filesystem_rt11_c: public filesystem_dec_c {

    typedef struct {
        enum dec_drive_type_e drive_type;
        // units are in drive_info_c.blocksize != sector size!

        unsigned block_size  ; // 512 bytes for all drives
        unsigned block_count ; // # of blocks RT-11 uses on disk surface
        unsigned first_dir_blocknr ; /// always 6 ?
        unsigned replacable_bad_blocks ;
        unsigned dir_seg_count ; // default segment count
    } layout_info_t ;

    layout_info_t layout_info ;
    layout_info_t get_documented_layout_info(enum dec_drive_type_e drive_type) ;


    unsigned pack_cluster_size; // Pack cluster size (== 1). Not used?
    rt11_blocknr_t first_dir_blocknr; // Block number of first directory segment
    string system_version ; // size 3, System version (Radix-50 "V3A"),
    string volume_id ; // size 12, Volume identification, 12 ASCII chars "RT11A" + 7 spaces
    string owner_name ; // size 12 Owner name, 12 spaces
    string system_id ; // size 12, System identification "DECRT11A" + 4 spaces
    uint16_t homeblock_chksum; // checksum of the home block

    // directory layout data
    uint16_t dir_total_seg_num ; // total number of segments in this directory
    uint16_t dir_max_seg_nr ; // number of highest segment in use (only 1st segment)
    uint16_t dir_entry_extra_bytes ; // number of extra bytes per dir entry

    bool struct_changed ; // directories or homeblock changed

public:
    filesystem_rt11_c(storageimage_partition_c *image_partition) ;
    ~filesystem_rt11_c() override ;

    void init() override ;
    void copy_metadata_to(filesystem_base_c *metadata_copy) override ;

    string get_label() override ;

    unsigned get_block_size() override {
        return layout_info.block_size ; // in bytes
    }

    // name of internal special files
    string bootblock_filename ;
    string monitor_filename ;
    string volume_info_filename ;

    // cache directory statistics
    unsigned		dir_file_count ; // # of files in the directory, without internal
    rt11_blocknr_t	used_file_blocks ;
    rt11_blocknr_t	free_blocks ;

    rt11_blocknr_t	file_space_blocknr ; // start of file space area
    rt11_blocknr_t	render_free_space_blocknr ; // start of free space for renderer

    static string make_filename(string basename, string ext) ;

    // low level operators
private:

    // RT11 has no subdirectories
    string get_filepath(file_base_c *f) override {
        return f->get_filename() ;
    }

    unsigned file_count() override {
        return rootdir->file_count() ;
    }

    void stream_parse(rt11_stream_c *stream, rt11_blocknr_t start, uint32_t byte_offset, uint32_t data_size) ;
    void stream_render(rt11_stream_c *stream) ;
    unsigned directory_entries_per_segment() ;
    unsigned directory_needed_segments(unsigned file_count) ;
    void calc_file_stream_change_flag(        rt11_stream_c *stream) ;
    void calc_change_flags() override  ;
    void calc_block_use(unsigned test_data_size) ;


private:

    void parse_internal_blocks_to_file(string _basename, string _ext, uint32_t start_block_nr, uint32_t data_size) ;
    bool parse_homeblock() ;
    void parse_directory() ;
    void parse_file_data() ;
    void parse_volumeinfo();
public:
    void parse()   override ;


private:
    void calc_layout() ;
    void render_homeblock() ;
    void render_directory_entry(byte_buffer_c &block_buffer, file_rt11_c *f, int ds_nr, int de_nr) ;
    void render_directory() ;
    void render_file_data() ;
public:
    void render() override ;


public:
    file_rt11_c *file_get(int fileidx)    override;

    string filename_from_host(string *hostfname, string *result_filnam, string *result_ext) override ;

    bool stream_by_host_filename(string host_fname,
                                 file_rt11_c **result_file, string *result_host_filename,
                                 rt11_stream_c **result_stream, string *result_stream_code) ;

    void import_host_file(file_host_c *host_file) override ;
    void delete_host_file(string host_path) override ;


    void sort() override ;

private:
    string rt11_date_text(struct tm t);
    string rt11_dir_entry_text(file_rt11_c *f) ;
public:


    void print_directory(FILE *stream) override ;
    void print_diag(FILE *stream) override;
};


} // namespace
#endif // _SHAREDFILESYSTEM_RT11_HPP_


