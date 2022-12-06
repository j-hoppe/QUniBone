/* storagecontroller.hpp: a qunibus device with several "storagedrives" attached

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

 */

#ifndef _STORAGECONTROLLER_HPP_
#define _STORAGECONTROLLER_HPP_

#include <vector>

#include "qunibusdevice.hpp"
#include "storagedrive.hpp"

class storagecontroller_c: public qunibusdevice_c {
public:
	unsigned drivecount; // # of drives connected to controller
	std::vector<storagedrive_c *> storagedrives;

	// does not instantiate the drives
	storagecontroller_c(void);
	virtual ~storagecontroller_c(); // classes with virtual functions shoudl have virtual destructors
	
	virtual bool on_before_install(void) override ;
	virtual void on_after_uninstall(void) override ;

	virtual bool on_param_changed(parameter_c *param) override;
	virtual void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
	virtual void on_init_changed() override;
	virtual void on_drive_status_changed(storagedrive_c *drive) = 0;

};

#endif

