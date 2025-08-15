/* inputline.cpp: Advanced routines for user text input

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


 Not called "readline", because this is the big Unix function for the same task.
 It is used here.

 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "kbhit.h"
#include "inputline.hpp"

/*
 * get input from user
 * - shall be run in seperate thread, with abort/query status fucntionality
 * - shall use readline() under *ix, DOS support under Windows
 * - can use fgets in early development stages
 * - can read input from a file
 * format of cmd file:
 *	"# text" is comment and ignored
 *	empty lines are ignored
 *	leading, tralingspace removed
 * Example:
 #  inputfile for demo to select a rl1 device in the "device test" menu.
 # Read in with command line option  "demo --cmdfile ..."
 td			# device test menu
 sd rl1			# select the "rl1" disk drive
 p powerswitch 1		# power on, now in "load" state
 p image test.rl02 	# load image file
 # set to "rl02"
 # load image file with test pattern
 p es 10 	# 10x speed: startup not 45 seconds!
 p load_switch 1	# load cartridge, spin up
 .wait 5000	# internal command
 .print Drive now spun up?
 p			# show all params
 .end	# ignore remaing content

 */

// reset input source and internal states
void inputline_c::init() 
{
	// close file, if open
	if (file)
		fclose(file);
	file = nullptr;
	skip_lines = false ;
}

bool inputline_c::open_file(char *filename) 
{
	file = fopen(filename, "r");
	return (file != nullptr);
}


bool inputline_c::is_file_open() 
{
	return (file != nullptr) ;
}

// replace $1..9 with variable[1..9]
// void inputline_c::expand_variables(char *line) {
//string s(line) ;
//strfind($1)
	//w rite back
//	strcpy(line, s.c_str()) ;

// }


// check, if line contains an internal "inputline" command
// (line stripped from spaces)
// .wait <millisecs>
// .print <text>
// result: true = internal command processed
//	false = unkwown
bool inputline_c::internal_command(char *line) 
{
	// endif termiantes skipped line range
	if (!strncasecmp(line, ".endif", 6)) {
		skip_lines = false ;
		return true ;	
	}
	if (skip_lines) 
		return true ; // not part of output

	// .ifeq <string1> <string2>
	// 	...
	// .endif
	if (!strncasecmp(line, ".ifeq", 5)) {
		char	str1[256], str2[256] ;
		sscanf(line + 5, "%s %s", str1, str2);
		skip_lines = strcasecmp(str1,str2) ;
		//	skip line range until .endif if strings are different
	} else if (!strncasecmp(line, ".wait", 5)) {
		struct timespec ts;
		unsigned millis;
		sscanf(line + 5, "%d", &millis);
		ts.tv_sec = millis / 1000;
		ts.tv_nsec = 1000000L * (millis % 1000);
		printf("<<<\n");
		printf("<<< Input: waiting for %d milli seconds >>>\n", millis);
		nanosleep(&ts, nullptr);
		printf("<<<\n");
		return true;
	} else if (!strncasecmp(line, ".print", 6)) {
		printf("<<< %s\n", line + 7);
		return true;
	} else if (!strncasecmp(line, ".input", 6)) {
		char buffer[100];
		printf("<<< Press ENTER to continue.\n");
		// flush stuff on stdin. (Eclipse remote debugging)
		while (os_kbhit())
			;

		fgets(buffer, sizeof(buffer), stdin);
		return true;
	} else if (!strncasecmp(line, ".end", 3)) {
		// close input file
		fclose(file);
		file = nullptr;
		return true;
	}
	return false;
}

char *inputline_c::readline(char *buffer, int buffer_size, const char *prompt) 
{
	char *s;
	if (file != nullptr) {
		// read from file
		bool ready = false;
		while (!ready && file != nullptr) {
			/*** read line from text file ***/
			if (fgets(buffer, buffer_size, file) == nullptr) {
				fclose(file);
				file = nullptr; // file empty, or unreadable
				ready = true;
			} else {
				// remove terminating "\n"
				for (s = buffer; *s; s++)
					if (*s == '\n')
						*s = '\0';
				// remove comments after #
				for (s = buffer; *s; s++)
					if (*s == '#')
						*s = 0;
				// trim leading space
				for (s = buffer; *s && isspace(*s); s++)
					;
				memmove(buffer, s, strlen(buffer));
				s = buffer + strlen(buffer) - 1;
				while (s > buffer && isspace(*s))
					s--;
				// points to last "non-white" char now
				s++;
				*s = 0; // trunc the spaces

				// if empty line: repeat
				if (*buffer == 0)
					continue;
				if (!internal_command(buffer)) {
					printf("%s\n", buffer);
					ready = true;
				}
			}
		}
	}
	if (file == nullptr) {
		/*** read interactive ***/
		if (prompt && *prompt)
			printf("%s", prompt);
		fgets(buffer, buffer_size, stdin);
		// remove terminating "\n"
		for (s = buffer; *s; s++)
			if (*s == '\n')
				*s = '\0';
	}
	return buffer;
}

