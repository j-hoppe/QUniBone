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

class byte_buffer_c
{
private:
    uint8_t	*_data ;
    uint32_t _size ;

public:
    uint64_t image_position ; // start position in storage image

    byte_buffer_c() {
        _data = nullptr ;
        _size = 0 ;
    }


    virtual ~byte_buffer_c() {
        if (_data != nullptr)
            free (_data) ;
    }

    bool is_empty() {
        return (_size == 0) ;
    }


    // set, update or erase buffer size.
    void set_size(unsigned new_size) {
        if (new_size == _size)
            return ;
        if (new_size > 0)
            _data = (uint8_t *)realloc(_data, new_size) ;
        else if (_data) {
            // deallocate
            free(_data) ;
            _data = nullptr ;
        }
        _size = new_size ;
    }


    // several ways to set an empty or filled buffer
    void set(uint8_t *data, unsigned size) {
        set_size(size) ;
        assert(_data != nullptr) ;
        memcpy(_data, data, size);
    }

    // copy data_ptr
    void set(byte_buffer_c *bb) {
        set(bb->data_ptr(), bb->size()) ;
    }

// like strdup()
    void set(std::string *s) {
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
    void set(istream *st, unsigned new_size)
    {
        set_size(new_size) ;
        assert(_data != nullptr) ;
        st->seekg(0) ;
        st->read((char *)_data, new_size) ;
    }

    // write bytes to stream
    void get(ostream *st)
    {
        st->write((char *)_data, _size) ;
    }

    // accessors
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


// is memory all set with a const value?
// reverse oeprator to memset()
// size == 0: true
//int is_memset(void *ptr, uint8_t val, uint32_t size) {
    bool is_zero_data(uint8_t val)
    {
        uint8_t *ptr = _data ;
        if (is_empty())
            return true ;
        assert(_data != nullptr) ;
        for (unsigned size = _size; size; ptr++, size--)
            if (*ptr != val)
                return false;
        return true;
    }

} ;

#endif



