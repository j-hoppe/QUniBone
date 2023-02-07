/*
    uda.cpp: Implementation of the MSCP port (qunibus interface).

    Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
    Contributed under the BSD 2-clause license.

    This provides logic for the UDA50's SA and IP registers, 
    the four-step initialization handshake, DMA transfers to and
    from the Qbus/Unibus, and the command/response ring protocols.

    While the name "UDA" is used here, this is not a strict emulation
    of a real UDA50 -- it is a general MSCP implementation and can be
    thought of as the equivalent of the third-party MSCP controllers
    from Emulex, CMD, etc. that were available. 

    At this time this class acts as the port for an MSCP controller.  
    It would be trivial to extend this to TMSCP at a future date.
*/

#include <string.h>
#include <strings.h>
#include <assert.h>

#include "logger.hpp"
#include "gpios.hpp"	// debug pin
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "qunibusdevice.hpp"
#include "storagecontroller.hpp"
#include "mscp_drive.hpp"
#include "uda.hpp"

uda_c::uda_c() :
        storagecontroller_c(),
        _controllerType(UDA50),
        _22bitDMA(false),
        _server(nullptr),
        _ringBase(0),
        _commandRingLength(0),
        _responseRingLength(0),
        _commandRingPointer(0),
        _responseRingPointer(0),
        _interruptVector(0),
        _interruptEnable(false),
        _purgeInterruptEnable(false),
        _step1Value(0),
        _initStep(InitializationStep::Uninitialized),
        _next_step(false)
{
    name.value = "uda";  
    type_name.value = "UDA50";
    type_name.readonly = false; 
    base_addr.readonly = false;
    log_label = "uda";
    _22bitDMA = twenty_two_bit_DMA.value = (qunibus->addr_width == 22); 

    // base addr, intr-vector, intr level
    set_default_bus_params(0772150, 20, 0154, 5) ;

    // The UDA50 controller has two registers.
    register_count = 2;

    IP_reg = &(this->registers[0]); // @ base addr
    strcpy(IP_reg->name, "IP");
    IP_reg->active_on_dati = true;
    IP_reg->active_on_dato = true;
    IP_reg->reset_value = 0;
    IP_reg->writable_bits = 0xffff;

    SA_reg = &(this->registers[1]); // @ base addr + 2
    strcpy(SA_reg->name, "SA");
    SA_reg->active_on_dati = false;
    SA_reg->active_on_dato = true;
    SA_reg->reset_value = 0;
    SA_reg->writable_bits = 0xffff;

    _server.reset(new mscp_server(this));

    //
    // Initialize drives.  We support up to eight attached drives.
    //
    drivecount = DRIVE_COUNT;
    for (uint32_t i=0; i<drivecount; i++)
    {
        mscp_drive_c *drive = new mscp_drive_c(this, i);
        drive->unitno.value = i;
        drive->activity_led.value = i ; // default: LED = unitno
        drive->name.value = name.value + std::to_string(i);
        drive->log_label = drive->name.value;
        drive->parent = this;
        storagedrives.push_back(drive);
    }
}

uda_c::~uda_c()
{
    for(uint32_t i=0; i<drivecount; i++)
    {
        delete storagedrives[i];
    }

    storagedrives.clear();
}

bool uda_c::on_param_changed(parameter_c *param) 
{
    // no own parameter or "enable" logic
    if (param == &priority_slot) 
    {
        dma_request.set_priority_slot(priority_slot.new_value);
        intr_request.set_priority_slot(priority_slot.new_value);
    }
    else if (param == &intr_level) 
    {
        intr_request.set_level(intr_level.new_value);
    } 
    else if (param == &intr_vector) 
    {
        return false;  // Not configurable for the UDA50.
    } 
    else if (param == &type_name) 
    {
        if (strcasecmp("uda50", type_name.new_value.c_str()) == 0) 
        {
            _controllerType = UDA50;
        }
        else if (strcasecmp("rqdx3", type_name.new_value.c_str()) == 0)
        {
            _controllerType = RQDX3;
        }
        else
        {
            return false;
        }
    }
    else if (param == &twenty_two_bit_DMA)
    {
        _22bitDMA = twenty_two_bit_DMA.new_value;
    }
      
    return storagecontroller_c::on_param_changed(param) ; // more actions (for enable)
}


//
// Reset():
//  Resets the UDA controller state.
//  Resets the attached MSCP server, which may take
//  significant time.
//
void uda_c::Reset(void)
{
    DEBUG_FAST("UDA reset");

    _server->Reset();

    _ringBase = 0;
    _commandRingLength = 0;
    _responseRingLength = 0;
    _commandRingPointer = 0;
    _responseRingPointer = 0;
    _interruptVector = 0;
    intr_vector.value = 0;
    _interruptEnable = false;
    _purgeInterruptEnable = false;
}

//
// GetDriveCount():
//  Returns the number of drives that can be attached to this controller.
//
uint32_t uda_c::GetDriveCount(void)
{
    return drivecount;
}
   
//
// GetDrive():
//  Returns a pointer to an mscp_drive_c object for the specified drive number.
//  This pointer is owned by the UDA class.
// 
mscp_drive_c* uda_c::GetDrive(
    uint32_t driveNumber)
{
    assert(driveNumber < drivecount);

    return dynamic_cast<mscp_drive_c*>(storagedrives[driveNumber]);
}

//
// StateTransition():
//  Transitions the UDA initialization state machine to the specified step,
//  atomically.
//
void uda_c::StateTransition(
    InitializationStep nextStep)
{
    pthread_mutex_lock(&on_after_register_access_mutex);
        _initStep = nextStep;
        _next_step = true;
    pthread_cond_signal(&on_after_register_access_cond);
    pthread_mutex_unlock(&on_after_register_access_mutex);
}

//
// worker():
//  Implements the initialization state machine.
//
void uda_c::worker(unsigned instance)
{
    UNUSED(instance) ; // only one

    worker_init_realtime_priority(rt_device); 

    timeout_c timeout;

    while (!workers_terminate)
    {
        //
        // Wait to be awoken.
        //
        pthread_mutex_lock(&on_after_register_access_mutex);
        while (!_next_step)
        {
            pthread_cond_wait(
                &on_after_register_access_cond,
                &on_after_register_access_mutex);
        }

        _next_step = false;
        pthread_mutex_unlock(&on_after_register_access_mutex);

        switch (_initStep)
        {
            case InitializationStep::Uninitialized:
                 DEBUG_FAST("Transition to Init state Uninitialized."); 
                 // SA should already be zero but we'll be extra sure here.
                 update_SA(0x0);

                 // Reset the controller: This may take some time as we must
                 // wait for the MSCP server to wrap up its current workitem.
                 Reset();
                 StateTransition(InitializationStep::Step1);
                 break;                

            case InitializationStep::Step1:
                 timeout.wait_us(500);  

                 DEBUG_FAST("Transition to Init state S1.");
                 //
                 // S1 is set, all other bits zero.  This indicates that we
                 // support a host-settable interrupt vector, that we do not
                 // implement enhanced diagnostics, and that no errors have
                 // occurred.
                 //
                 DEBUG_FAST("22 bit dma is %d", _22bitDMA);
                 update_SA(STEP1 | (_22bitDMA ? STEP1_22_BIT : 0x0));
                 break;

            case InitializationStep::Step2:
                 timeout.wait_us(500); 
                 DEBUG_FAST("Transition to Init state S2.");
                
                 // Update the SA read value for step 2:
                 // S2 is set, qunibus port type (0),  SA bits 15-8 written
                 // by the host in step 1.
                 Interrupt(STEP2 | ((_step1Value >> 8) & 0xff));
                 break;

            case InitializationStep::Step3:
                 timeout.wait_us(500); 

                 DEBUG_FAST("Transition to Init state S3.");
                 // Update the SA read value for step 3:
                 // S3 set, plus SA bits 7-0 written by the host in step 1.
                 Interrupt(STEP3 | (_step1Value & 0xff));
                 break;
 
            case InitializationStep::Step4:
                 timeout.wait_us(100);

                 // Clear communications area, set SA   
                 DEBUG_FAST("Clearing comm area at 0x%x. Purge header: %d", _ringBase, _purgeInterruptEnable);
                 DEBUG_FAST("resp 0x%x comm 0x%x", _responseRingLength, _commandRingLength);

                 {
                     int headerSize = _purgeInterruptEnable ? 8 : 4;
                     for(uint32_t i = 0; 
                         i < (_responseRingLength + _commandRingLength) * sizeof(Descriptor) + headerSize;
                         i += 2)
                     {
                         DMAWriteWord(_ringBase + i - headerSize, 0x0);
                     }
                 }

                 //
                 // Set the ownership bit on all descriptors in the response ring
                 // to indicate that the port owns them.
                 //
                 Descriptor blankDescriptor;
                 blankDescriptor.Word0.Word0 = 0;
                 blankDescriptor.Word1.Word1 = 0;
                 blankDescriptor.Word1.Fields.Ownership = 1;

                 for(uint32_t i = 0; i < _responseRingLength; i++)
                 {
                     DMAWrite(
                         GetResponseDescriptorAddress(i),
                         sizeof(Descriptor),
                         reinterpret_cast<uint8_t*>(&blankDescriptor));
                 }  
 
                 DEBUG_FAST("Transition to Init state S4, comm area initialized.");
                 // Update the SA read value for step 4:
                 // Bits 7-0 indicating our control microcode version.
                 Interrupt(STEP4 | (_controllerType == UDA50 ? UDA50_ID : RQDX3_ID)); 
                 break;

            case InitializationStep::Complete:
                 DEBUG_FAST("Initialization complete.");
                 break;
        }
    }
}


//
// on_after_register_access():
//  Handles register accesses for the IP and SA registers.
//
void
uda_c::on_after_register_access(
    qunibusdevice_register_t *device_reg,
    uint8_t unibus_control
)
{
    switch (device_reg->index)
    {
        case 0:  // IP - read / write
            if (QUNIBUS_CYCLE_DATO == unibus_control)
            {
                // "When written with any value, it causes a hard initialization
                //  of the port and the device controller."
                DEBUG_FAST("Reset due to IP read"); 
                update_SA(0x0); 
                StateTransition(InitializationStep::Uninitialized);
            }
            else
            {
                // "When read while the port is operating, it causes the controller
                //  to initiate polling..."
                if (_initStep == InitializationStep::Complete)
                {
                    DEBUG_FAST("Request to start polling.");
                    _server->InitPolling();
                }
            }
            break;

        case 1:  // SA - write only
            uint16_t value = SA_reg->active_dato_flipflops;

            switch (_initStep)
            {
                case InitializationStep::Uninitialized:
                    // Should not occur, we treat it like step1 here.
                    DEBUG_FAST("Write to SA in Uninitialized state.");

                case InitializationStep::Step1:
                    // Host writes the following:
                    // 15   13 11 10  8 7 6           0
                    // +-+-+-----+-----+-+-------------+
                    // |1|W|c rng|r rng|I| int vector  |
                    // | |R| lng | lng |E|(address / 4)|
                    // +-+-+-----+-----+-+-------------+
                    // WR = 1 tells the port to enter diagnostic wrap
                    // mode (which we ignore).
                    //
                    // c rng lng is the number of slots (32 bits each)
                    //  in the command ring, expressed as a power of two.
                    //
                    // r rng lng is as above, but for the response ring.
                    // 
                    // IE=1 means the host is requesting an interrupt
                    // at the end of the completion of init steps 1-3.
                    //
                    // int vector determines if interrupts will be generated
                    // by the port.  If this field is non-zero, interupts will
                    // be generated during normal operation and, if IE=1,
                    // during initialization.
                    _step1Value = value;

                    _interruptVector = ((value & 0x7f) << 2);

                    intr_request.set_vector(_interruptVector);
                    _interruptEnable = !!(value & 0x80);
                    _responseRingLength = (1 << ((value & 0x700) >> 8));
                    _commandRingLength = (1 << ((value & 0x3800) >> 11));

                    DEBUG_FAST("Step1: 0x%x", value); 
                    DEBUG_FAST("resp ring 0x%x", _responseRingLength);
                    DEBUG_FAST("cmd ring 0x%x", _commandRingLength);
                    DEBUG_FAST("vector 0x%x", _interruptVector);
                    DEBUG_FAST("ie %d", _interruptEnable);
 
                    // Move to step 2.
                    StateTransition(InitializationStep::Step2);
                    break;

                case InitializationStep::Step2:
                    // Host writes the following:
                    //  15                          1 0 
                    // +-----------------------------+-+ 
                    // |        ringbase low         |P| 
                    // |         (address)           |I|
                    // +-----------------------------+-+ 
                    // ringbase low is the low-order portion of word
                    // [ringbase+0] of the communications area.  This is a
                    // 16-bit byte address whose low-order bit is zero implicitly.
                    //
                    _ringBase = value & 0xfffe;
                    _purgeInterruptEnable = !!(value & 0x1);

                    DEBUG_FAST("Step2: rb 0x%x pi %d", _ringBase, _purgeInterruptEnable);
                    // Move to step 3 and interrupt as necessary.
                    StateTransition(InitializationStep::Step3);
                    break;

                case InitializationStep::Step3:
                    // Host writes the following:
                    // 15                              0
                    // +-+-----------------------------+ 
                    // |P|        ringbase hi          |
                    // |P|         (address)           | 
                    // +-+-----------------------------+
                    // PP = 1 means the host is requesting execution of
                    // purge and poll tests, which we ignore because we can.
                    //
                    // ringbase hi is the high-order portion of the address
                    // [ringbase+0].
                    _ringBase |= ((value & 0x7fff) << 16);

                    DEBUG_FAST("Step3: ringbase 0x%x", _ringBase);
                    // Move to step 4 and interrupt as necessary.
                    StateTransition(InitializationStep::Step4);
                    break;

                case InitializationStep::Step4:
                    // Host writes the following:
                    // 15             8 7           1 0 
                    // +---------------+-----------+-+-+ 
                    // |    reserved   |    burst  |L|G| 
                    // |               |           |F|O| 
                    // +---------------+-----------+-+-+ 
                    // Burst is one less than the max. number of longwords
                    // the host is willing to allow per DMA transfer.
                    // If zero, the port uses its default burst count.
                    //
                    // LF=1 means that the host wants a "last fail" response
                    // packet when initialization is complete.
                    //
                    // GO=1 means that the controller should enter its functional
                    // microcode as soon as initialization completes.
                    // 
                    // Note that if GO=0 when initialization completes, the port
                    // will continue to read SA until the host forces SA bit 0 to
                    // make the transition 0->1.
                    //
                    // There is no explicit interrupt at the end of Step 4.
                    //
                    // TODO: For now we ignore burst settings.
                    // We also ignore Last Fail report requests since we aren't
                    // supporting onboard diagnostics and there's nothing to
                    // report.
                    //
                    DEBUG_FAST("Step4: 0x%x", value);
                    if (value & 0x1)
                    {
                        //
                        // GO is set, move to the Complete state.  The worker will
                        // start the controller running.
                        //
                        StateTransition(InitializationStep::Complete);
                        // The VMS bootstrap expects SA to be zero IMMEDIATELY
                        // after completion. 
                        update_SA(0x0);
                    }
                    else
                    {
                        // GO unset, wait until it is.
                    }
                    break;

                case InitializationStep::Complete:
                    // "When zeroed by the host during both initialization and normal
                    //  operation, it signals the port that the host has successfully
                    //  completed a bus adapter purge in response to a port-initiated
                    //  purge request.
                    //  We don't deal with bus adapter purges, yet.
                    break;
            }
            break;

    }
}

//
// update_SA():
//  Updates the SA register value exposed by the Unibone.
//
void
uda_c::update_SA(uint16_t value)
{
    set_register_dati_value(
        SA_reg,
        value,
        "update_SA"); 
} 

//
// GetNextCommand():
//  Attempts to pull the next command from the command ring, if any
//  are available.
//  If successful, returns a pointer to a Message struct; this pointer
//  is owned by the caller.
//  On failure, nullptr is returned.  This indicates that the ring is
//  empty or that an attempt to access non-existent memory occurred.
//  The error pointer is set to true if an error occurred during the 
//  operation -- due to non-existent memory or invalid data.  
//  (In this case nullptr will also be returned.)
//
Message*
uda_c::GetNextCommand(bool* error) 
{
    timeout_c timer;
    *error = false;
 
    // Grab the next descriptor being pointed to    
    uint32_t descriptorAddress = 
        GetCommandDescriptorAddress(_commandRingPointer);

    DEBUG_FAST("Next descriptor (ring ptr 0x%x) address is o%o", 
        _commandRingPointer, 
        descriptorAddress);

    std::unique_ptr<Descriptor> cmdDescriptor(
        reinterpret_cast<Descriptor*>(
            DMARead(
                descriptorAddress,
                sizeof(Descriptor),
                sizeof(Descriptor))));

    // A null command descriptor indicates an NXM condition; we set SA to the appropriate
    // error code and reset the port.
    if (!cmdDescriptor)
    {
        PortError(PORT_ERROR_PACKET_READ);
        *error = true;
        return nullptr;
    }
 
    // Check owner bit: if set, ownership has been passed to us, in which case
    // we can attempt to pull the actual message from memory.
    if (cmdDescriptor->Word1.Fields.Ownership)
    {
        bool doInterrupt = false;

        uint32_t messageAddress =
            cmdDescriptor->Word0.EnvelopeLow |
            (cmdDescriptor->Word1.Fields.EnvelopeHigh << 16);

        DEBUG_FAST("Next message address is o%o, flag %d", 
            messageAddress, cmdDescriptor->Word1.Fields.Flag);

        //
        // Grab the message length; this is at messageAddress - 4
        //
        bool success = false;
        uint16_t messageLength = 
            DMAReadWord(
                messageAddress - 4,
                success);
       
        if (!success || messageLength <= 0 || messageLength >= MAX_MESSAGE_LENGTH)
        {
            PortError(PORT_ERROR_RING_READ);
            *error = true;
            return nullptr;
        }     
   
        std::unique_ptr<Message> cmdMessage(
            reinterpret_cast<Message*>(
                DMARead(
                    messageAddress - 4,
                    messageLength + 4, 
                    sizeof(Message)))); 

        if (!cmdMessage)
        {
            PortError(PORT_ERROR_RING_READ);
            *error = true;
            return nullptr;
        }

        //
        // Handle Ring Transitions (from full to not-full) and associated
        // interrupts.
        // If the previous entry in the ring is owned by the Port then that indicates
        // that the ring was previously full (i.e. the descriptor we're now returning
        // is the first free entry.)
        //
        if (cmdDescriptor->Word1.Fields.Flag)
        {
            //
            // Flag is set, host is requesting a transition interrupt.
            // Check the previous entry in the ring.            
            //            
            if (_commandRingLength == 1)
            {
                // Degenerate case:  If the ring is of size 1 we always interrupt.
                doInterrupt = true;
            }
            else
            {
                uint32_t previousDescriptorAddress =
                    GetCommandDescriptorAddress(
                        (_commandRingPointer - 1) % _commandRingLength);

                std::unique_ptr<Descriptor> previousDescriptor(
                    reinterpret_cast<Descriptor*>(
                        DMARead(
                            previousDescriptorAddress,
                            sizeof(Descriptor),
                            sizeof(Descriptor))));

                if (!previousDescriptor)
                {
                    PortError(PORT_ERROR_RING_READ);
                    *error = true;
                    return nullptr;
                }

                if (previousDescriptor->Word1.Fields.Ownership)
                {
                    // We own the previous descriptor, so the ring was previously
                    // full.
                    doInterrupt = true;
                }
            }            
        }

        //
        // Message retrieved; reset the Owner bit of the command descriptor,
        // set the Flag bit (to indicate that we've processed it)
        // and return a pointer to the message.
        //
        cmdDescriptor->Word1.Fields.Ownership = 0;
        cmdDescriptor->Word1.Fields.Flag = 1;
        if (!DMAWrite(
            descriptorAddress,
            sizeof(Descriptor),
            reinterpret_cast<uint8_t*>(cmdDescriptor.get())))
        {
            PortError(PORT_ERROR_RING_WRITE);
            *error = true;
            return nullptr;
        }

        //
        // Move to the next descriptor in the ring for next time.
        _commandRingPointer = (_commandRingPointer + 1) % _commandRingLength;

        // Post an interrupt as necessary.
        if (doInterrupt)
        {
            //
            // Set ring base - 4 to non-zero to indicate a transition.
            //
            DMAWriteWord(_ringBase - 4, 0x1);
            Interrupt();
        }

        return cmdMessage.release();
    }
   
    DEBUG_FAST("No descriptor found.  0x%x 0x%x", cmdDescriptor->Word0.Word0, cmdDescriptor->Word1.Word1);  
 
    // No descriptor available.
    return nullptr;
}

//
// PostResponse():
//  Posts the provided Message to the response ring.
//  Returns true on success, false otherwise.
//  TODO: Need to handle NXM, as above.
//
bool
uda_c::PostResponse(
    Message* response
)
{
    bool res = false;

    // Grab the next descriptor.
    uint32_t descriptorAddress = GetResponseDescriptorAddress(_responseRingPointer);
    std::unique_ptr<Descriptor> cmdDescriptor(
        reinterpret_cast<Descriptor*>(
            DMARead(
                descriptorAddress,
                sizeof(Descriptor),
                sizeof(Descriptor))));

    // TODO: if NULL is returned assume a bus error and handle it appropriately.

    //
    // Check owner bit: if set, ownership has been passed to us, in which case
    // we can use this descriptor and fill in the response buffer it points to.
    // If not, we return false to indicate to the caller the need to try again later.
    //
    if (cmdDescriptor->Word1.Fields.Ownership)
    {
        bool doInterrupt = false;

        uint32_t messageAddress =
            cmdDescriptor->Word0.EnvelopeLow |
            (cmdDescriptor->Word1.Fields.EnvelopeHigh << 16);

        //
        // Read the buffer length the host has allocated for this response.
        //
        // TODO:
        // If it is shorter than the buffer we're writing then we will need to
        // split the response into multiple responses.  I have never seen this happen,
        // however and I'm curious if the documentation (AA-L621A-TK) is simply incorrect:
        // "Note that if a controller's responses are less than or equal to 60 bytes, 
        //  then the controller need not check the size of the response slot."
        // All of the MSCP response messages are shorter than 60 bytes, so this is always
        // the case.  I'll also note that the spec states "The minimum acceptable size
        // is 60 bytes of message text" for the response buffer set up by the host and this
        // is *definitely* not followed by host drivers.
        //
        // The doc is also not exactly clear what a fragmented set of responses looks like...
        // 
        // Message length is at messageAddress - 4 -- this is the size of the command
        // not including the two header words.
        //
        bool success = false;
        uint16_t messageLength =
            DMAReadWord(
                messageAddress - 4,
                success);

        DEBUG_FAST("response address o%o length o%o", messageAddress, response->MessageLength);

        assert(reinterpret_cast<uint16_t*>(response)[0] > 0);

        if (messageLength == 0)
        { 
            // A lot of bootstraps appear to set up response buffers of length 0.
            // We just log the behavior.
            DEBUG_FAST("Host response buffer size is zero.");
        }
        else if (messageLength < response->MessageLength)
        {
            //
            // If this happens it's likely fatal since we're not fragmenting responses (see the big comment
            // block above).  So eat flaming death.     
            // Note: the VMS bootstrap does this, so we'll just log the issue.
            //
            DEBUG_FAST("Response buffer 0x%x > host buffer length 0x%x", response->MessageLength, messageLength);
        }

        //
        // This will fit; simply copy the response message over the top
        // of the buffer allocated on the host -- this updates the header fields
        // as necessary and provides the actual response data to the host.
        //
        DMAWrite(
            messageAddress - 4,
            response->MessageLength + 4,
            reinterpret_cast<uint8_t*>(response));

        //
        // Check if a transition from empty to non-empty occurred, interrupt if requested.
        //
        // If the previous entry in the ring is owned by the Port then that indicates
        // that the ring was previously empty (i.e. the descriptor we're now returning
        // is the first entry returned to the ring by the Port.)
        //
        if (cmdDescriptor->Word1.Fields.Flag)
        {
            //
            // Flag is set, host is requesting a transition interrupt.
            // Check the previous entry in the ring.
            //            
            if (_responseRingLength == 1)
            {
                // Degenerate case:  If the ring is of size 1 we always interrupt.
                doInterrupt = true;
            }
            else
            {
                uint32_t previousDescriptorAddress =
                    GetResponseDescriptorAddress(
                    (_responseRingPointer - 1) % _responseRingLength);

                std::unique_ptr<Descriptor> previousDescriptor(
                    reinterpret_cast<Descriptor*>(
                        DMARead(
                            previousDescriptorAddress,
                            sizeof(Descriptor),
                            sizeof(Descriptor))));

                if (previousDescriptor->Word1.Fields.Ownership)
                {
                    // We own the previous descriptor, so the ring was previously
                    // full.
                    doInterrupt = true;
                }
            }
        }

        //
        // Message posted; reset the Owner bit of the response descriptor,
        // and set the Flag bit (to indicate that we've processed it).
        //
        cmdDescriptor->Word1.Fields.Ownership = 0;
        cmdDescriptor->Word1.Fields.Flag = 1;
        DMAWrite(
            descriptorAddress,
            sizeof(Descriptor),
            reinterpret_cast<uint8_t*>(cmdDescriptor.get()));

        // Post an interrupt as necessary.
        if (doInterrupt)
        {
            DEBUG_FAST("Response ring no longer empty, interrupting.");
            //
            // Set ring base - 2 to non-zero to indicate a transition.
            //
            DMAWriteWord(_ringBase - 2, 0x1);
            Interrupt();
        }

        res = true;
        
        // Move to the next descriptor in the ring for next time.
        _responseRingPointer = (_responseRingPointer + 1) % _responseRingLength;
    }

    return res;
}

//
// GetControllerIdentifier():
//  Returns the ID used by SET CONTROLLER CHARACTERISTICS.
//  This should be unique per controller.
//
uint32_t
uda_c::GetControllerIdentifier()
{
    // TODO: make this not hardcoded
    // ID 0x12345678
    return 0x12345678;
}

//
// GetControllerClassModel():
//  Returns the Class and Model information used by SET CONTROLLER CHARACTERISTICS.
//
uint16_t
uda_c::GetControllerClassModel()
{
    return 0x0102;   // Class 1 (mass storage), model 2 (UDA50)
}

//
// PortError():
//  Sets the SA register to the specified error code and resets the port.
//
void
uda_c::PortError(uint16_t error)
{
    DEBUG_FAST("Resetting port due to error o%o", error);
    update_SA(PORT_ERROR | error);
    StateTransition(InitializationStep::Uninitialized);
}

//
// Interrupt():
//  Invokes a Qbus/Unibus interrupt if interrupts are enabled and the interrupt
//  vector is non-zero.  Updates SA to the specified value atomically.
//
void
uda_c::Interrupt(uint16_t sa_value)
{
    if ((_interruptEnable || _initStep == InitializationStep::Complete) && _interruptVector != 0)
    {
        qunibusadapter->INTR(intr_request, SA_reg, sa_value);
    }
    else
    {
        update_SA(sa_value);
    }
}

//
// Interrupt():
//  Invokes a Qbus/Unibus interrupt if interrupts are enabled and the interrupt
//  vector is non-zero.
//
void
uda_c::Interrupt(void)
{
    if ((_interruptEnable || _initStep == InitializationStep::Complete) && _interruptVector != 0)
    {
        qunibusadapter->INTR(intr_request, NULL, 0); 
    }
}

//
// on_power_changed():
//  Resets the controller and all attached drives.
//
// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void
uda_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge)
{
    storagecontroller_c::on_power_changed(aclo_edge, dclo_edge);
  
    if (dclo_edge == SIGNAL_EDGE_RAISING)
    {
        DEBUG_FAST("Reset due to power change");
        StateTransition(InitializationStep::Uninitialized);
    }
}

//
// on_init_changed():
//  Resets the controller and all attached drives.
//
void
uda_c::on_init_changed(void)
{
    if (init_asserted)
    {
        DEBUG_FAST("Reset due to INIT");
        StateTransition(InitializationStep::Uninitialized);
    }

    storagecontroller_c::on_init_changed();
}

//
// on_drive_status_changed():
//  A no-op.  The controller doesn't require any drive notifications.
//
void
uda_c::on_drive_status_changed(storagedrive_c *drive)
{
    UNUSED(drive);    
}

//
// GetCommandDescriptorAddress():
//  Returns the address of the given command descriptor in the command ring.
//
uint32_t 
uda_c::GetCommandDescriptorAddress(
    size_t index
)
{
    return _ringBase + _responseRingLength * sizeof(Descriptor) +
            index * sizeof(Descriptor);
}

//
// GetResponseDescriptorAddress():
//  Returns the address of the given response descriptor in the response ring.
//
uint32_t
uda_c::GetResponseDescriptorAddress(
    size_t index
)
{
    return  _ringBase + index * sizeof(Descriptor);
}

//
// DMAWriteWord():
//  Writes a single word to Qbus/Unibus memory.  Returns true 
//  on success; if false is returned this is due to an NXM condition.
//
bool
uda_c::DMAWriteWord(
    uint32_t address,
    uint16_t word)
{
    return DMAWrite(
        address,
        sizeof(uint16_t),
        reinterpret_cast<uint8_t*>(&word));
}

//
// DMAReadWord():
//  Read a single word from Qbus/Unibus memory.  Returns the word read on success.
//  the success field indicates the success or failure of the read.
//
uint16_t
uda_c::DMAReadWord(
    uint32_t address,
    bool& success)
{
    uint8_t* buffer = DMARead(
        address,
        sizeof(uint16_t),
        sizeof(uint16_t));

    if (buffer)
    {
        success = true;
        uint16_t retval = *reinterpret_cast<uint16_t *>(buffer);
        delete[] buffer;
        return retval;
    }
    else
    {
        success = false;
        return 0;
    }
}


//
// DMAWrite():
//  Write data from the provided buffer to Qbus/Unibus memory.  Returns true
//  on success; if false is returned this is due to an NXM condition.
//  The address specified in 'address' must be word-aligned and the
//  length must be even.
//
bool
uda_c::DMAWrite(
    uint32_t address,
    size_t lengthInBytes,
    uint8_t* buffer)
{
    assert ((lengthInBytes % 2) == 0);
    assert (address < 2* qunibus->addr_space_word_count); // exceeds address space? test for IOpage too?

    qunibusadapter->DMA(dma_request, true,
            QUNIBUS_CYCLE_DATO,
            address,
            reinterpret_cast<uint16_t*>(buffer),
            lengthInBytes >> 1);
	return dma_request.success ;
}

//
// DMARead():
// Read data from Qbus/Unibus memory into the returned buffer.
// Buffer returned is nullptr if memory could not be read.
// Caller is responsible for freeing the buffer when done.
// The address specified in 'address' must be word-aligned
// and the length must be even.
//
uint8_t*
uda_c::DMARead(
    uint32_t address,
    size_t lengthInBytes,
    size_t bufferSize)
{
    assert (bufferSize >= lengthInBytes);
    assert((lengthInBytes % 2) == 0);
    assert (address < 2* qunibus->addr_space_word_count); // exceeds address space? test for IOpage too?

    uint16_t* buffer = new uint16_t[bufferSize >> 1];

    assert(buffer);

    memset(reinterpret_cast<uint8_t*>(buffer), 0xc3, bufferSize);

    qunibusadapter->DMA(dma_request, true,
                QUNIBUS_CYCLE_DATI,
                address,
                buffer,
                lengthInBytes >> 1);

    if (dma_request.success)
    { 
	return reinterpret_cast<uint8_t*>(buffer);
    }
    else
    {
//    ARM_DEBUG_PIN0(1) ;
//printf("UDA.DMARead NXM: address=%07o,len=%d words, failing addr=%07o",
//		address, lengthInBytes >> 1, dma_request.qunibus_end_addr ) ;
        return nullptr;     
    }
} 
