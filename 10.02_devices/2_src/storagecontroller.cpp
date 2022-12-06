/* storagecontroller.cpp: a qunibus device with several "storagedrives" attached

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

 A qunibus device with several "storagedrives"
 supports the "attach" command
 */
#include "utils.hpp"

#include "storagecontroller.hpp"

storagecontroller_c::storagecontroller_c():  	  qunibusdevice_c() 
{
	// sub class (Like "RL11") must create drives into array
	this->drivecount = 0;
}

storagecontroller_c::~storagecontroller_c() 
{
}

// called when "enabled" goes true, before registers plugged to QBUS/UNIBUS
// result false: configuration error, do not install
bool storagecontroller_c::on_before_install(void) 
{
	return true ;
}

void storagecontroller_c::on_after_uninstall(void) 
{
	// power/up/down attached drives, then plug to QBUS/UNIBUS
	// if disable, disable also the drives ("controller plugged from QBUS/UNIBUS)")
	// on enable, leave them disabled (user may decide which to use)
	for (unsigned i = 0; i < drivecount; i++)
		storagedrives[i]->enabled.set(false);
}


// implements params, so must handle "change"
bool storagecontroller_c::on_param_changed(parameter_c *param) 
{
	return qunibusdevice_c::on_param_changed(param); // more actions (for enable)
}

// forward BUS events to connected storage drives

// drives are powered if controller is powered
// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void storagecontroller_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) 
{
	std::vector<storagedrive_c*>::iterator it;
	for (it = storagedrives.begin(); it != storagedrives.end(); it++) {
		// drives should evaluate only DCLO for power to simulate wall power.
		(*it)->on_power_changed(aclo_edge, dclo_edge);
	}
}

// drives get INIT if controller got it
void storagecontroller_c::on_init_changed() 
{
	std::vector<storagedrive_c*>::iterator it;
	for (it = storagedrives.begin(); it != storagedrives.end(); it++) {
		(*it)->init_asserted = init_asserted;
		(*it)->on_init_changed();
	}
}

