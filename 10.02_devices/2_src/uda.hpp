/*
    uda.hpp: MSCP controller port (UDA50)

    Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
    Contributed under the BSD 2-clause license.
*/

#pragma once

#include <memory>
#include "utils.hpp"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"
#include "storagecontroller.hpp"
#include "mscp_server.hpp"
#include "mscp_drive.hpp"

// The number of drives supported by the controller.
// This is arbitrarily fixed at 8 but could be set to any
// value up to 65535.
#define DRIVE_COUNT 8

// The control/microcode version info returned by SA in the fourth intialization step.
#define UDA50_ID 0x0063
#define RQDX3_ID 0x0133

// The maximum message length we can handle.  This is provided as a sanity check
// to prevent parsing clearly invalid commands.
#define MAX_MESSAGE_LENGTH 0x1000

#define STEP1    0x0800
#define STEP2    0x1000
#define STEP3    0x2000
#define STEP4    0x4000
#define STEP1_22_BIT 0x200

// Port-generic fatal error codes (AA-L621A-TK, p. 7-1)
#define PORT_ERROR                 0x8000
#define PORT_ERROR_PACKET_READ     1
#define PORT_ERROR_PACKET_WRITE    2
#define PORT_ERROR_RING_READ       6
#define PORT_ERROR_RING_WRITE      7 

// TODO: this currently assumes a little-endian machine!
#pragma pack(push,1)
struct Message
{
    uint16_t MessageLength;

    union
    {
        struct
        {
            uint16_t Credits : 4;
            uint16_t MessageType : 4;
            uint16_t ConnectionID : 8;
        } Info;
        uint16_t Word1;
    } Word1;

    uint8_t Message[sizeof(ControlMessageHeader)];
};
#pragma pack(pop)

/*
  This implements the Transport layer for a Qbus/Unibus MSCP controller.

  Logic for initialization, reset, and communcation with the MSCP Server
  is implemented here.
*/
class uda_c : public storagecontroller_c
{
public:
    uda_c();
    virtual ~uda_c();

	bool on_param_changed(parameter_c *param) override;

    void worker(unsigned instance) override;

    void on_after_register_access(
        qunibusdevice_register_t *device_reg,
        uint8_t unibus_control,
        DATO_ACCESS access) override;

    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
    void on_init_changed(void) override;

    void on_drive_status_changed(storagedrive_c *drive) override;
	
    // As every storage controller UDA has one INTR and DMA
    dma_request_c dma_request = dma_request_c(this) ; // operated by qunibusadapter
    intr_request_c intr_request = intr_request_c(this) ;

    // Configuration parameter for 22-bit DMA
    parameter_bool_c twenty_two_bit_DMA = parameter_bool_c(this, "22_bit_dma", "dma22",
        false, "Enable 22-bit DMA"); 
   	
public:

    //
    // Returns the next command message from the command ring, if any.
    // Returns NULL if the ring is empty.  error is set to true if
    // an error occurred while reading the message.
    //
    Message* GetNextCommand(bool* error);

    //
    // Posts a response message to the response ring and memory
    // if there is space.
    // Returns FALSE if the ring is full.
    bool PostResponse(Message* response);

    uint32_t GetControllerIdentifier(void);
    uint16_t GetControllerClassModel(void);
   
    uint32_t GetDriveCount(void);
    mscp_drive_c* GetDrive(uint32_t driveNumber);

private:
    // TODO: consolidate these private/public groups here 
    void Reset(void);
    void PortError(uint16_t error); 
    void Interrupt(uint16_t sa_value); 
    void Interrupt(void);

    uint32_t GetCommandDescriptorAddress(size_t index);
    uint32_t GetResponseDescriptorAddress(size_t index);

    enum ControllerType {
        UDA50 = 0,
        RQDX3 = 1,
    } _controllerType;

    bool _22bitDMA;
   
public:
    bool DMAWriteWord(uint32_t address, uint16_t word);
    uint16_t DMAReadWord(uint32_t address, bool& success);

    bool DMAWrite(uint32_t address, size_t lengthInBytes, uint8_t* buffer);
    uint8_t* DMARead(uint32_t address, size_t lengthInBytes, size_t bufferSize);

private:
    void update_SA(uint16_t value);

    // UDA50 registers:
    qunibusdevice_register_t *IP_reg;
    qunibusdevice_register_t *SA_reg;

    std::shared_ptr<mscp_server> _server;

    uint32_t _ringBase;

    // Lengths are in terms of slots (32 bits each) in the
    // corresponding rings.
    size_t   _commandRingLength;
    size_t   _responseRingLength;

    // The current slot in the ring being accessed.
    uint32_t _commandRingPointer;
    uint32_t _responseRingPointer;

    // Interrupt vector -- if zero, no interrupts
    // will be generated.
    uint32_t _interruptVector;
   
    // Interrupt enable flag
    bool _interruptEnable;

    // Purge interrupt enable flag
    bool _purgeInterruptEnable;

    // Value written during step1, saved
    // to make manipulation easier.
    uint16_t _step1Value;

    enum InitializationStep
    {
        Uninitialized = 0,
        Step1 = 1,
        Step2 = 2,
        Step3 = 4,
        Step4 = 8,
        Complete,
    };

    volatile InitializationStep _initStep;
    volatile bool _next_step;

    void StateTransition(InitializationStep nextStep);

    // TODO: this currently assumes a little-endian machine!
    #pragma pack(push,1)
    struct Descriptor
    {
        union 
        {
            uint16_t Word0;
            uint16_t EnvelopeLow;
        } Word0;

        union
        {
            uint16_t Word1;
            struct
            {
                uint16_t EnvelopeHigh : 2;
                uint16_t Reserved : 12;
                uint16_t Flag : 1;
                uint16_t Ownership : 1;
            } Fields;
        } Word1;
    };   
    #pragma pack(pop) 
};

