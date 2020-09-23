/* logsource.cpp: Interface of global logger to object instances.

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

   Every object has an own logger-label and an own log_level variable

   Usage:
   - An object which wants to log must inherit from logsource_c.
   - In its constructor, it must set log_label_ptr and log_level_ptr pointers
     to its own variables.
   - Messages are generated with FATAL(), ERROR(), ... DEBUG() message macros.
   - each object.log_level is initialized by logger.default_level
*/
#ifndef _LOGSOURCE_HPP_
#define _LOGSOURCE_HPP_


#include <string>

class logsource_c {
private:
	// default vars for logsources without own variables
	unsigned	log_level ; // filters which messages to display in the global logger.

	// register/unregister at global logger
	// called in constructor/destructor
	void connect() ;
	void disconnect() ;
public:
	logsource_c() ;
	~logsource_c() ;

	// surrounding object can use own variables
	std::string log_label ; // short string to show source in print outs
//	std::string	*log_label_ptr ; // ref to "label variable" of surrounding object instance
	unsigned log_id ; // unique numeric id, assigned by global logger
	// the logger keeps a list of known logsources[]

	// access loglevel indirect, because its a device parameter
	unsigned *log_level_ptr ; // ref to "level" variable of surrounding object instance
	// level determines which log() message are to be output
	//  fatal, error, warning, info, debug
} ;

// macros to be used in surrounding class code
// (must be macros, because of __FILE__/__LINE__ )
#define FATAL(...)	\
	logger->log(this, LL_FATAL, __FILE__, __LINE__, __VA_ARGS__)
#define ERROR(...)	\
	logger->log(this, LL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define WARNING(...)	\
	logger->log(this, LL_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define INFO(...)	\
	logger->log(this, LL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG(...)	\
	logger->log(this, LL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
// disables a DEBUG	
#define _DEBUG(...)	


#endif // _LOGSOURCE_HPP_
