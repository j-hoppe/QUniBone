/* timeout.hpp: timer based on realtime and arbitrary timebase

 Copyright (c) 2020, Joerg Hoppe
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


 29.02.2020   JH      entered beta phase
 */

#ifndef _TIMEOUT_HPP_
#define _TIMEOUT_HPP_

#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#include <list>
#include <map>

#include "logsource.hpp"

/*** standard timeouts, always based on world time ***/
class timeout_c: public logsource_c {
private:
	struct timespec starttime;
	uint64_t duration_ns;
public:
	timeout_c();
	static uint64_t get_resolution_ns(void);
	static uint64_t abstime_ns(void) ;
	void start_ns(uint64_t duration_ns);
	void start_us(uint64_t duration_us);
	void start_ms(uint64_t duration_ms);
	uint64_t elapsed_ns(void);
	uint64_t elapsed_us(void);
	uint64_t elapsed_ms(void);
	bool reached(void);
	static void wait_ns(uint64_t duration_ns);
	static void wait_us(unsigned duration_us);
	static void wait_ms(unsigned duration_ms);

};

/*** flexible timeouts, based on world time or arbitrary "emulated steps" ***/

class flexi_timeout_controller_c;

class flexi_timeout_c: public logsource_c {
	friend class flexi_timeout_controller_c;
public:
	// Basic modes of timeout-system
	enum mode {
		world_time, emulated_time
	};
private:

	flexi_timeout_controller_c *timeout_controller; // link to owner
	uint64_t starttime_ns;
	uint64_t signaltime_ns; // trigger signal, when global reaches this

	sem_t semaphore; // for wait/signal

public:
	// all timeouts are constructed for single global "the_flexi_timeout_controller",
	// maybe changed later.
	flexi_timeout_c();
	~flexi_timeout_c();
	uint64_t get_resolution_ns(void);
	void start_ns(uint64_t duration_ns);
	void start_us(uint64_t duration_us);
	void start_ms(uint64_t duration_ms);
	uint64_t elapsed_ns(void);
	uint64_t elapsed_us(void);
	uint64_t elapsed_ms(void);
	bool reached(void);
	// directly work on the_flexi_timeout_controller
	static void wait_ns(uint64_t duration_ns);
	static void wait_us(unsigned duration_us);
	static void wait_ms(unsigned duration_ms);

};

// handels a list of flexi_timeout_c, when using "emulatedt"
class flexi_timeout_controller_c {
	friend class flexi_timeout_c;
private:
	// timestamp of first (and oldest) timeout in list, for emu_step_ns() quick test
	// 0 == none
	uint64_t emu_oldest_signal_time_ns;
	void update_oldest(void) {
		std::multimap<uint64_t, flexi_timeout_c*>::iterator it = emu_timeout_wait_list.begin();
		if (it == emu_timeout_wait_list.end())
			emu_oldest_signal_time_ns = 0;
		else
			emu_oldest_signal_time_ns = it->second->signaltime_ns;
	}

	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

public:
	flexi_timeout_controller_c();
	// Basic modes of timeout-system
			enum flexi_timeout_c::mode mode;

			/*** when using emulated timestamps ***/
			static uint64_t world_now_ns(void);
			uint64_t emu_now_ns; // current time in emulated nanoseconds

			// list of all instantiated timeouts
			std::list<flexi_timeout_c *> timeout_list;
			void insert_timeout(flexi_timeout_c *timeout);
			void erase_timeout(flexi_timeout_c *timeout);

			// list of timeouts and their wait signal times
			// ordered by endtime. so iterators get the soonest event first.
			std::multimap<uint64_t, flexi_timeout_c *> emu_timeout_wait_list;
	// list is never searched for certain "starttime" key, just ordered for iterators.
	// key "signaltime" is duplicate of flexi_timeout_c:.signaltime
	// multiple identical "signaltime" keys are possible, hence a "multi" map.

	void set_mode(enum flexi_timeout_c::mode new_mode);

	// insert a timeout to monitor for wait() signal. with starttime as sorting key
	void emu_insert_timeout_wait(flexi_timeout_c *timeout) {
		pthread_mutex_lock(&mutex);
		emu_timeout_wait_list.insert(std::pair<uint64_t,flexi_timeout_c*>(timeout->starttime_ns, timeout));
		std::multimap<uint64_t,flexi_timeout_c*>::iterator it = emu_timeout_wait_list.begin();
		emu_oldest_signal_time_ns = it->second->signaltime_ns;
		pthread_mutex_unlock(&mutex);
		// is checked on first step() call
	}

	// advance internal timebase
	void emu_step_ns(unsigned emu_delta_ns );
};

extern flexi_timeout_controller_c *the_flexi_timeout_controller; // singleton

#endif
