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
#include <string.h>

#include <string>
#include <algorithm> // TRIM_STRING



#define MILLION 1000000LL
#define BILLION (1000LL * MILLION)

#define BIT(n) (1 << (n))



enum endianness_e {
    endianness_little, // lsb first
    endianness_pdp11, // DEC: word = LSB first, dword: 0x01020304 => 02, 01, 04, 03
    endianness_big // msb first
} ;


#ifndef _UTILS_CPP_
extern volatile int SIGINTreceived;
#endif

// my version
void strcpy_s(char *dest, int len, const char *src);

// mark unused parameters
// http://stackoverflow.com/questions/1486904/how-do-i-best-silence-a-warning-about-unused-variables
//#define UNUSED(x) (void)(x)
#define UNUSED(expr) do { (void)(expr); } while (0)

#define USE(x) (void)(x)

// string identifying source file and line
// https://www.decompile.com/cpp/faq/file_and_line_error_string.htm
#define STRINGIFY(x) #x    // a trick or de facto standard?
#define TOSTRING(x) STRINGIFY(x)
#define __FILE__LINE__ __FILE__ ":" TOSTRING(__LINE__)


void SIGINTcatchnext();

// dummy to have an executable line for break points
void break_here(void);


class printf_exception: public std::exception {
private:
    std::string message;
public:
    printf_exception(std::string msgfmt, ...) ;
    virtual const char* what() const noexcept {
        return message.c_str();
    }
};


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

inline struct tm null_time()
{
    struct tm result ;
    memset(&result, 0, sizeof(result)) ;
    return result ;
}

bool is_leapyear(int y) ;
extern int monthlen_noleapyear[12] ;
extern int monthlen_leapyear[12] ;

char *cur_time_text(void);

// remove leading/trailing spaces
// https://stackoverflow.com/questions/83439/remove-spaces-from-stdstring-in-c
#define TRIM_STRING(str) str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end())

char *strtrim(char *txt);
void ltrim(std::string &s) ;
void rtrim(std::string &s) ;
void trim(std::string &s) ;
std::string ltrim_copy(std::string s) ;
std::string rtrim_copy(std::string s) ;
std::string trim_copy(std::string s) ;
bool caseInsensStringCompare(std::string &str1, std::string &str2);

//char *printf_to_cstr(const char *fmt, ...) ;
std::string printf_to_string(const char *fmt, ...) ;




char * fileErrorText(const char *msgfmt, const char *fname);

bool is_memset(uint8_t *ptr, uint8_t val, uint32_t size);
bool is_fileset(std::string *fpath, uint8_t val, uint32_t offset);

void split_path(std::string path, std::string *directory, std::string *filename, std::string *basename, std::string *extension) ;
void split_path_test() ;

std::string absolute_path(std::string *path) ;

int file_write(char *fpath, uint8_t *data, unsigned size) ;

// 1, if path/filename exists
//int file_exists(char *path, char *filename) ;
bool file_exists(std::string *filename);
bool file_exists(std::string *path, std::string *filename) ;

std::string rad50_decode(uint16_t w);
uint16_t rad50_encode(std::string s);



void hexdump(std::ostream &stream, uint8_t *data, int size, const char *fmt, ...);


//ool caseInsCompare(const std::string& s1, const std::string& s2) ;

// add a number of microseconds to a time
struct timespec timespec_add_us(struct timespec ts, unsigned us);
// add microseconds to current time
struct timespec timespec_future_us(unsigned offset_us);

uint64_t now_ms(void) ;

// decodes C escape sequences \char, \nnn octal, \xnn hex
bool str_decode_escapes(char *result, unsigned result_size, char *encoded) ;

int	rangeToMinMax(int val, int min, int max) ;


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
