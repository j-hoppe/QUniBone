/* utils.cpp: misc. utilities

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

#define _UTILS_CPP_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <algorithm>
#include <string>
#include <cctype>
#include <cstdio>
#include <cstdarg>
#include <memory>
#include <iostream>




//#include "main.h" // linewidth
#include "logger.hpp"
#include "utils.hpp"


// singleton
rolling_text_buffers_c rolling_text_buffers ;


/*********************************
 * strcpy without buffer overlfow
 */
void strcpy_s(char *dest, int len, const char *src)
{
    strncpy(dest, src, len - 1);
    dest[len - 1] = 0; // termiante if truncated
}

/*********************************
 *  catching ^C
 */
volatile int SIGINTreceived = 0;

static void SIGINThandler(int dummy __attribute__((unused)))
{
    SIGINTreceived++;
    // detach signal handler, only one ^C is caught
    signal(SIGINT, NULL);
}

// catch the next SIGINT = ^C
void SIGINTcatchnext()
{
    signal(SIGINT, SIGINThandler); // setup handler for ^C
    SIGINTreceived = 0;
}

void break_here(void)
{
}


// exception constructor with printf() arguments
printf_exception::printf_exception(const std::string msgfmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, msgfmt);
    vsprintf(buffer, msgfmt.c_str(), args);
    message = std::string(buffer) ;
    va_end(args);
}


// progress: print progress text, break lines
progress_c::progress_c(unsigned _linewidth)
{
    linewidth = _linewidth;
    cur_col = 0;
}

void progress_c::init(unsigned _linewidth)
{
    linewidth = _linewidth;
    cur_col = 0;
}

void progress_c::put(const char *info)
{
    cur_col += strlen(info);
    if (cur_col >= linewidth) {
        printf("\n");
        cur_col = strlen(info);
    }
    printf("%s", info);
    fflush(stdout);
}
void progress_c::putf(const char *fmt, ...)
{
    static char buffer[256];
    va_list arg_ptr;

    va_start(arg_ptr, fmt);
    vsprintf(buffer, fmt, arg_ptr);
    va_end(arg_ptr);

    put(buffer);
}




/* random number with 24 valid bits
 * RAND_MAX is only guaranteed 15 bits
 */
unsigned random24()
{
    unsigned val;
    assert(RAND_MAX >= 0x3fff);
    val = rand() ^ (rand() << 9);
    return val & 0xffffff;
}

/* random numbers, distributed logarithmically
 * returns 0..limit-1
 */
uint32_t random32_log(uint32_t limit)
{
    uint32_t result, mantissa;
    int rand_exponent, limit_exp;
    assert(limit > 0);
    assert(RAND_MAX >= 0x3fff); // 15 bits
    // generate normalized mantissa, bit 31 set
    mantissa = rand();
    mantissa ^= (rand() << 9);
    mantissa ^= (rand() << 18);
    while ((mantissa & (1 << 31)) == 0)
        mantissa <<= 1;
    // rand_exponent of limit: 2^limit_exp <= limit
    // ctz = "Count Leading Zeros"
    // limit = 1 -> exp=0, limit = 0xffffffff -> exp = 31
    limit_exp = 31 - __builtin_clz(limit);
    limit_exp++; // 2^limit_exp >= limit

    // random rand_exponent 0..limit-1
    rand_exponent = rand() % limit_exp;
    // 2^rand_exponent <= limit
    result = mantissa >> (31 - rand_exponent);
    // mantissa has bit 31 set, is never shifted more then 31
    assert(result);

    // final masking
    if (limit > 1)
        result %= limit;
    return result;
}



bool is_leapyear(int y) {
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

int monthlen_noleapyear[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
int monthlen_leapyear[12]   = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };


char *cur_time_text()
{
    static char result[80], millibuff[10];
    timeval cur_time;
    gettimeofday(&cur_time, NULL);
    int millis = cur_time.tv_usec / 1000;
    strftime(result, 26, "%H:%M:%S", localtime(&cur_time.tv_sec));
    sprintf(millibuff, ".%03d", millis);
    strcat(result, millibuff);
    return result;
}



// return result from big static buffer
char *strtrim(char *txt)
{
    static char buff[1024];
    char *s = txt; // start

    assert(strlen(txt) < sizeof(buff));

    // skip leading space
    while (*s && isspace(*s))
        s++;
    strcpy(buff, s); // buff now without leading space
    // s = last last non-white char
    s = buff + strlen(buff) - 1;
    while (s > buff && isspace(*s))
        s--;
    *(s + 1) = 0; // clip off
    return buff;
}


// trim from start (in place)
void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch)
    {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch)
    {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

// trim from start (copying)
std::string ltrim_copy(std::string s)
{
    ltrim(s);
    return s;
}

// trim from end (copying)
std::string rtrim_copy(std::string s)
{
    rtrim(s);
    return s;
}

// trim from both ends (copying)
std::string trim_copy(std::string s)
{
    trim(s);
    return s;
}

// https://thispointer.com/c-case-insensitive-string-comparison-using-stl-c11-boost-library/
// C++ 11
bool caseInsensStringCompare(std::string & str1, std::string &str2)
{
    return ((str1.size() == str2.size())
    && std::equal(str1.begin(), str1.end(), str2.begin(), [](char & c1, char & c2) {
        return (c1 == c2 || std::toupper(c1) == std::toupper(c2));
    }
                         ));
}


// printf for strings
// https://codereview.stackexchange.com/questions/187183/create-a-c-string-using-printf-style-formatting
char *printf_to_cstr(const char *fmt, ...)
{
    static char buf[1024];
    va_list args;
    va_start(args, fmt);
    auto r = std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    assert(r >= 0 &&  r < (int)sizeof(buf)) ; // no error, no overflow
    return buf ;
}


std::string printf_to_string(const char *fmt, ...)
{
    char buf[1024];

    va_list args;
    va_start(args, fmt);
    auto r = std::vsnprintf(buf, sizeof buf, fmt, args);  // save truncate on overflow
    va_end(args);

    assert(r >= 0 &&  r < (int)sizeof(buf)) ; // no error, no overflow
    if (r < 0)
        // conversion failed
        return {};

    const size_t len = r;
    if (len < sizeof buf)
        // we fit in the buffer
        return { buf, len };

    // C++11 or C++14: We need to allocate scratch memory
    auto vbuf = std::unique_ptr<char[]>(new char[len+1]);
    va_start(args, fmt);
    std::vsnprintf(vbuf.get(), len+1, fmt, args);
    va_end(args);

    return { vbuf.get(), len };
}


// Generates "perror()" printout,
// msgfmt must have one "%s" field for absolute filename
char *fileErrorText(const char *msgfmt, const char *fname)
{
    static char linebuff[PATH_MAX + 100];
    char abspath[PATH_MAX];
    realpath(fname, abspath);
    sprintf(linebuff, msgfmt, abspath);
    strcat(linebuff, ": ");
    strcat(linebuff, strerror(errno));
//	perror(linebuff);
    return linebuff;
}


// are all bytes in file behind "offset" set to "val" ?
bool is_fileset(std::string *fpath, uint8_t val, uint32_t offset)
{
    bool result;
    FILE *f;
    uint8_t b;
    f = fopen(fpath->c_str(), "r");
    fseek(f, offset, SEEK_SET);
    result = true;
    // inefficient byte loop
    while (result && fread(&b, sizeof(b), 1, f) == 1)
        if (b != val)
            result = false;

    fclose(f);
    return result;
}

// Abstract: split a Unix file path into its parts
// format: /dir/subdir/.../filename, with filename = basename.ext
// for handled cases see self test
// Parameters:
// path: Path to split.
// Result:
// directories: chain of directories to file. if path ends with "/", everything is directories
//				Also single "/"
// filename: filename after last / , shortcut for basename.ext
// basename: filename before "." dot. Also ".." or "."
// extension: extension of file (without the "." dot)
//
// input "path" is copied, so calls like
//		split-path(path, &path, &filename ,...) are allowed
// TODO: should it really strip dot from "basename." ?
void split_path(std::string path, std::string *directories, std::string *filename, std::string *basename, std::string *extension)
{
    // path is a true copy, so its buffer can be split in pieces
    const char *path_buff = path.c_str();
    int cur_char_idx = 0;
    int last_match_idx = -1;

    const char* cur_char = path_buff;
    while(*cur_char != '\0') {
        // search for the last_match_idx slash
        while(*cur_char != '/' && *cur_char != '\0') {
            cur_char++;
            cur_char_idx++;
        }
        if(*cur_char == '/') {
            last_match_idx = cur_char_idx;
            cur_char++;
            cur_char_idx++;
        }
    }

    // directories is the first part of the path until the last_match_idx slash appears
    if (directories && last_match_idx < 0) { // no slash, no dir
        *directories = "" ;
    } else {
        // remove all trailing /: "dir/////file" => dir + file
        int start_trailing_slashes_idx = last_match_idx ;
        while (start_trailing_slashes_idx > 0 && path_buff[start_trailing_slashes_idx-1] == '/')
            start_trailing_slashes_idx-- ;
        // strip off dir, without trailing /
        if (directories) {
            if (start_trailing_slashes_idx == 0) // but keep single "/", like in /file.ext
                *directories = std::string(path_buff, start_trailing_slashes_idx+1);
            else
                *directories = std::string(path_buff, start_trailing_slashes_idx);
        }
        path_buff += last_match_idx+1 ; // trunk leading path and trailing /
    }
    // path_buff now points to start of filename
    if (filename)
        *filename = std::string(path_buff) ;

    // basename is the part behind the last_match_idx slash and before the "."
    // special cases:  "." and ".."
    // remember of hidden ".filename.ext"
    last_match_idx = -1 ;
    cur_char_idx = 0 ;
    cur_char = path_buff ;
    while(*cur_char != '\0') {
        // search for the last_match_idx "."
        while(*cur_char != '.' && *cur_char != '\0') {
            cur_char++;
            cur_char_idx++;
        }
        if(*cur_char == '.') {
            last_match_idx = cur_char_idx;
            cur_char++;
            cur_char_idx++;
        }
    }
    // find first leading ., for cases like ".", "..", "...crazy"
    int start_leading_dots_idx = last_match_idx ;
    while (start_leading_dots_idx > 0 && path_buff[start_leading_dots_idx-1] == '.')
        start_leading_dots_idx-- ;

    if (last_match_idx < 0 || start_leading_dots_idx == 0) { // no "." =>  no extension
        // handles also "leading dot", like in ".file", or "." or ".."
        if (basename)
            *basename = std::string(path_buff) ;
        if (extension)
            *extension = "" ;
    } else {
        // split off extension, without "."
        if (basename)
            *basename = std::string(path_buff, last_match_idx);
        path_buff += last_match_idx+1 ; // trunk leading path
        if (extension)
            *extension = std::string(path_buff) ;
    }
}

void split_path_test_single(std::string path)
{
    std::string directory, basename, extension ;
    //split_path(path, &directory, nullptr, &basename, &extension);
    // multiple calls, to test nullptr
    split_path(path, &directory, nullptr, nullptr, nullptr);
    split_path(path, nullptr, nullptr, &basename, nullptr);
    split_path(path, nullptr, nullptr, nullptr, &extension);
    printf("split=path(\"%s\") => dir=\"%s\", basename=\"%s\", ext=\"%s\"\n",
           path.c_str(), directory.c_str(), basename.c_str(), extension.c_str());
}

void split_path_test()
{
    split_path_test_single("filename");
    split_path_test_single("filename.ext");
    split_path_test_single("filename.ext1.ext2"); // ext=ext2
    split_path_test_single(".filename"); // no ext
    split_path_test_single("filename."); // dot removed?
    split_path_test_single(".filename."); // for curiosity
    split_path_test_single(".filename.ext");
    split_path_test_single("."); // filename
    split_path_test_single(".."); // filename
    split_path_test_single("dir/filename.ext");
    split_path_test_single("dir////filename.ext");
    split_path_test_single("dir/dir1");
    split_path_test_single("dir/dir1/"); // no filename
    split_path_test_single("dir/dir1/filename.ext");
    split_path_test_single("dir/.");
    split_path_test_single("dir/..");
    split_path_test_single("./"); // no filename
    split_path_test_single("../"); // no filename
    split_path_test_single("./dir/filename");
    split_path_test_single("../dir/filename");
    split_path_test_single("/"); // dir = /, no filename
    split_path_test_single("///"); // dir = /
    split_path_test_single("/filename"); // / + filename
}


// NOT YET CORRECTED
// TODO: use std:.string, add / and .
// Abstract: Make a path out of its parts
// Parameters: Path: Object to be made
// Directory: Directory part of path
// Filename: File part of path
// Extension: Extension part of path (includes the leading point)
// Returns: Path is changed
// Comment: Note that the concept of an extension is not available in Linux,
// nevertheless it is considered

void compose_path(char* path, const char* directory,
                  const char* basename,const char* extension)
{
    while(*directory != '\0' && directory != NULL) {
        *path = *directory;
        path ++;
        directory ++;
    }
    while(*basename != '\0' && basename != NULL) {
        *path = *basename;
        path ++;
        basename ++;
    }
    while(*extension != '\0' && extension != NULL) {
        *path = *extension;
        path ++;
        extension ++;
    }
    *path = '\0';
    return;
}


// make a relative path absolute by prefixing with current working dir
std::string absolute_path(std::string *path)
{
    if (path->size() > 0 && path->at(0) == '/')
        return *path ; // is already absolute
    char pathbuf[PATH_MAX] ;
    getcwd(pathbuf, sizeof(pathbuf)) ;
    std::string result = std::string(pathbuf) + std::string("/") + *path;
    return result ;
}

// write binary data into file
int file_write(char *fpath, uint8_t *data, unsigned size)
{
    int fd;
    // O_TRUNC: set to length 0
    fd = open(fpath, O_CREAT | O_TRUNC | O_RDWR, 0666);
    // or f = fopen(fpath, "w") ;
    if (fd < 0) {
        fprintf(stderr, "File write: can not open \"%s\"", fpath);
        return -1 ;
    }
    write(fd, data, size);
    close(fd);
    return 0;
}

bool file_exists(std::string *filename)
{
    struct stat buf;
    if (stat(filename->c_str(), &buf) != -1) {
        return true;
    }
    return false;
}


// true, if path/filename exists
bool file_exists(std::string *path, std::string *filename)
{
    char buffer[4096];
    struct stat st;
    if (path && !path->empty())
        sprintf(buffer, "%s/%s", path->c_str(), filename->c_str());
    else
        strcpy(buffer, filename->c_str());
    return !stat(buffer, &st);
}


// encode char into a 0..39 value
// " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789"
// invalid = %
// see https://en.wikipedia.org/wiki/DEC_Radix-50#16-bit_systems
static int rad50_chr2val(int c)
{
    if (c == ' ')
        return 000;
    else if (c >= 'A' && c <= 'Z')
        /// => 000001..011010
        return 1 + c - 'A';
    else if (c == '$')
        return 033;
    else if (c == '.')
        return 034;
    else if (c == '%')
        return 035;
    else if (c >= '0' && c <= '9')
        return 036 + c - '0';
    else
        return 035; // RT-11 for "invalid"
}

static int rad50_val2chr(int val)
{
    if (val == 000)
        return ' ';
    else if (val >= 001 && val <= 032)
        return 'A' + val - 001;
    else if (val == 033)
        return '$';
    else if (val == 034)
        return '.';
    else if (val == 035)
        return '%';
    else if (val >= 036 && val <= 047)
        return '0' + val - 036;
    else
        return '%'; // RT-11 for "invalid"
}

// convert 3 chars in RAD-50 encoding to a string
// letters are digits in a base 40 (octal "50") number system
// highest digit = left most letter
std::string rad50_decode(uint16_t w)
{
    char result[4];
    result[2] = rad50_val2chr(w % 050);
    w /= 050;
    result[1] = rad50_val2chr(w % 050);
    w /= 050;
    result[0] = rad50_val2chr(w);
    result[3] = 0;
    return std::string(result);
}

// convert first 3 chars. if less than 3 cars: space appended
uint16_t rad50_encode(std::string s)
{
    uint16_t result = 0;
    int len;

    if (s.empty())
        return 0; // 3 spaces

    len = s.length() ;
    if (len > 0)
        result += rad50_chr2val(toupper(s.at(0)));
    result *= 050;
    if (len > 1)
        result += rad50_chr2val(toupper(s.at(1)));
    result *= 050;
    if (len > 2)
        result += rad50_chr2val(toupper(s.at(2)));
    return result;
}



#define HEXDUMP_BYTESPERLINE	16
// mount hex and ascii and print
static void hexdump_put(std::ostream &stream, unsigned start, char *line_hexb, char* line_hexw,
                        char *line_ascii)
{
    char buffer[HEXDUMP_BYTESPERLINE * 3 +80] ;
    // expand half filled lines
    while (strlen(line_hexb) < HEXDUMP_BYTESPERLINE * 3)
        strcat(line_hexb, " ");
    while (strlen(line_hexw) < (HEXDUMP_BYTESPERLINE / 2) * 5)
        strcat(line_hexw, " ");
    while (strlen(line_ascii) < HEXDUMP_BYTESPERLINE)
        strcat(line_ascii, " ");
    sprintf(buffer, "%3x: %s   %s  %s\n", start, line_hexb, line_hexw, line_ascii);
    stream << buffer ;
    line_hexb[0] = 0; // clear output
    line_hexw[0] = 0;
    line_ascii[0] = 0;
}

// hex dump with info
void hexdump(std::ostream &stream, uint8_t *data, int size, const char *fmt, ...)
{
    va_list args;
    int i, startaddr;
    char line_hexb[80]; // buffer for hex bytes
    char line_hexw[80]; // buffer for hex words
    char line_ascii[40]; // buffer for ASCII chars

    if (fmt && strlen(fmt)) {
        char buffer[256] ;
        va_start(args, fmt);
        vsprintf(buffer, fmt, args);
        stream << buffer << "\n";
        va_end(args);
    }
    line_hexb[0] = 0; // clear output
    line_hexw[0] = 0;
    line_ascii[0] = 0;
    for (startaddr = i = 0; i < size; i++) {
        char buff[80];
        char c;
        if ((i % HEXDUMP_BYTESPERLINE) == 0 && i > 0) { // dump cur line
            hexdump_put(stream, startaddr, line_hexb, line_hexw, line_ascii);
            startaddr = i; // label for next line
        } else if ((i % 8) == 0 && i > 0) { // 8er col separator
            strcat(line_hexb, " ");
        }
        // append cur byte to hex and char display line
        sprintf(buff, "%02x", (unsigned) data[i]);
        if (strlen(line_hexb))
            strcat(line_hexb, " ");
        strcat(line_hexb, buff);
        if (i % 2) { // odd: mount word. LSB first
            unsigned w = data[i];
            w = (w << 8) | data[i - 1];
            if (strlen(line_hexw))
                strcat(line_hexw, " ");
            sprintf(buff, "%04x", w);
            strcat(line_hexw, buff);
        }

        c = data[i];
        if (c < 0x20 || c >= 0x7f)
            c = '.';
        sprintf(buff, "%c", c);
        strcat(line_ascii, buff);
    }    // dump rest of lines
    if (strlen(line_hexb))
        hexdump_put(stream, startaddr, line_hexb, line_hexw, line_ascii);
}



// add a number of microseconds to a time
struct timespec timespec_add_us(struct timespec ts, unsigned us)
{
    ts.tv_nsec += us * 1000;
    while (ts.tv_nsec >= BILLION) { // loops only once
        ts.tv_sec++;
        ts.tv_nsec -= BILLION;
    }
    return ts;
}

// add microseconds to current time
struct timespec timespec_future_us(unsigned offset_us)
{
    struct timeval tv;
    struct timespec ts;
    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec;
    ts.tv_nsec = 1000L * tv.tv_usec;
    return timespec_add_us(ts, offset_us);
}

// system time in milli seconds
uint64_t now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * (uint64_t) 1000 + tv.tv_usec / 1000;
}

/*
 bool caseInsCompare(const string& s1, const string& s2) {
 return((s1.size() == s2.size()) &&
 equal(s1.begin(), s1.end(), s2.begin(), caseInsCharCompareN));
 }
 */

// decodes C escape sequences \char, \nnn octal, \xnn hex
// result string is smaller or same as "encoded", must have at least "ncoded" size
// return: true of OK, else false
static int digitval(char c)
{
    c = toupper(c);
    if (c < '0')
        return 0; // illegal
    else if (c <= '9')
        return c - '0';
    else if (c < 'A')
        return 0; // illegal
    else if (c <= 'F')
        return c - 'A' + 10;
    else
        return 0; // illegal
}

bool str_decode_escapes(char *result, unsigned result_size, char *encoded)
{
    int c ;
    char *wp = result; // write pointer
    char *rp = encoded; // read pointer
    assert(result_size >= strlen(encoded));
    while (*rp) {
        if (*rp != '\\') {
            *wp++ = *rp++; // not escaped
            continue;
        }
        // decode escapes
        rp++; // eat backslash
        int n = strspn(rp, "01234567"); //
        if (n >= 1) { // \nnn given
            // use max 3 digits for octal literal
            c = digitval(*rp++) ;
            if (n >= 2)
                c = c * 8 + digitval(*rp++) ;
            if (n >= 3)
                c = c * 8 + digitval(*rp++) ;
            *wp++ = (char) c;
            continue;
        }
        switch (*rp) {
        // literals allowed behind backslash
        case '\'':
        case '"':
        case '?':
        case '\\':
            *wp++ = *rp++;
            continue;
        case 'a':
            *wp++ = 0x07; // audible bell
            rp++;
            continue;
        case 'b':
            *wp++ = 0x08; // backspace
            rp++;
            continue;
        case 'f':
            *wp++ = 0x0c; // form feed - new page
            rp++;
            continue;
        case 'n':
            *wp++ = 0x0a; // line feed - new line
            rp++;
            continue;
        case 'r':
            *wp++ = 0x0d; // carriage return
            rp++;
            continue;
        case 't':
            *wp++ = 0x09; // horizontal tab
            rp++;
            continue;
        case 'v':
            *wp++ = 0x0b; // vertical tab
            rp++;
            continue;
        case 'x': // hex: \xnn
            rp++; // eat "x"
            // in contrast to the standard, max 2 hex digits are evaualted, not arbitrary amount.
            // this makes it easy to write "L 200" as "L\x20200".
            // Else \xnnnn may eat following chars not meant as part of the hex sequence
            // convert and skip arbitrary count of hex characters
            n = strspn(rp, "0123456789aAbBcCdDeEfF");
            if (n < 1)
                return false ; // no hexdigit after "x"
            // use max 2 digits for hex literal
            c = digitval(toupper(*rp++)) ;
            if (n >= 2)
                c = c * 16 + digitval(toupper(*rp++)) ;
            // c = strtol(rp, &rp, 16) ; if unlimited hex chars
            *wp++ = (char) c;
            continue;
        default:
            return false; // unknown char behind backslash
        }
    }
    *wp = 0;
    return true;
}


// clip value to range
int	rangeToMinMax(int val, int min, int max) {
    assert(min <= max) ;
    int result = val ;
    if (val < min)
        result = min ;
    else if (val > max)
        result = max ;
    else
        result = val ;
    return result ;
}

