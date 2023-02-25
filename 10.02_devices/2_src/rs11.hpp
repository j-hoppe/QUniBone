/* 
    rs11.hpp: Implementation of RS11 DECdisk Fixed-Head Disk unit, used with RF11 controller. 

    Copyright (c) 2023 J. Dersch.
    Contributed under the BSD 2-clause license.

*/
#ifndef _RS11_HPP_
#define _RS11_HPP_

#include <stdint.h>
#include <string.h>
using namespace std;

#include "storagedrive.hpp"

class rs11_c: public storagedrive_c 
{
private:
        // Status bits
        uint32_t get_max_address();
        size_t clip_into_range(uint32_t wordAddress, size_t count);
     
public:

        // Commands
        bool read(uint32_t wordAddress, uint16_t* buffer, size_t count);
        bool write(uint32_t wordAddress, uint16_t* buffer, size_t count);
        void drive_reset(void);
         
public:
	rs11_c(storagecontroller_c *controller);

        bool on_param_changed(parameter_c* param) override;

	void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;

	void on_init_changed(void) override;

	// background worker function
	void worker(unsigned instance) override;
};

#endif
