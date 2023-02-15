/* 
    ke11.hpp: KE11 Arithmetic Element

    Copyright (c) 2023 J. Dersch.
    Contributed under the BSD 2-clause license.

 */
#ifndef _KE11_HPP_
#define _KE11_HPP_

#include <cstdint>

#include "utils.hpp"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"

class ke11_c: public qunibusdevice_c
{
private:
    // KE11 Registers (see Appendix B of manual):
    qunibusdevice_register_t *DIV_reg;   // Divide Register (777300)
    qunibusdevice_register_t *AC_reg;    // AC Register (777302) 
    qunibusdevice_register_t *MQ_reg;    // Multiplier/Quotient Register (777304)
    qunibusdevice_register_t *MUL_reg;   // Multiply Register (777306)
    qunibusdevice_register_t *SCSR_reg;  // SC/SR Register (777310)
    qunibusdevice_register_t *NOR_reg;   // Normalize Register (777312)
    qunibusdevice_register_t *LSH_reg;   // Logical Shift Register (777314)
    qunibusdevice_register_t *ASH_reg;   // Arithmetic Shift Register (777316)

    void read_register(qunibusdevice_register_t *device_reg, DATO_ACCESS access);
    void write_register(qunibusdevice_register_t *device_reg, DATO_ACCESS access);
 
    // Resets all register values on BUS INIT or Control Reset functions
    // and any other relevant local state.
    void reset_controller(void);

    uint32_t get_sign_byte(uint8_t value);
    uint32_t get_sign_word(uint16_t value);
    uint32_t get_sign_long(uint32_t value); 

    uint16_t set_SR(uint16_t ac, uint16_t mq, uint8_t sr);

    void update_AC(uint16_t value); 
    void update_MQ(uint16_t value);
    void update_SCSR(uint16_t sc, uint16_t sr);
  
    const uint32_t DMASK = 0xffff;

    enum SR_FLAGS
    {
       SR_C 	= 0x1,
       SR_SXT	= 0x2,
       SR_Z	= 0x4,
       SR_MQZ   = 0x8,
       SR_ACZ   = 0x10,
       SR_ACM1  = 0x20,
       SR_N     = 0x40,
       SR_NXV   = 0x80,
       SR_DYN   = SR_SXT | SR_Z | SR_MQZ | SR_ACZ | SR_ACM1
    };

public:

    ke11_c();
    virtual ~ke11_c();    

    // called by qunibusadapter on emulated register access
    void on_after_register_access(
        qunibusdevice_register_t *device_reg,
        uint8_t unibus_control,
        DATO_ACCESS access) override;

    bool on_param_changed(parameter_c *param) override;
    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
    void on_init_changed(void) override;
};


#endif
