/* inputline.hpp: Advanced routines for user text input

 Copyright (c) 2012-2019, Joerg Hoppe
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

 05-Sep-2019	JH	C++, scripting
 23-Feb-2012  JH      created
 */

#ifndef INPUTLINE_HPP_
#define INPUTLINE_HPP_

#include <stdio.h>
#include <stdbool.h>

class inputline_c {
private:
	FILE *file = NULL;
	bool skip_lines ;
	bool internal_command(char *line);

public:
	inputline_c() {
		init();
	}
	void init(void);

	bool open_file(char *filename);
	bool is_file_open() ;

	char *readline(char *buffer, int buffer_size, const char *prompt);
};

#endif /* INPUTLINE_HPP_ */
