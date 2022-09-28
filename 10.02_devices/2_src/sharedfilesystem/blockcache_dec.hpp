/* blockcache_dec.hpp - memory cache for disk images with DEC filesystem


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

  10-sep-2022 JH      created

 */

#ifndef __BLOCKCACHE_DEC_HPP_
#define __BLOCKCACHE_DEC_HPP_

#include <stdint.h>
#include <string.h>
#include <string>
#include <iostream>
#include <assert.h>

#include "bytebuffer.hpp"
#include "filesystem_dec.hpp"

namespace sharedfilesystem {

// is a byte buffer, with position in disk image,
// represents data on a DEC operating system
class block_cache_dec_c: public byte_buffer_c
{
private:
    filesystem_dec_c *filesystem ;
    uint32_t	block_size ;

    uint32_t	start_block_nr ; // cached data starts here
    uint32_t	block_count ; // # of cached blocks

    // range of cached image
    uint64_t lo_cache_offset ;
    uint64_t hi_cache_offset ;

public:

    block_cache_dec_c(filesystem_dec_c *_filesystem) {
        filesystem = _filesystem ;
        block_size = filesystem->get_block_size() ;
    }


    ~block_cache_dec_c() override {
    }

    // link to image, but fill with 00 instead of loading data
    void init(uint32_t _image_block_nr, uint32_t _block_count)
    {
        start_block_nr = _image_block_nr ;
        block_count = _block_count ;
        // range of cached bytes
        lo_cache_offset = start_block_nr * block_size ; // 1st byte in cache
        hi_cache_offset = lo_cache_offset + (block_count * block_size) - 1 ; // last byte

        image_position =  _image_block_nr * block_size ;
        set_size(block_count * block_size) ;
        memset(data_ptr(), 0, size()) ;
    }


    void load_from_image(uint32_t _image__block_nr, uint32_t _block_count)
    {
        init(_image__block_nr, _block_count) ;

        filesystem->image_partition->get_bytes(this, _image__block_nr * block_size, _image__block_nr * block_size) ;
    }


    // cache back to image, free
    void flush_to_image() {
        filesystem->image_partition->set_bytes(this) ;
    }

    // get buffer address to cache data of an image position
    // assert, wether its in the cache
    uint8_t *get_image_addr(uint32_t _image_block_nr, uint32_t _byte_offset)
    {
        uint64_t abs_offset = _image_block_nr * block_size + _byte_offset;
        assert(abs_offset >= lo_cache_offset) ;
        assert(abs_offset <= hi_cache_offset) ;
        uint64_t rel_offset = abs_offset - lo_cache_offset ;
        return data_ptr()+rel_offset ;
    }

//#define IMAGE_GET_WORD(ptr) (  (uint16_t) ((uint8_t *)(ptr))[0]  |  (uint16_t) ((uint8_t *)(ptr))[1] << 8  )
//#define IMAGE_PUT_WORD(ptr,w) ( ((uint8_t *)(ptr))[0] = (w) & 0xff, ((uint8_t *)(ptr))[1] = ((w) >> 8) & 0xff  )

    // get word from image offset, check checks wether the word  is in cache
    uint16_t get_image_word_at(uint32_t _image_byte_offset)
    {
        return get_image_word_at(0, _image_byte_offset) ;
        /** all this is inlined, expansion with blcok_nr=0 quite compact ***/
    }

    // fetch/write a 16bit word in the image. checks wether the word is in cache
    // LSB first
    uint16_t get_image_word_at(uint32_t _image_block_nr, uint32_t _block_byte_offset)
    {
        uint8_t *addr = get_image_addr(_image_block_nr, _block_byte_offset) ;
        return (uint16_t)addr[0] | (uint16_t)(addr[1] << 8);
    }

    void set_image_word_at(uint32_t _image_byte_offset, uint16_t val)
    {
        set_image_word_at(0, _image_byte_offset, val) ;
    }

    void set_image_word_at(uint32_t _image_block_nr, uint32_t _block_byte_offset, uint16_t val)
    {
        uint8_t *addr = get_image_addr(_image_block_nr, _block_byte_offset) ;
        // assert((idx + 1) < image_partition_size);
        addr[0]	= val & 0xff;
        addr[1] = (val >> 8) & 0xff;
        //fprintf(flog, "set 0x%x to 0x%x\n", idx, val) ;
    }

    // import many bytes
    void set_image_bytes_at(uint32_t _image_byte_offset, byte_buffer_c *byte_buffer)
    {
        uint8_t *addr = get_image_addr(0, _image_byte_offset) ;
        // assert last byte in cache area
        get_image_addr(0, _image_byte_offset + byte_buffer->size() - 1) ;
        memcpy(addr, byte_buffer->data_ptr(), byte_buffer->size()) ;
    }


} ;

}
#endif



