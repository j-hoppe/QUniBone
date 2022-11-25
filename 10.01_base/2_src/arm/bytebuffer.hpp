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


// most code here, so much inlining
class byte_buffer_c
{
private:
    enum endianness_e endianness ;
    uint8_t	*_data ;
    uint32_t _size ;

public:
    uint8_t	zero_byte_val ; // invalid memory is initialized with this value

    byte_buffer_c()
    {
        _data = nullptr ;
        _size = 0 ;
        zero_byte_val = 0;
        endianness = endianness_pdp11 ;
    }

    byte_buffer_c(enum endianness_e _endianness): byte_buffer_c()
    {
        endianness = _endianness ;
    }


    virtual ~byte_buffer_c()
    {
        if (_data != nullptr)
            free (_data) ;
        _data = nullptr ;
        _size = 0 ;
    }


    // "Rule of Three": manages buffer, needs copy and copy assignment
    // https://en.wikipedia.org/wiki/Rule_of_three_(C%2B%2B_programming)
    // https://stackoverflow.com/questions/4172722/what-is-the-rule-of-three

    // copy constructor
    byte_buffer_c(const byte_buffer_c& other): byte_buffer_c()
    {
        zero_byte_val = other.zero_byte_val;
        set_data(&other) ; // copy
    }

    // copy assignment
    byte_buffer_c& operator=(const byte_buffer_c& other)
    {
        if (this == &other)
            return *this ;
        zero_byte_val = other.zero_byte_val;
        set_data(&other) ; // copy
        return *this ;
    }

    bool is_empty() const {
        return (_size == 0) ;
    }

    // set, update or erase buffer size.
    // intialise new data with "zero_byte_val"
    void set_size(unsigned new_size)
    {
        if (new_size == _size)
            return ;
        if (new_size > 0) {
            _data = (uint8_t *)realloc(_data, new_size) ;
            assert(_data != nullptr) ;
        }
        else if (_data) {
            // deallocate
            free(_data) ;
            _data = nullptr ;
        }
        if (new_size > _size) {
            // init new bytes
            size_t new_byte_count = new_size - _size;
            uint8_t* new_data_start = _data + _size;
            memset(new_data_start, zero_byte_val, new_byte_count);
        }
        _size = new_size ;
    }


    // several ways to set an empty or filled buffer
    void set_data(uint8_t *src_data, unsigned src_size)
    {
        set_size(src_size) ;
        assert(_data != nullptr) ;
        memcpy(_data, src_data, src_size);
    }

    // copy data_ptr
    void set_data(const byte_buffer_c *bb)
    {
        set_data(bb->data_ptr(), bb->size()) ;
    }
    /*
    void byte_buffer_c::set(const byte_buffer_c &bb) {
    	set(bb.data_ptr(), bb.size()) ;
    }
    */

    // like strdup()
    void set_data(std::string *s)
    {
        unsigned new_size = s->size() +1 ; // include \0
        set_size(new_size) ;
        assert(_data != nullptr) ;
        memcpy((void *)_data, (void *)s->c_str(), new_size) ;
    }
    /*
    	// like strdup()
    	void set(char *s) {
    		unsigned new_size = strlen(s) +1 ; // include \0
    		set_size(new_size) ;
    		memcpy((void *)_data, (void *)s, new_size) ;
    	}
    */

    // read first "size" bytes into the array. stream must be opem, position is changed
    void set_data(std::istream *st, unsigned new_size)
    {
        set_size(new_size) ;
        assert(_data != nullptr) ;
        st->seekg(0) ;
        st->read((char *)_data, new_size) ;
    }



// write bytes to stream
    void get_data(std::ostream *st)
    {
        st->write((char *)_data, _size) ;
    }

    void init_zero(unsigned new_size)
    {
        set_size(new_size) ;
        memset(data_ptr(), zero_byte_val, size()) ;
    }


    // is memory all set with a const value?
    // reverse operator to memset()
    // size == 0: true
    //int is_memset(void *ptr, uint8_t val, uint32_t size) {
    bool is_zero_data(uint8_t val) const
    {
        uint8_t *ptr = _data ;
        if (is_empty())
            return true ;
        assert(_data != nullptr) ;
        for (unsigned n = _size; n; ptr++, n--)
            if (*ptr != val)
                return false;
        return true;
    }

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

    // block relative
    uint16_t get_word_at_byte_offset(uint32_t _byte_offset) const
    {
        assert(_byte_offset < _size) ;
        assert(endianness == endianness_pdp11) ; // todo: implement other?
        uint8_t *addr = data_ptr() + _byte_offset ;
        // LSB first
        return (uint16_t)addr[0] | (uint16_t)(addr[1] << 8);
    }

    void set_word_at_byte_offset(uint32_t _byte_offset, uint16_t val)
    {
        assert(_byte_offset < _size) ;
        assert(endianness == endianness_pdp11) ; // todo: implement other?
        uint8_t *addr = data_ptr() + _byte_offset ;
        addr[0] = val & 0xff;
        addr[1] = (val >> 8) & 0xff;
    }


    void set_bytes_at_byte_offset(uint32_t _byte_offset, const byte_buffer_c *bb)
    {
        uint8_t *dest_addr = &_data[_byte_offset] ;
        // assert last byte in cache area
        unsigned last_byte_offset = _byte_offset + bb->size() - 1 ;
        assert(last_byte_offset < _size) ;
        memcpy(dest_addr, bb->data_ptr(), bb->size()) ;
    }


    uint16_t get_word_at_word_offset(uint32_t _word_offset) const
    {
        return get_word_at_byte_offset(2 * _word_offset) ;
    }
    void set_word_at_word_offset(uint32_t _word_offset, uint16_t val) {
        set_word_at_byte_offset(2 * _word_offset, val) ;
    }


} ;

#endif



