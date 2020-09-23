/* timeout.cpp: timer based on realtime and arbitrary emulated timebase

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



 For timing, most threads must wait (time) in their worker() or elsewhere.

 Delays is given in physical "real world" time.

 If device are used by simulated CPU, timebase must sometimes switch  to "emulated" time,
 derived from CPU cycle execution.
 So if the CPU thread is stopped by the process scheduler, all timeouts wait also.
 This is important, else (seen from emulated CPU) devices would seem to get
 "speed bursts". This may lead to for example for serial transmission OVERRUN conditions.

 The "flex_timeout" classprovide timeouts, whose mode can bes witche between
 - "world_time": real world time provided by Linux kernel
    Waiting is done with nano_sleep().
 - "emulated_time", as defined by artifically generated "emulated nanosconds"
    It is bumped up by the emualted CPU on code execution,
     or and some arbitrary intervals for CPU WAIT with "emu_step_ns()"
    On each step() it is checked whether a waiting thread is now signaled to continue.


 */

#include <assert.h>

#include "utils.hpp"
#include "timeout.hpp"

/*** standard timeouts, always based on world time ***/

timeout_c::timeout_c() {
	log_label = "TO";
}

uint64_t timeout_c::get_resolution_ns() {
	struct timespec res;
	clock_getres(CLOCK_MONOTONIC, &res);
	return BILLION * res.tv_sec + res.tv_nsec;
}

void timeout_c::start_ns(uint64_t duration_ns) {
	this->duration_ns = duration_ns;
	clock_gettime(CLOCK_MONOTONIC, &starttime);
}

void timeout_c::start_us(uint64_t duration_us) {
	start_ns(duration_us * 1000);
}

void timeout_c::start_ms(uint64_t duration_ms) {
	start_ns(duration_ms * MILLION);
}

uint64_t timeout_c::elapsed_ns(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	uint64_t result = (uint64_t) BILLION * (now.tv_sec - starttime.tv_sec)
			+ (uint64_t) now.tv_nsec - starttime.tv_nsec;
	return result;
}

uint64_t timeout_c::elapsed_us(void) {
	return elapsed_ns() / 1000;
}

uint64_t timeout_c::elapsed_ms(void) {
	return elapsed_ns() / MILLION;
}

bool timeout_c::reached() {
	return (elapsed_ns() > duration_ns);
}

/***
 Tests indicate that any nano_sleep() often causes delays of 60-80 �s
 ***/

// wait a number of nanoseconds, resolution in 0.1 millisecs
void timeout_c::wait_ns(uint64_t duration_ns) {
	struct timespec ts = { (long) (duration_ns / BILLION), (long) (duration_ns % BILLION) };
	int res = nanosleep(&ts, NULL);
	assert(res == 0 || (res == -1 && errno == EINTR)); // ^C abort may happen.
//		DEBUG("nanosleep() return a %d", res);
}

// wait a number of milliseconds
void timeout_c::wait_ms(unsigned duration_ms) {
	wait_ns(MILLION * duration_ms);
}

void timeout_c::wait_us(unsigned duration_us) {
	wait_ns(1000L * duration_us);
}

/*** flexible timeouts, based on world time or arbitrary "emulated steps" ***/

/* global singleton */
flexi_timeout_controller_c *the_flexi_timeout_controller;

flexi_timeout_c::flexi_timeout_c() {
	log_label = "FTO";
	timeout_controller = the_flexi_timeout_controller;
	timeout_controller->insert_timeout(this);
	if (timeout_controller->mode == emulated_time) {
		int res = sem_init(&semaphore, 0, 0);
		assert(res == 0);
	}
}
flexi_timeout_c::~flexi_timeout_c() {
	if (timeout_controller->mode == emulated_time) {
		int sval;
		sem_getvalue(&semaphore, &sval);
		assert(sval == 0); // nobody waiting
		int res = sem_destroy(&semaphore);
		assert(res == 0);
	}
	timeout_controller->erase_timeout(this);
}

uint64_t flexi_timeout_c::get_resolution_ns() {
	struct timespec res;
	clock_getres(CLOCK_MONOTONIC, &res);
	return BILLION * res.tv_sec + res.tv_nsec;
}

// current time in nanosecons

void flexi_timeout_c::start_ns(uint64_t duration_ns) {
	if (timeout_controller->mode == world_time) {
		starttime_ns = timeout_controller->world_now_ns();
		signaltime_ns = starttime_ns + +duration_ns;
	} else {
		// emulated time
		starttime_ns = timeout_controller->emu_now_ns;
		signaltime_ns = starttime_ns + duration_ns;
		timeout_controller->emu_insert_timeout_wait(this);
	}
}

void flexi_timeout_c::start_us(uint64_t duration_us) {
	start_ns(duration_us * 1000);
}

void flexi_timeout_c::start_ms(uint64_t duration_ms) {
	start_ns(duration_ms * MILLION);
}

uint64_t flexi_timeout_c::elapsed_ns(void) {
	if (timeout_controller->mode == world_time) {
		return timeout_controller->world_now_ns() - starttime_ns;
	} else {
		// emulated time
		return timeout_controller->emu_now_ns - starttime_ns;
	}
}

uint64_t flexi_timeout_c::elapsed_us(void) {
	return elapsed_ns() / 1000;
}

uint64_t flexi_timeout_c::elapsed_ms(void) {
	return elapsed_ns() / MILLION;
}

bool flexi_timeout_c::reached() {
	if (timeout_controller->mode == world_time) {
		return (timeout_controller->world_now_ns() >= signaltime_ns);
	} else {
		// emulated time
		return (timeout_controller->emu_now_ns >= signaltime_ns);
	}
}

/***
 Tests indicate that any nano_sleep() often causes delays of 60-80 �s
 ***/

// wait a number of nanoseconds, resolution in 0.1 millisecs
void flexi_timeout_c::wait_ns(uint64_t duration_ns) {
	if (the_flexi_timeout_controller->mode == world_time) {
		struct timespec ts = { (long) (duration_ns / BILLION), (long) (duration_ns % BILLION) };
		int res = nanosleep(&ts, NULL);
		assert(res == 0 || res == -1); // may terminate due to signal handler
		if (res == -1)
			assert(errno == EINTR);
	} else {
		// emulated time : wait for signal, generated by step()
		flexi_timeout_c timeout;
		timeout.starttime_ns = timeout.timeout_controller->emu_now_ns;
		timeout.signaltime_ns = timeout.starttime_ns + duration_ns;
		timeout.timeout_controller->emu_insert_timeout_wait(&timeout);
		int res = sem_wait(&timeout.semaphore); // wait until sem_post()
		assert(res == 0 || res == -1); // may terminate due to signal handler
		if (res == -1)
			assert(errno == EINTR);
	}
//		DEBUG("nanosleep() return a %d", res);
}

// wait a number of milliseconds
void flexi_timeout_c::wait_ms(unsigned duration_ms) {
	wait_ns(MILLION * duration_ms);
}

void flexi_timeout_c::wait_us(unsigned duration_us) {
	wait_ns(1000L * duration_us);
}

flexi_timeout_controller_c::flexi_timeout_controller_c() {
	mode = flexi_timeout_c::world_time; // downward copatibility
// "emulated_time" only used when emulated CPU
	emu_now_ns = 0;
	emu_oldest_signal_time_ns = 0;
}

void flexi_timeout_controller_c::insert_timeout(flexi_timeout_c *timeout) {
	pthread_mutex_lock(&mutex);
	timeout_list.push_back(timeout);
	pthread_mutex_unlock(&mutex);
}

void flexi_timeout_controller_c::erase_timeout(flexi_timeout_c *timeout) {
	pthread_mutex_lock(&mutex);
	// remove from list
	std::list<flexi_timeout_c*>::iterator p;
	p = find(timeout_list.begin(), timeout_list.end(), timeout);
	if (p != timeout_list.end())
		timeout_list.erase(p);
	pthread_mutex_unlock(&mutex);
}

uint64_t flexi_timeout_controller_c::world_now_ns() {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	uint64_t result = (uint64_t) BILLION * now.tv_sec + (uint64_t) now.tv_nsec;
	return result;
}

// mode can change while threads are waiting
void flexi_timeout_controller_c::set_mode(enum flexi_timeout_c::mode new_mode) {
	if (mode == new_mode)
		return;
	pthread_mutex_lock(&mutex);
	if (mode == flexi_timeout_c::world_time) {
		// Transition real_time -> emulated_time
		// waiting thread use "nanosleep()", will terminated automatically.
		// their next wait() is then by semaphore
		mode = flexi_timeout_c::emulated_time;
		// seamless continue with current time,
		// so all starttime/endtime can be re-used, reached() and elapsed() preserved
		emu_now_ns = world_now_ns();
		emu_oldest_signal_time_ns = 0;
		assert(emu_timeout_wait_list.size() == 0); // start empty
		// recalc all start and endtimes, so elapsed() and reached() are preserved)
	} else {
		// Transition emulated_time -> real_time
		uint64_t now_ns = world_now_ns();
		// convert emulated time stamps to real time
		for (std::list<flexi_timeout_c*>::iterator it = timeout_list.begin();
				it != timeout_list.end(); ++it) {
			(*it)->starttime_ns = ((*it)->starttime_ns + now_ns) - emu_now_ns;
			(*it)->signaltime_ns = ((*it)->signaltime_ns + now_ns) - emu_now_ns;
		}

		// Threads wait for semaphores.
		// as the app stopps calingstep() now, they'd freeze forever.
		// so signal all.
		mode = flexi_timeout_c::world_time; // before signaling waiters, they may wait() immediately again
		std::multimap<uint64_t, flexi_timeout_c*>::iterator it = emu_timeout_wait_list.begin();
		while (it != emu_timeout_wait_list.end()) {
			int res = sem_post(&(it->second->semaphore)); // signal to sem_wait()
			assert(res == 0);
			it = emu_timeout_wait_list.erase(it); // delete and next
		}
	}
	pthread_mutex_unlock(&mutex);
}

// advance emulated clock
// and signal elapsed timeouts.
void flexi_timeout_controller_c::emu_step_ns(unsigned emu_delta_ns) {
	if (mode != flexi_timeout_c::emulated_time)
		return;

	// step global ticks
	emu_now_ns += emu_delta_ns;

	// signals must be triggered? Quick test first.
	// step() is called very frequently, timeouts change quite seldom.
	if (emu_oldest_signal_time_ns && emu_now_ns >= emu_oldest_signal_time_ns) {
		pthread_mutex_lock(&mutex);
		// Create a map iterator and point to beginning of map
		std::multimap<uint64_t, flexi_timeout_c*>::iterator it = emu_timeout_wait_list.begin();
		while (it != emu_timeout_wait_list.end() && it->second->signaltime_ns <= emu_now_ns) {
			int res = sem_post(&(it->second->semaphore)); // signal to sem_wait()
			assert(res == 0);
			it = emu_timeout_wait_list.erase(it); // delete and next
		}
		// save first entry timestamp in "emu_oldest_signal_time_ns" for quick test
		if (it == emu_timeout_wait_list.end())
			emu_oldest_signal_time_ns = 0;
		else
			emu_oldest_signal_time_ns = it->second->signaltime_ns;
		pthread_mutex_unlock(&mutex);
	}
}

// test procedures
#include <thread>
#include "logger.hpp"

class flexi_timeout_test_c {
public:
	// execute on construction
	flexi_timeout_test_c() {
		// construct global helper objects
		logger = new logger_c();
		the_flexi_timeout_controller = new flexi_timeout_controller_c();

		run();
	}

private:
	uint64_t world_starttime_us; // test start time in real world

	char * text_us(uint64_t us) {
		static char buff[256];
		sprintf(buff, "%0.6f", us / 1000.0);
		return buff;
	}

	uint64_t world_now_us(void) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		uint64_t result = (uint64_t) MILLION * now.tv_sec + (uint64_t) now.tv_nsec / 1000;
		return result;
	}

	void world_wait_ns(uint64_t duration_ns) {
		struct timespec ts = { (long) (duration_ns / BILLION), (long) (duration_ns % BILLION) };
		nanosleep(&ts, NULL);
	}

	// abs time relative to test start
	uint64_t test_now_us() {
		return world_now_us() - world_starttime_us;
	}

// print string with test-timestamp.
	void print(const char *s) {
		printf("[%9.6f] %s\n", test_now_us() / 1000000.0, s);
	}

	/*


	 Testing:
	 a) start, reached() with change emu/worldtime
	 2 test threads, parallel waiting

	 test1:
	 init() ;
	 flexi_timeout_c to1, to2 ;
	 to1.start_ms(1000)
	 to2.start_ms(500)
	 to1.tst_reached =to2.tst_reached=false;
	 while(!reached[1] || !reached[1] {
	 if (to1.reached() && !reached[1]) {
	 printf("to1 reached @ %s", text_us(test_now_us())
	 to1.tst_reached = true ;
	 }


	 3 trhreads
	 #A                 #B              #C
	 wait(1000ms) |  	wait(1000ms) | 	wait(1000ms)			< all same time
	 wait(1000ms) |  	wait(2000ms) | 	wait(3000ms)			< separate them
	 wait(3000ms) |  	wait(2000ms) | 	wait(1000ms)			< join them, called in reverse order


	 Test run 1): wolrd time
	 Tets run 2) emulated time, double speed 8every second step(2000000)
	 Test run 3) start with "world" time, wait 2500ms, then switch to "emulated time
	 Test run 4) start with "emulated time" time, wait 2500ms, then switch to "world time"
	 }

	 */

	std::thread *tA, *tB, *tC;

	void start_simulation() {
		// thread bodies as lambda
		tA = new std::thread([this] {
			print("A.1 @ 0");
			flexi_timeout_c::wait_ms(1000);
			print("A.2 @ 1000");
			flexi_timeout_c::wait_ms(1000);
			print("A.3 @ 2000");
			flexi_timeout_c::wait_ms(3000);
			print("A.4 @ 5000");
		});
		tB = new std::thread([this] {
			print("B.1 @ 0");
			flexi_timeout_c::wait_ms(1000);
			print("B.2 @ 1000");
			flexi_timeout_c::wait_ms(2000);
			print("B.3 @ 3000");
			flexi_timeout_c::wait_ms(2000);
			print("B.4 @ 5000");
		});
		tC = new std::thread([this] {
			print("C.1 @ 0");
			flexi_timeout_c::wait_ms(1000);
			print("C.2 @ 1000");
			flexi_timeout_c::wait_ms(3000);
			print("C.3 @ 4000");
			flexi_timeout_c::wait_ms(1000);
			print("C.4 @ 5000");
		});
		// after 2500 ms, A.4, B.3, C.3 are reached
	}

	void waitfor_simulation() {
		tA->join();
		tB->join();
		tC->join();
	}

	uint64_t totaltick_ns; // total ticks issued to timeout_controller

	// Stimualte time_controller with step() until totaltick_ns reaches end_total_ticks_ns.
	// step duration is 0.. max.
	// speed_factor_percent: 0..100: emulated ticks faster then real time
	//	e.g.: 200 -> emulated time double as fast as world time
	//		50: -> emulated time half as fast as world time
	void emulated_random_steps(uint64_t end_total_ticks_ns, unsigned max_step_duration_ns,
			unsigned speed_factor_percent) {
		while (totaltick_ns < end_total_ticks_ns) {
			// steps in range 0..10ms
			//			uint64_t emu_ticks_ns = 1000 * (rand() % 50);
			uint64_t emu_ticks_ns = rand() % max_step_duration_ns;
			totaltick_ns += emu_ticks_ns;
			// advance emulated clock
			the_flexi_timeout_controller->emu_step_ns(emu_ticks_ns);
			// wait double as long => emulated clock "half speed""
			world_wait_ns(emu_ticks_ns * 100 / speed_factor_percent);// very imprecise in micro second range
		}
	}

	void test1() {
		printf("Test 1: wait() with real world time\n");
		world_starttime_us = world_now_us();
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::world_time);
		start_simulation();
		waitfor_simulation();
	}

	void test2() {
		printf("\nTest 2: wait() with emulated time, half speed\n");
		world_starttime_us = world_now_us();
		// clock system runs with emulated "ticks"
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::emulated_time);
		totaltick_ns = 0; // total emulated nanosecs issued
		start_simulation();

		// "6000": threads should terminate after 5 secons
		// ticks: random 0..50 ms
		emulated_random_steps(6000 * MILLION, 50 * MILLION, 50);
		waitfor_simulation();
		// if threads not terminating clock system is damaged
	}

	void test3() {
		printf("\nTest 3: wait() with emulated time, 10x speed, high frequency steps\n");
		world_starttime_us = world_now_us();
		totaltick_ns = 0;
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::emulated_time);
		start_simulation();
		// ticks: random 0..10 ms, nano_sleep does time errors
		emulated_random_steps(6000 * MILLION, 10 * MILLION, 1000);
		waitfor_simulation();
		// if threads not terminating clock system is damaged
	}

	void test4() {
		printf(
				"\nTest 4: changing from world time to emulated time @ 2500ms, threads waits not completed\n");
		world_starttime_us = world_now_us();
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::world_time);
		start_simulation();
		printf("Waiting 2500ms seconds\n");
		world_wait_ns(2500 * MILLION); // threads run into middle wait()
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::emulated_time);
		printf("Switching over to emulated time, wait 10 seconds\n");
		printf("A.4, B.3, C.3 complete now current world-time wait\n");
		printf("Threads hang then in emulated-time wait, as no steps are generated\n");

		world_wait_ns(10000 * MILLION); // threads run into middle wait()
		printf(
				"Switching back to world time. Waiting threads continue with new time source.\n");
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::world_time);
		waitfor_simulation();
	}

	void test5() {
		printf(
				"\nTest 5: changing from world time to emulated time in middle of 1st wait (@ 2500ms)\n");
		world_starttime_us = world_now_us();
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::world_time);
		start_simulation();
		printf("Waiting 2500ms seconds\n");
		world_wait_ns(2500 * MILLION); // threads run into middle wait()
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::emulated_time);
		printf("Switching over to emulated time, emulated nanosseconds in wolrd.speed.\n");
		totaltick_ns = 2500 * MILLION; // only feed steps in for another 2.5 seconds
		emulated_random_steps(6000 * MILLION, 50 * MILLION, 100);
		waitfor_simulation();
	}

	void test6() {
		printf(
				"\nTest 6: changing from emulated time to world time in middle of 1st wait (@ 2500ms)\n");
		world_starttime_us = world_now_us();
		totaltick_ns = 0;
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::emulated_time);
		start_simulation();
		printf("Injecting emulated nanoseconds for 2500ms.\n");
		emulated_random_steps(2500 * MILLION, 50 * MILLION, 100);

		world_wait_ns(2500 * MILLION); // threads run into middle wait()
		printf(
				"Threads now waiting for more emulated nanoseconds, which never are generated.\n");
		printf("Switching over to world time, in the middle of thread wait()s.\n");
		printf(
				"Waiting for emulated nanoseconds is immediately aborted, so different amounts of wait time are lost.\n");
		the_flexi_timeout_controller->set_mode(flexi_timeout_c::world_time);
		waitfor_simulation();
	}

	// all tests
	void run() {
		test1();
		test2();
		test3();
		test4();
		test5();
		test6();
		printf("\nTimeout tests completed\n\n");

	}
};

// construct global, this executes test
//timeout_test_c timeout_test;

