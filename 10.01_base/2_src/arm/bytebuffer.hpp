/* bytebuffer.hpp - secure uint8_t[]


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

#ifndef __BYTEBUFFER_HPP_
#define __BYTEBUFFER_HPP_

#include <stdint.h>
#include <string.h>
#include <string>
#include <iostream>
#include <assert.h>
#include <iostream>

class byte_buffer_c
{
private:
    uint8_t	*_data ;
    uint32_t _size ;

public:
    uint64_t image_position ; // start position in storage image

    uint8_t	zero_byte_val ; // invalid memory is initialized with this value

    byte_buffer_c() ;
    virtual ~byte_buffer_c() ;

    bool is_empty() ;

    void set_size(unsigned new_size) ;
    void set(uint8_t *src_data, unsigned src_size) ;
    void set(byte_buffer_c *bb) ;
    void set(std::string *s) ;
    void set(std::istream *st, unsigned new_size) ;

    void get(std::ostream *st) ;

    bool is_zero_data(uint8_t val) ;

    // inline accessors
    uint8_t *data_ptr() {
        return _data ;
    }

    unsigned size() {
        return _size ;
    }

    uint8_t& operator [](unsigned idx) {
        // assert(idx >= 0) ;
        assert(idx < _size) ;
        assert(_data != nullptr) ;
        return _data[idx];
    }

    uint8_t operator [](unsigned idx) const {
        // assert(idx >= 0) ;
        assert(idx < _size) ;
        assert(_data != nullptr) ;
        return _data[idx];
    }



} ;

#endif



