/* 
    ke11_cpp: KE11-A/B Extended Arithmetic Element

    Copyright (c) 2023 J. Dersch.
    Contributed under the BSD 2-clause license.

    The actual math portions of this code are adapted from OpenSIMH's KE11-A code:

    Copyright (c) 1993-2008, Robert M Supnik
    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.
 */

#include <string.h>
#include <assert.h>

#include "logger.hpp"
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"
#include "ke11.hpp"   

ke11_c::ke11_c() : 
    qunibusdevice_c(), 
    _ac(0), 
    _mq(0),
    _sr(0),
    _sc(0)
{
    // static config
    name.value = "ke";
    type_name.value = "KE11";
    log_label = "ke";

    // base addr, intr-vector, intr level
    set_default_bus_params(0777300, 10, 0, 5);

    // The KE11 has eight registers
    register_count = 8;

    // Divide register (write only)
    DIV_reg = &(this->registers[0]); // @  base addr
    strcpy(DIV_reg->name, "DIV");
    DIV_reg->active_on_dati = true; 
    DIV_reg->active_on_dato = true;
    DIV_reg->reset_value =   0; 
    DIV_reg->writable_bits = 0177777; 

    // AC Register (read/write)
    AC_reg = &(this->registers[1]); // @  base addr + 2
    strcpy(AC_reg->name, "AC"); 
    AC_reg->active_on_dati = false; // no controller state change
    AC_reg->active_on_dato = true;
    AC_reg->reset_value = 0;
    AC_reg->writable_bits = 0177777;

    // MQ Register (read/write)
    MQ_reg = &(this->registers[2]); // @  base addr + 4
    strcpy(MQ_reg->name, "MQ");
    MQ_reg->active_on_dati = false;
    MQ_reg->active_on_dato = true;
    MQ_reg->reset_value = 0;
    MQ_reg->writable_bits = 0177777; 

    // Multiply register (write only)
    MUL_reg = &(this->registers[3]); // @  base addr + 6
    strcpy(MUL_reg->name, "MUL");
    MUL_reg->active_on_dati = true;
    MUL_reg->active_on_dato = true;
    MUL_reg->reset_value = 0;
    MUL_reg->writable_bits = 0177777;

    // SC/SR Registers (read/write)
    SCSR_reg = &(this->registers[4]); // @  base addr + 10
    strcpy(SCSR_reg->name, "SCSR");
    SCSR_reg->active_on_dati = false;
    SCSR_reg->active_on_dato = true;
    SCSR_reg->reset_value = 0;
    SCSR_reg->writable_bits = 0177777;  // write bits 8-7, 5-0

    // Normalize Register (read/write)
    NOR_reg = &(this->registers[5]); // @  base addr + 12
    strcpy(NOR_reg->name, "NOR");
    NOR_reg->active_on_dati = false;
    NOR_reg->active_on_dato = true;
    NOR_reg->reset_value = 0;
    NOR_reg->writable_bits = 0;

    // LSH Register (write only)
    LSH_reg = &(this->registers[6]); // @  base addr + 14
    strcpy(LSH_reg->name, "LSH");
    LSH_reg->active_on_dati = true;
    LSH_reg->active_on_dato = true;
    LSH_reg->reset_value = 0;
    LSH_reg->writable_bits = 0177777;

    // ASH Register (write only)
    ASH_reg = &(this->registers[7]); // @  base addr + 16
    strcpy(ASH_reg->name, "ASH");
    ASH_reg->active_on_dati = true;
    ASH_reg->active_on_dato = true;
    ASH_reg->reset_value = 0;
    ASH_reg->writable_bits = 0177777;
}
  
ke11_c::~ke11_c()
{
}

bool ke11_c::on_param_changed(parameter_c *param) 
{
   return qunibusdevice_c::on_param_changed(param);
}

//
// process DATI/DATO access to the KE11's "active" registers.
// !! called asynchronuously by PRU, with SSYN/RPLY asserted and blocking QBUS/UNIBUS.
// The time between PRU event and program flow into this callback
// is determined by ARM Linux context switch
//
// QBUS/UNIBUS DATO cycles let dati_flipflops "flicker" outside of this proc:
//      do not read back dati_flipflops.
void ke11_c::on_after_register_access(
    qunibusdevice_register_t *device_reg,
    uint8_t unibus_control,
    DATO_ACCESS access)
{
    if (unibus_control == QUNIBUS_CYCLE_DATI)
    {
       read_register(device_reg, access);
    }
    else
    {
       write_register(device_reg, access);
    }
}

void ke11_c::read_register(qunibusdevice_register_t *device_reg, DATO_ACCESS access)
{
    UNUSED(access);
    set_register_dati_value(device_reg, 0, "read_register");
}

void ke11_c::write_register(qunibusdevice_register_t *device_reg, DATO_ACCESS access)
{
   int32_t quo, t32, sout, sign;
   uint32_t absd, absr;

   switch (device_reg->index)
   {
      case 0:   // DIV
      {
          int32_t div = DIV_reg->active_dato_flipflops;

          if ((access == DATO_BYTEL) && get_sign_byte(div))    // byte write? 
          {
             div |= 0177400;                            // sext data to 16b
          }

          _sr = 0;					// N = V = C = 0
          t32 = (_ac << 16) | _mq;                      // 32b divd
          
          if (get_sign_word(_ac))                       // sext (divd)
          {
             t32 = t32 | ~017777777777;
          }

          if (get_sign_word(div))                       // sext (divr)
          {
             div = div | ~077777;
          }

          absd = abs(t32);
          absr = abs(div);

          if ((absd >> 16) >= absr)                     // divide fails?
          {
             sign = get_sign_word(_ac ^ div) ^ 1;        // 1 if signs match 
             _ac = (_ac << 1) | (_mq >> 15);
             _ac = (sign ? _ac - div : _ac + div) & DMASK;
             _mq = ((_mq << 1) | sign) & DMASK;
            
             if (get_sign_word(_ac ^ div) == 0)          // 0 if signs match
             {
                _sr |= SR_C;
             }           
 
             _sc = 15;                                   // SC clocked once
             _sr |= SR_NXV;                              // set overflow 
          }
          else 
          {
             _sc = 0;    
             quo = t32 / div;
             _mq = quo & DMASK;                          // MQ has quo
             _ac = (t32 % div) & DMASK;                  // AC has rem 
             if ((quo > 32767) || (quo < -32768))       // quo overflow?
             { 
                _sr |= SR_NXV;                           // set overflow
             } 
          }

          if (get_sign_word(_mq))                        // result negative? 
          {
              _sr ^= (SR_N | SR_NXV);                    // N = 1, compl NXV 
          }

          update_AC();
          update_MQ();
          update_SCSR();
       }
       break;

       case 1:   // AC
       {
          _ac = AC_reg->active_dato_flipflops;

          if (access == DATO_WORD)
          {
              update_AC();
          }
          else if (access == DATO_BYTEL && get_sign_byte(_ac))
          {
              // sign extend to 16b
              _ac |= 0177400;
              update_AC();
          }
          else
          {
              // ac = (ac & 0377) | (ac << 8);  // should be a no-op
              update_AC();
          }

          update_SCSR();
       }
       break;

       case 2:   // MQ
       {
          _mq = MQ_reg->active_dato_flipflops;

          if (access == DATO_BYTEL && get_sign_byte(_mq))
          {
             _mq |= 0177400; 
          }
          else if (access == DATO_BYTEH)
          {
             // mq = (mq & 0377) | (mq << 8);  // should be a no-op 
          }
 
          if (get_sign_word(_mq))
          {
             _ac = 0177777;   // sign extend MQ to AC
          }
          else
          {
             _ac = 0;
          }

          update_MQ();
          update_AC();
          update_SCSR();
       }
       break;
   
       case 3:   // MUL
       {
          int32_t mul = MUL_reg->active_dato_flipflops;
          _ac = 0; 
          _sc = 0;

          if ((access == DATO_BYTEL) && get_sign_byte(mul))    // byte write?
          {
             mul |= 0177400;                            // sext data to 16b
          } 
          if (get_sign_word (mul))                          // sext operands
          { 
             mul |= ~077777;
          }
          t32 = _mq;
          if (get_sign_word (t32)) 
          {
             t32 |= ~077777;
          }
          t32 = t32 * mul;
          _ac = (t32 >> 16) & DMASK;
          _mq = t32 & DMASK;
          if (get_sign_word (_ac))                         // result negative?
          {
             _sr = SR_N | SR_NXV;                	// N = 1, V = C = 0
          }
          else 
          {
             _sr = 0;                                   // N = 0, V = C = 0
          }

          update_MQ();
          update_AC();
          update_SCSR();
       }
       break;

       case 4:   // SC/SR
          if (access == DATO_WORD)                    // Accept only word writes
          {
             uint16_t value = SCSR_reg->active_dato_flipflops & (((SR_NXV | SR_N | SR_C) << 8) | 0xff);
             _sr = value >> 8;
             _sc = value;
             set_register_dati_value(SCSR_reg, value, "write_register");
             set_register_dati_value(NOR_reg, value & 0xff, "write_register");
          }
       break;

       case 5:   // NOR
       {
          for (_sc = 0; _sc < 31; _sc++)                // Max 31 shifts
          {
             if (((_ac == 0140000) && (_mq == 0)) ||
                 get_sign_word(_ac ^ (_ac << 1)))       // AC<15> != AC<14>?
             {
                break;
             }
             _ac = ((_ac << 1) | (_mq >> 15)) & DMASK;
             _mq = (_mq << 1) & DMASK;
          }

          if (get_sign_word(_ac))
          {
             _sr = SR_N | SR_NXV;
          }
          else
          {
             _sr = 0;
          }

          update_AC();
          update_MQ();
          update_SCSR();
       }
       break;

       case 6:  // LSH
       {
          _sc = 0;
          _sr = 0;                              // N = V = C = 0 
          uint16_t lsh = LSH_reg->active_dato_flipflops;
          
          lsh = lsh & 077;                              // 6b shift count 
          if (lsh != 0) 
          {
             t32 = (_ac << 16) | _mq;                      // 32b operand
             if ((sign = get_sign_word(_ac)))             // sext operand
             {
                t32 = t32 | ~017777777777;
             }

             if (lsh < 32) 
             {  // [1,31] - left 
                sout = (t32 >> (32 - lsh)) | (-sign << lsh);
                t32 = ((uint32_t)t32) << lsh;   // do shift (zext)
                if (sout != (get_sign_long(t32) ? -1 : 0))   // bits lost = sext?
                {
                    _sr |= SR_NXV;                       // no, V = 1
                }
                if (sout & 1)                           // last bit lost = 1? 
                {
                    _sr |= SR_C;                         // yes, C = 1 
                }
             }
             else 
             {                                           // [32,63] = -32,-1 
                if ((t32 >> (63 - lsh)) & 1)            // last bit lost = 1?
                { 
                    _sr |= SR_C;                         // yes, C = 1
                }
                t32 = (lsh != 32) ? ((uint32_t)t32) >> (64 - lsh) : 0;
             }
           
             _ac = (t32 >> 16) & DMASK;
             _mq = t32 & DMASK;
         }

         if (get_sign_word(_ac))                     // result negative?
         {
            _sr ^= (SR_N | SR_NXV);                  // N = 1, compl NXV 
         }

         update_MQ();
         update_AC();
         update_SCSR();
       }
       break;

       case 7:   // ASH
       {
          _sc = 0;
          _sr = 0;
          uint16_t ash = ASH_reg->active_dato_flipflops;
        
          ash = ash & 077;                              // 6b shift count 
          if (ash != 0) 
          {
             t32 = (_ac << 16) | _mq;                     // 32b operand 
             if ((sign = get_sign_word(_ac)))            // sext operand
             { 
                t32 = t32 | ~017777777777;
             }
             if (ash < 32)                              // [1,31] - left 
             {
                sout = (t32 >> (31 - ash)) | (-sign << ash);
                t32 = (t32 & 020000000000) | ((t32 << ash) & 017777777777);
                if (sout != (get_sign_long(t32)? -1: 0))  // bits lost = sext?
                {
                    _sr |= SR_NXV;                       // no, V = 1 
                }
                if (sout & 1)                            // last bit lost = 1? 
                {
                    _sr |= SR_C;                         // yes, C = 1 
                }
            }
            else                                        // [32,63] = -32,-1 
            {
                if ((t32 >> (63 - ash)) & 1)            // last bit lost = 1? 
                {
                    _sr |= SR_C;                         // yes, C = 1 
                }

                t32 = (ash != 32) ?                     // special case 32 
                    (static_cast<uint32_t>(t32) >> (64 - ash)) | (-sign << (ash - 32)) :
                    -sign;
            }

            _ac = (t32 >> 16) & DMASK;
            _mq = t32 & DMASK;
         }
        
         if (get_sign_word(_ac))                         // result negative? 
         {
            _sr ^= (SR_N | SR_NXV);                      // N = 1, compl NXV 
         }
   
         update_MQ();
         update_AC();
         update_SCSR();
       }
       break;
   }
}

uint32_t ke11_c::get_sign_byte(uint8_t value)
{
    return (value & 0x80) ? 1 : 0;
}

uint32_t ke11_c::get_sign_word(uint16_t value)
{
    return (value & 0x8000) ? 1 : 0;
}

uint32_t ke11_c::get_sign_long(uint32_t value)
{
    return (value & 0x80000000) ? 1 : 0;
}

void ke11_c::update_AC() 
{
    set_register_dati_value(AC_reg, _ac, "update_AC");
}

void ke11_c::update_MQ()
{
    set_register_dati_value(MQ_reg, _mq, "update_MQ");
}

void ke11_c::update_SCSR()
{
   _sr &= ~SR_DYN;                                    // clr dynamic bits
   if (_mq == 0)                                      // MQ == 0?
   {
      _sr |= SR_MQZ;
   }

   if (_ac == 0)                                      // AC == 0?
   {
      _sr |= SR_ACZ;
      if (get_sign_word(_mq) == 0)                    // MQ positive?
      {
         _sr |= SR_SXT;
      }

      if (_mq == 0)                                   // MQ zero?
      {
         _sr |= SR_Z;
      }
   }

   if (_ac == 0177777)                                // AC == 177777?
   {
      _sr |= SR_ACM1;
      if (get_sign_word(_mq) == 1)                    // MQ negative?
      {
         _sr |= SR_SXT;
      }
   }

   uint16_t value = ((uint16_t)_sr << 8) | (_sc & 0xff); 
   set_register_dati_value(SCSR_reg, value, "update_SCSR");
}

void ke11_c::reset_controller(void)
{
    // This will reset the DATI values to their defaults.
    // We then need to reset our copy of the values to correspond.
    reset_unibus_registers();
    _ac = 0;
    _mq = 0;
    _sr = 0;
    _sc = 0;
}

// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void ke11_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) 
{
    UNUSED(aclo_edge);

    if (dclo_edge == SIGNAL_EDGE_RAISING) 
    { 
        // power-on defaults
        reset_controller();
    }
}

// QBUS/UNIBUS INIT: clear all registers
void ke11_c::on_init_changed(void) 
{
    // write all registers to "reset-values"
    if (init_asserted) 
    {
        reset_controller();
    }
}
