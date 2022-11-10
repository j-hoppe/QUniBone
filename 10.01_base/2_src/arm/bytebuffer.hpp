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

#include "utils.hpp" // endianness


class byte_buffer_c
{
private:
    enum endianness_e endianness ;
    uint8_t	*_data ;
    uint32_t _size ;

public:
    uint8_t	zero_byte_val ; // invalid memory is initialized with this value

    byte_buffer_c() ;
    byte_buffer_c(enum endianness_e _endianness) ;
    virtual ~byte_buffer_c() ;

    // "Rule of Three": manages buffer, needs copy and copy assignment
    // https://en.wikipedia.org/wiki/Rule_of_three_(C%2B%2B_programming)
    // https://stackoverflow.com/questions/4172722/what-is-the-rule-of-three

    byte_buffer_c(const byte_buffer_c& other) ;// copy constructor
    byte_buffer_c& operator=(const byte_buffer_c& other) ;// copy assignment


    bool is_empty() const {
        return (_size == 0) ;
    }


    void set_size(unsigned new_size) ;
    void set_data(uint8_t *src_data, unsigned src_size) ;
    void set_data(const byte_buffer_c *bb) ;
//	void set_data(const byte_buffer_c &bb) ;

    void set_data(std::string *s) ;
    void set_data(std::istream *st, unsigned new_size) ;

    void get_data(std::ostream *st) ;

    void init_zero(unsigned new_size) ;

    bool is_zero_data(uint8_t val) const ;

    // inline accessors
    uint8_t *data_ptr() const {
        return _data ;
    }

    unsigned size() const {
        return _size ;
    }

    uint8_t& operator [](unsigned _byte_offset) {
        // assert(idx >= 0) ;
        assert(_byte_offset < _size) ;
        assert(_data != nullptr) ;
        return _data[_byte_offset];
    }

    uint8_t operator [](unsigned _byte_offset) const {
        // assert(idx >= 0) ;
        assert(_byte_offset < _size) ;
        assert(_data != nullptr) ;
        return _data[_byte_offset];
    }

    // PDP11 endianness
    uint16_t get_word_at_byte_offset(uint32_t _byte_offset) ;
    void set_word_at_byte_offset(uint32_t _byte_offset, uint16_t val) ;
    void set_bytes_at_byte_offset(uint32_t _byte_offset, const byte_buffer_c *bb) ;
    uint16_t get_word_at_word_offset(uint32_t _word_offset) {
        return get_word_at_byte_offset(2 * _word_offset) ;
    }
    void set_word_at_word_offset(uint32_t _word_offset, uint16_t val) {
        set_word_at_byte_offset(2 * _word_offset, val) ;
    }


    /*
    	uint8_t	*get_addr(uint32_t _byte_offset) {
    		assert(_byte_offset < _size) ;
    		return _data + _byte_offset ;
    	}
    */


} ;

#endif



