/* device.cpp - abstract base class for devices

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

 Abstract device, with or without QBUS/UNIBUS registers.
 maybe mass storage controller, storage drive or other QBUS/UNIBUS device
 sets device register values depending on internal status,
 reacts on register read/write over QBUS/UNIBUS by evaluation of PRU events.

 A device
 - has a worker()
 - has a logger
 - has parameters
 */
#define _DEVICE_CPP_

#include <string.h>
#include <assert.h>
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>

#include "utils.hpp"
#include "logger.hpp"
#include "timeout.hpp"
#include "device.hpp"

// declare device list of class separate
std::list<device_c *> device_c::mydevices;

// argument is a device_c
// called reentrant in parallel for all different devices

// called on cancel and exit()
static void device_worker_pthread_cleanup_handler(void *context) 
{
	device_worker_c *worker_instance = (device_worker_c *) context;
	device_c *device = worker_instance->device;
#define this device // make INFO work
	worker_instance->running = false;
	INFO("%s::worker(%d) terminated.", device->name.value.c_str(), worker_instance->instance);
//	printf("cleanup for device %s\n", device->name.value.c_str()) ;
#undef this
}

static void *device_worker_pthread_wrapper(void *context) 
{
	device_worker_c *worker_instance = (device_worker_c *) context;
	device_c *device = worker_instance->device;
	int oldstate; // not used
#define this device // make INFO work
	// call real worker
	INFO("%s::worker(%u) started", device->name.value.c_str(), worker_instance->instance);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldstate); //ASYNCH not allowed!
	worker_instance->running = true;
	pthread_cleanup_push(device_worker_pthread_cleanup_handler, worker_instance);
		device->worker(worker_instance->instance);
		pthread_cleanup_pop(1); // call cleanup_handler on regular exit
	// not reached on pthread_cancel()
#undef this
	return NULL;
}

device_c::device_c() 
{
	// create work thread and run virtual "worker()" function in parallel

	// add reference to device to class list
	mydevices.push_back(this);

	parent = NULL;

	// init workers
	workers_terminate = false;
	set_workers_count(1); // default: 1 worker

	// do not link params to this device over param constructor
	// creation order of vector vs params?
	name.parameterized = this;
	type_name.parameterized = this;
	enabled.parameterized = this;
	verbosity.parameterized = this;
	verbosity.value = *log_level_ptr; // global default value from logger->logsource
	enabled.value = false; // must be activated by emulation logic/user interaction
	param_add(&name);
	param_add(&type_name);
	param_add(&enabled);
	param_add(&emulation_speed);
	param_add(&verbosity);
	emulation_speed.value = 1;
	init_asserted = false;

	// use registered parameters for logger interface
	log_label = name.value; // link logger params to this
	log_level_ptr = &(verbosity.value);
	// do not call virtual "reset()" here, sub class constructors must finish first
}

device_c::~device_c() 
{
	// registered parameters must be deleted by allocator
	parameter.clear();

	// remove device from class list
	// https://www.safaribooksonline.com/library/view/c-cookbook/0596007612/ch08s05.html
	std::list<device_c*>::iterator p = find(mydevices.begin(), mydevices.end(), this);
	if (p != mydevices.end())
		mydevices.erase(p);
}

// default is 1 worker. Default: null function, terminates if not overwritten by child class
// can be set > 1 if device needs multiple worker instances
// only to be called in constructors
void device_c::set_workers_count(unsigned workers_count) 
{
	workers.resize(workers_count);
	for (unsigned instance = 0; instance < workers_count; instance++) {
		device_worker_c *worker_instance = &workers[instance];
		worker_instance->device = this;
		worker_instance->instance = instance;
		worker_instance->running = false;
	}
}

bool device_c::on_param_changed(parameter_c *param) 
{
	if (param == &enabled) {
		if (enabled.new_value)
			workers_start();
		else
			workers_stop();
	}
	// all devices forward their "on_param_changed" to parent classes,
	// until a class rejects a value.
	// device_c is the grand parent and produces "OK" for unknown or passive parameters
	return true;
}

// search device in global list mydevices[]				
device_c *device_c::find_by_name(char *name) 
{
	std::list<device_c *>::iterator it;
	for (it = device_c::mydevices.begin(); it != device_c::mydevices.end(); ++it)
		if (!strcasecmp((*it)->name.value.c_str(), name))
			return *it;
	return NULL;
}

// set priority to max, keep policy, return current priority
// do not change worker_sched_priority
void device_c::worker_boost_realtime_priority() 
{
	int ret;
	// int prev_priority ;
	struct sched_param params;
	// ret = pthread_getschedparam(pthread_self(), &policy, &params);
	// assert(ret == 0) ;
	// prev_priority = params.sched_priority ;
	// set to max for current policy (FIFO or RR)
	params.sched_priority = sched_get_priority_max(worker_sched_policy);
	ret = pthread_setschedparam(pthread_self(), worker_sched_policy, &params);
	assert(ret == 0);
	// return prev_priority ;
}

// fast set to saved worker_sched_policy
void device_c::worker_restore_realtime_priority() 
{
	int ret;
	struct sched_param params;
	params.sched_priority = worker_sched_priority;		// std val, from init()
	ret = pthread_setschedparam(pthread_self(), worker_sched_policy, &params);
	assert(ret == 0);
}

// http://www.yonch.com/tech/82-linux-thread-priority
void device_c::worker_init_realtime_priority(enum worker_priority_e priority) 
{
	/* 1. set RT priority to 100% CPU time, without scheduler failsave.
	 * Endless loop in worker() will now hang the machine
	 */
	std::string rtperiod_path = "/proc/sys/kernel/sched_rt_runtime_us";
	// disable while debugging:: each error in thread requires a powercycle-reboot
	bool rttotal = true;
//	bool rttotal = false;
	std::fstream rtperiod;
	// /proc/sys/kernel/sched_rt_period_us containing 1000000 and /proc/sys/kernel/sched_rt_runtime_us containing 950000
	// See https://www.kernel.org/doc/Documentation/scheduler/sched-rt-group.txt

	switch (priority) {
	case rt_max:
		// 1. assert path exists
		if (!file_exists(&rtperiod_path)) {
			WARNING("kernel param %s not found.\n"
					"Verify \"uname -a\" shows a \"PREEMPT RT\" kernel build!",
					rtperiod_path.c_str());
		} else {
			if (rttotal) {
				// 2. set to -1 = unlimited
				rtperiod.open(rtperiod_path, std::ios::out | std::ios::trunc);
				rtperiod << "-1\n";
				rtperiod.close();
			}
			// 3. verify -1
			std::string line;
			rtperiod.open(rtperiod_path, std::ios::in);
			getline(rtperiod, line);
			rtperiod.close();
			if (line != "-1") {
				WARNING("can not set kernel param %s to \"-1\", is \"%s\".\n"
						"qunibusadapter_c::worker() may get interrupt by other tasks,\n"
						"resulting in ultra-long MSYN/SSYN cycles.", rtperiod_path.c_str(),
						line.c_str());
			} else {
				INFO(
						"%s set to -1:\n"
								"qunibusadapter_c::worker() is now un-interruptible and using 100%% RT cpu time.",
						rtperiod_path.c_str());
			}
		}
		worker_sched_policy = SCHED_FIFO;
		worker_sched_priority = sched_get_priority_max(SCHED_FIFO);
		break;
	case rt_device:
		// all device controllers and storage worker must run in parallel
		// (SO RR instead of SCHED), but higher than every Linux stad thread.
		worker_sched_policy = SCHED_RR;
		worker_sched_priority = 50;
		break;
	case none_rt:
		// default Linux time-share scheduling
		worker_sched_policy = SCHED_OTHER;
		worker_sched_priority = 0;
		break;
	}
	/* 2. set thread to max RT priority */
	{
		int ret;
		// We'll operate on the currently running thread.
		pthread_t this_thread = pthread_self();
		// struct sched_param is used to store the scheduling priority
		struct sched_param params;
		params.sched_priority = worker_sched_priority;
		INFO("Trying to set thread realtime priority = %d", (int)params.sched_priority);

		// Attempt to set thread real-time priority
		ret = pthread_setschedparam(this_thread, worker_sched_policy, &params);
		if (ret != 0) {
			// Print the error
			ERROR("Unsuccessful in setting thread realtime prio");
			return;
		}
		// Now verify the change in thread priority
		int policy = 0;
		ret = pthread_getschedparam(this_thread, &policy, &params);
		if (ret != 0) {
			ERROR("Couldn't retrieve real-time scheduling parameters");
			return;
		}

		// Check the correct policy was applied
		if (policy != SCHED_FIFO && policy != SCHED_RR) {
			INFO("Scheduling is not RT: neither SCHED_FIFO nor SCHED_RR!");
		} else {
			INFO("Scheduling is at RT priority.");
		}

		// Print thread scheduling priority
		INFO("Thread priority is %d", (int)params.sched_priority);
	}
}

/* worker_start - executes threads
 *
 * use of C++11 std::thread failed:
 * thread.join() crashes with random system_errors
 * So use classic "pthread" wrapper
 * TODO: crash still there, was caused by cross compile -> back to C++11 threads!
 */

void device_c::workers_start(void) 
{
	workers_terminate = false;
	for (unsigned instance = 0; instance < workers.size(); instance++) {
		device_worker_c *worker_instance = &workers[instance];
		worker_instance->running = true;
		// start pthread
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		// pthread_attr_setstacksize(&attr, 1024*1024);
		int status = pthread_create(&worker_instance->pthread, &attr,
				&device_worker_pthread_wrapper, (void *) worker_instance);
		if (status != 0) {
			FATAL("Failed to create pthread with status = %d", status);
		}
		pthread_attr_destroy(&attr); // why?
	}
}

void device_c::workers_stop(void) 
{
	timeout_c timeout;
	int status;

	workers_terminate = true; // global signal to all instances
	timeout.wait_ms(100);

	for (unsigned instance = 0; instance < workers.size(); instance++) {
		device_worker_c *worker_instance = &workers[instance];

//	if (!worker_instance->running) {
//		DEBUG("%s.worker_stop(%u): already terminated.", name.name.c_str(), instance);
//		return;
//	}
		if (worker_instance->running) {
			INFO("%s.worker(%u) not cooperative: cancel it ...", name.value.c_str(), instance);
			// if thread is hanging in pthread_cond_wait(): send a cancellation request
			status = pthread_cancel(worker_instance->pthread);
			if (status != 0)
				FATAL("Failed to send cancellation request to worker_pthread with status = %d",
						status);
		}

		// !! If crosscompiling: this causes a crash in the worker thread
		// !! at pthread_cond_wait() or other cancellation points.
		// !! No problem for compiles build on BBB itself.
		status = pthread_join(worker_instance->pthread, NULL);
		if (status != 0) {
			FATAL("Failed to join worker_pthread with status = %d", status);
		}
	}
}

