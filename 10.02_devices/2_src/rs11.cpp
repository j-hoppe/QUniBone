/* 
 rs11.cpp: implementation of RS11 disk units, attached to RF11 controller

 Copyright (c) 2023 J. Dersch.
 Contributed under the BSD 2-clause license.

 */

#include <assert.h>

using namespace std;

#include "logger.hpp"
#include "utils.hpp"
#include "timeout.hpp"
#include "rs11.hpp"
#include "rf11.hpp"

rs11_c::rs11_c(storagecontroller_c *parentController) :
		storagedrive_c(parentController)
{
   name.value = "RS11";
   type_name.value = "RS11";
   log_label = "RS11";
}

bool rs11_c::on_param_changed(parameter_c *param) 
{
   if (param == &enabled) 
   {
      if (!enabled.new_value) 
      {
         // disable switches power OFF.
         drive_reset();
      }
   }
   else if (image_is_param(param)) 
   {
      if (image_recreate_on_param_change(param) &&
          image_open(true)) 
      { 
          controller->on_drive_status_changed(this);
          image_filepath.value = image_filepath.new_value;
          return true;
      }
   }
   
   return storagedrive_c::on_param_changed(param); 
}

//
// Reset / Power handlers
//

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void rs11_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) 
{
   UNUSED(aclo_edge) ;
   // called at high priority.
   if (dclo_edge == SIGNAL_EDGE_RAISING) 
   {
      // power-on defaults
      drive_reset();
   }
}

void rs11_c::on_init_changed(void) 
{
   // called at high priority.
   if (init_asserted) 
   {
      drive_reset();
   }
}

//
// Disk actions (read/write)
//
bool rs11_c::read(uint32_t wordAddress, uint16_t* buffer, size_t count)
{
    size_t adjustedCount = clip_into_range(wordAddress, count);
    
    if (adjustedCount > 0)
    { 
        image_read(reinterpret_cast<uint8_t*>(buffer), wordAddress << 1, adjustedCount * 2);
    }
    return adjustedCount == count;
}

bool rs11_c::write(uint32_t wordAddress, uint16_t* buffer, size_t count)
{
    size_t adjustedCount = clip_into_range(wordAddress, count);
    if (adjustedCount > 0)
    {
        image_write(reinterpret_cast<uint8_t*>(buffer), wordAddress << 1, adjustedCount * 2);
    } 
    return adjustedCount == count;
}

uint32_t rs11_c::get_max_address()
{
    // TODO: make configurable 
    return (0x40000 * 8) - 1; 
}

size_t rs11_c::clip_into_range(uint32_t wordAddress, size_t count)
{
    int32_t diff = (wordAddress + count) - get_max_address();

    if (diff > 0)
    {
        // Adjust count so it fits within the available address space
        count = max(static_cast<uint32_t>(0), count - diff);
    }

    return count;
}

void rs11_c::drive_reset(void) 
{
}

void rs11_c::worker(unsigned instance) 
{
    UNUSED(instance) ; // only one
}

