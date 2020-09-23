/* parameter.cpp: collection of typed name/value pairs, used by devices and other objects

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


 12-nov-2018  JH      entered beta phase
 */

#include <cctype>
#include "bitcalc.h"

#include "utils.hpp"
#include "parameter.hpp"
#include "device.hpp"
#define _PARAMETER_CPP_

parameter_c::parameter_c(parameterized_c *parameterized, string name, string shortname,
bool readonly, string unit, string format, string info) {
	this->parameterized = parameterized;
	this->name = name;
	this->shortname = shortname;
	this->readonly = readonly;
	this->unit = unit;
	this->format = format;
	this->info = info;

	// add to parameter list of device
	if (parameterized != NULL)
		parameterized->param_add(this);
}

parameter_c::~parameter_c() {
}

// to be implemented in subclass
void parameter_c::parse(string text) {
	throw bad_parameter_parse("parameter_c::parse(" + text + ") to be implemented in subclass");
}

// convert to text
string *parameter_c::render(void) {
	printbuffer = "to be implemented in subclass";
	return &printbuffer;

}

parameter_string_c::parameter_string_c(parameterized_c *parameterized, string name,
		string shortname, bool readonly, string info) :
		parameter_c(parameterized, name, shortname, readonly, "", "", info) {
	value = "";
}

parameter_string_c::~parameter_string_c() {
}

void parameter_string_c::set(string new_value) {

	if (value == new_value)
		return; // call "on_change" only on change
	this->new_value = new_value;
	// reject parsed value, if device parameter check complains
	if (parameterized == NULL || parameterized->on_param_changed(this))
		value = this->new_value; // device may have changed "new_value"
}

// string parsing is just copying
void parameter_string_c::parse(string text) {
	if (readonly)
		throw bad_parameter_readonly("Parameter \"" + name + "\" is read-only");
	set(text);
}

string *parameter_string_c::render() {
	printbuffer = value;
	return &printbuffer;
}

parameter_bool_c::parameter_bool_c(parameterized_c *parameterized, string name,
		string shortname,
		bool readonly, string info) :
		parameter_c(parameterized, name, shortname, readonly, "", "", info) {
	value = false;
}

void parameter_bool_c::set(bool new_value) {
	if (value == new_value)
		return; // call "on_change" only on change

	// reject parsed value, if device parameter check complains
	this->new_value = new_value;
	if (parameterized == NULL || parameterized->on_param_changed(this))
		value = new_value;
}

// bool accepts 0/1, y*/n*, t*/f*
void parameter_bool_c::parse(string text) {
	char c;
	if (readonly)
		throw bad_parameter_readonly("Parameter \"" + name + "\" is read-only");
	TRIM_STRING(text);
//	text.erase(std::remove_if(text.begin(), text.end(), ::isspace), text.end())	;
	if (text.size() <= 0)
		throw bad_parameter_parse("empty string");
	c = toupper((text.at(0)));
	if (c == '1' || c == 'Y' || c == 'T')
		new_value = true;
	else if (c == '0' || c == 'N' || c == 'F')
		new_value = false;
	else
		throw bad_parameter_parse("Illegal boolean expression \"" + text + "\"");
	set(new_value);
}

string *parameter_bool_c::render() {
	if (value)
		printbuffer = "1";
	else
		printbuffer = "0";
	return &printbuffer;
}

parameter_unsigned_c::parameter_unsigned_c(parameterized_c *parameterized, string name,
		string shortname, bool readonly, string unit, string format, string info,
		unsigned bitwidth, unsigned base) :
		parameter_c(parameterized, name, shortname, readonly, unit, format, info) {
	this->bitwidth = bitwidth;
	this->base = base;
	value = 0;
}

void parameter_unsigned_c::set(unsigned new_value) {
	if (value == new_value)
		return; // call "on_change" only on change

	this->new_value = new_value;
	// reject parsed value, if device parameter check complains
	if (parameterized == NULL || parameterized->on_param_changed(this))
		value = new_value;
}

void parameter_unsigned_c::parse(string text) {
	char *endptr;
	if (readonly)
		throw bad_parameter_readonly("Parameter \"" + name + "\" is read-only");
	TRIM_STRING(text);
	new_value = strtol(text.c_str(), &endptr, base);
	if (*endptr)
		throw bad_parameter_parse("Format error in \"" + text + "\" at \"" + *endptr + "\"");
	if (new_value & ~BitmaskFromLen32[bitwidth]) //
		throw bad_parameter_parse(
				"Number " + to_string(new_value) + " exceeds bitwidth " + to_string(bitwidth));
	set(new_value);
}

string *parameter_unsigned_c::render() {
	char buffer[1024];
	sprintf(buffer, format.c_str(), value);
	printbuffer = buffer;
	return &printbuffer;
}

parameter_unsigned64_c::parameter_unsigned64_c(parameterized_c *parameterized, string name,
		string shortname, bool readonly, string unit, string format, string info,
		unsigned bitwidth, unsigned base) :
		parameter_c(parameterized, name, shortname, readonly, unit, format, info) {
	this->bitwidth = bitwidth;
	this->base = base;
	value = 0;
}

void parameter_unsigned64_c::set(uint64_t new_value) {
	if (value == new_value)
		return; // call "on_change" only on change

	this->new_value = new_value;
	// reject parsed value, if device parameter check complains
	if (parameterized == NULL || parameterized->on_param_changed(this))
		value = new_value;
}

void parameter_unsigned64_c::parse(string text) {
	char *endptr;
	if (readonly)
		throw bad_parameter_readonly("Parameter \"" + name + "\" is read-only");
	TRIM_STRING(text);
	new_value = strtoll(text.c_str(), &endptr, base);
	if (*endptr)
		throw bad_parameter_parse("Format error in \"" + text + "\" at \"" + *endptr + "\"");
	if (new_value & ~BitmaskFromLen64[bitwidth]) //
		throw bad_parameter_parse(
				"Number " + to_string(new_value) + " exceeds bitwidth " + to_string(bitwidth));
	set(new_value);
}

string *parameter_unsigned64_c::render() {
	char buffer[1024];
	sprintf(buffer, format.c_str(), value);
	printbuffer = buffer;
	return &printbuffer;
}

parameter_double_c::parameter_double_c(parameterized_c *parameterized, string name,
		string shortname,
		bool readonly, string unit, string format, string info) :
		parameter_c(parameterized, name, shortname, readonly, unit, format, info) {
	value = 0.0;
}

string *parameter_double_c::render(void) {
	char buffer[1024];
	sprintf(buffer, format.c_str(), value);
	printbuffer = buffer;
	return &printbuffer;
}

void parameter_double_c::parse(string text) {
	if (readonly)
		throw bad_parameter_readonly("Parameter \"" + name + "\" is read-only");
	TRIM_STRING(text);
	new_value = atof(text.c_str());
	set(new_value);
}

void parameter_double_c::set(double new_value) {
	if (value == new_value)
		return; // call "on_change" only on change

	this->new_value = new_value;
	// reject parsed value, if device parameter check complains
	if (parameterized == NULL || parameterized->on_param_changed(this))
		value = new_value;
}

// add reference to parameter. It will be automatically deleted
parameter_c *parameterized_c::param_add(parameter_c *param) {
	parameter.push_back(param);
	return param;
}

// search a parameter by name or shortname
parameter_c *parameterized_c::param_by_name(string name) {
	vector<parameter_c *>::iterator it;

	for (it = parameter.begin(); it != parameter.end(); it++) {
		if (strcasecmp((*it)->name.c_str(), name.c_str()) == 0)
			return (*it);
	}
	for (it = parameter.begin(); it != parameter.end(); it++) {
		if (strcasecmp((*it)->shortname.c_str(), name.c_str()) == 0)
			return (*it);
	}

	return NULL;
}

