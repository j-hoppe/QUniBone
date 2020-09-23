/* parameter.hpp: collection of typed name/value pairs, used by devices and other objects

 Copyright (c) 2018-2019, Joerg Hoppe
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

 16-mar-2019 JH		unlinked from devices
 12-nov-2018  JH      entered beta phase
 */

#ifndef _PARAMETER_HPP_
#define _PARAMETER_HPP_

#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <exception>
using namespace std;

class bad_parameter: public exception {
private:
	string message;
public:
	bad_parameter(string message) {
		(this->message = message);
	}
	const char* what() const noexcept {
		return message.c_str();
	}
};

class bad_parameter_parse: public bad_parameter {
public:
	bad_parameter_parse(string message) :
			bad_parameter(message) {
	}
};

class bad_parameter_check: public bad_parameter {
public:
	bad_parameter_check(string message) :
			bad_parameter(message) {
	}
};

class bad_parameter_readonly: public bad_parameter {
public:
	bad_parameter_readonly(string message) :
			bad_parameter(message) {
	}
};

class parameterized_c;

class parameter_c {
private:
public:
	parameterized_c *parameterized; // link to parent object
	string name;
	string shortname;
	bool readonly;
	string info; // help text
	string unit; // "MB",
	string format; // printf, scanf

	// parameter_c();
	parameter_c(parameterized_c *parameterized, string name, string shortname, bool readonly,
			string unit, string format, string info);
	virtual ~parameter_c(); // class with virtual functions should have virtual destructors

	// convert text to value. result: ok?
	virtual void parse(string text);
	// convert to text
	string printbuffer;
	virtual string *render(void);
};

class parameter_string_c: public parameter_c {
public:
	// dynamic state
	string value;
	string new_value;

	parameter_string_c(parameterized_c *parameterized, string name, string shortname,
			bool readonly, string info);
	~parameter_string_c();
	string *render(void) override;
	void parse(string text) override;
	void set(string new_value);
};

class parameter_bool_c: public parameter_c {
public:
	// dynamic state
	bool value;
	bool new_value;

	parameter_bool_c(parameterized_c *parameterized, string name, string shortname,
			bool readonly, string info);
	parameter_bool_c();
	string *render(void) override;
	void parse(string text) override;
	void set(bool new_value);
};

class parameter_unsigned_c: public parameter_c {

public:
	// static config
	unsigned bitwidth;
	unsigned base; // octal, decimal, hex

	// dynamic state
	unsigned value;
	unsigned new_value;

	parameter_unsigned_c(parameterized_c *parameterized, string name, string shortname,
			bool readonly, string unit, string format, string info, unsigned bitwidth,
			unsigned base);
	string *render(void) override;
	void parse(string text) override;
	void set(unsigned new_value);
};

class parameter_unsigned64_c: public parameter_c {

public:
	// static config
	unsigned bitwidth;
	unsigned base; // octal, decimal, hex

	// dynamic state
	uint64_t value;
	uint64_t new_value;

	parameter_unsigned64_c(parameterized_c *parameterized, string name, string shortname,
			bool readonly, string unit, string format, string info, unsigned bitwidth,
			unsigned base);
	string *render(void) override;
	void parse(string text) override;
	void set(uint64_t new_value);
};

class parameter_double_c: public parameter_c {

public:

	// dynamic state
	double value;
	double new_value;

	parameter_double_c(parameterized_c *parameterized, string name, string shortname,
			bool readonly, string unit, string format, string info);
	string *render(void) override;
	void parse(string text) override;
	void set(double new_value);
};

// objects with parameters are "parameterized" and inherit this.
class parameterized_c {
public:
	vector<parameter_c *> parameter;

	// register parameter
	parameter_c *param_add(parameter_c *param);

	// search a parameter
	parameter_c *param_by_name(string name);

	// sort?

	// called after param value changed. 
	// result: false = "new_value" not excepted, error printed.
	virtual bool on_param_changed(parameter_c *param) = 0;
};

#endif
