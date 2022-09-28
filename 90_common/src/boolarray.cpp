/* boolarray.cpp: large binary array
 *
 *  Copyright (c) 2021, Joerg Hoppe
 *  j_hoppe@t-online.de, www.retrocmp.com
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *  - Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  6-mar-2021  JH  migrated from tu58fs
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "boolarray.hpp"

boolarray_c::boolarray_c(uint32_t _bitcount) {
    bitcount = _bitcount;
    flags = (uint32_t *)malloc((bitcount / 32) + 1);
    clear();
}

boolarray_c::~boolarray_c() {
    free(flags);
}

void boolarray_c::clear() {
    memset(flags, 0, bitcount / 32 + 1);
}

void boolarray_c::bit_set(uint32_t i) {
    assert(i < bitcount);
    uint32_t w = flags[i / 32];
    w |= (1 << (i % 32));
    flags[i / 32] = w;
}

void boolarray_c::bit_clear(uint32_t i) {
    assert(i < bitcount);
    uint32_t w = flags[i / 32];
    w &= ~(1 << (i % 32));
    flags[i / 32] = w;
}

int boolarray_c::bit_get(uint32_t i) {
    assert(i < bitcount);
    uint32_t w = flags[i / 32];
    return !!(w & (1 << (i % 32)));
}

// dump state of first "actua_lbitcount" bits
// compressed display with "from-to" ranges
void boolarray_c::print_diag(FILE *stream, uint32_t actual_bitcount, char *info) {
    bool any = false;
    unsigned start, end;
    if (actual_bitcount > bitcount)
        actual_bitcount = bitcount ;
    fprintf(stream, "%s - Dump of boolarray@%p, bits 0..%d: ", info, this, actual_bitcount-1);
    start = 0;
    while (start < actual_bitcount) {
        // find next set bit
        while (start < actual_bitcount && !bit_get(start))
            start++;
        // find next clr bit
        end = start;
        while (end < actual_bitcount && bit_get(end))
            end++;
        if (start < actual_bitcount) {
            if (!any)
                fprintf(stream, "bits set =\n");
            else
                fprintf(stream, ",");
            if (end - start > 1)
                fprintf(stream, "%d-%d", start, end - 1);
            else
                fprintf(stream, "%d", start);
            any = true;
        }
        start = end;
    }
    if (!any)
        fprintf(stream, "no bits set.\n");
    else
        fprintf(stream, ".\n");
}

