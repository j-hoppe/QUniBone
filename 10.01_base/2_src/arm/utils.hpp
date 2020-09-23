/* utils.hpp: misc. utilities

 Copyright (c) 2018, Joerg Hoppe
 j_hoppe@t-online.de, www.retrocmp.com

 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 JOERG HOPPE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


 12-nov-2018  JH      entered beta phase
 20-may-2018  JH      created
 */

#ifndef _UTILS_HPP_
#define _UTILS_HPP_

#include <stdint.h>
#include <stdbool.h>

#include <string>
#include <algorithm> // TRIM_STRING



#define MILLION 1000000LL
#define BILLION (1000LL * MILLION)

#define BIT(n) (1 << (n))


#ifndef _UTILS_CPP_
extern volatile int SIGINTreceived;
#endif

// my version
void strcpy_s(char *dest, int len, const char *src);

// mark unused parameters
#define UNUSED(x) (void)(x)

#define USE(x) (void)(x)

void SIGINTcatchnext();

// dummy to have an executable line for break points
void break_here(void);


class progress_c {
private:
	unsigned linewidth;
	unsigned cur_col;
public:
	progress_c(unsigned linewidth);
	void init(unsigned linewidth);
	void put(const char *info);
	void putf(const char *fmt, ...);

};

unsigned random24(void);
uint32_t random32_log(uint32_t limit);

char *cur_time_text(void);

// remove leading/trailing spaces
// https://stackoverflow.com/questions/83439/remove-spaces-from-stdstring-in-c
#define TRIM_STRING(str) str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end())

bool fileExists(const std::string& filename);

char * fileErrorText(const char *msgfmt, const char *fname);

//ool caseInsCompare(const std::string& s1, const std::string& s2) ;

// add a number of microseconds to a time
struct timespec timespec_add_us(struct timespec ts, unsigned us);
// add microseconds to current time
struct timespec timespec_future_us(unsigned offset_us);

// decodes C escape sequences \char, \nnn octal, \xnn hex
bool str_decode_escapes(char *result, unsigned result_size, char *encoded) ;


// rotating set of buffers for "number to text" functions
class rolling_text_buffers_c {
	private:
	static const unsigned buffer_count = 16 ;
	char buffer[buffer_count][256] ;
	unsigned buffer_idx ;
public:	
	rolling_text_buffers_c() {
		buffer_idx=0 ;
	}

	char *get_next() {
		buffer_idx = (buffer_idx + 1) % buffer_count ;
		return buffer[buffer_idx] ;
	}
} ;
	
extern rolling_text_buffers_c rolling_text_buffers ; // singleton


#endif /* _UTILS_H_ */
