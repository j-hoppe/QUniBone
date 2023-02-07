/*
    mscp_server.cpp: Implementation of a simple MSCP server.

    Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
    Contributed under the BSD 2-clause license.

    This provides an implementation of the Minimal MSCP subset outlined
    in AA-L619A-TK (Chapter 6).  It takes a few liberties and errs on 
    the side of implementation simplicity.

    In particular:
         All commands are executed sequentially, as they appear in the
         command ring.  This includes any commands in the "Immediate"
         category.  Technically this is incorrect:  Immediate commands
         should execute as soon as possible, before any other commands.
         In practice I have yet to find code that cares.

         This simplifies the implementation significantly, and apart
         from maintaining fealty to the MSCP spec for Immediate commands,
         there's no good reason to make it more complex:  real MSCP
         controllers (like the original UDA50) would resequence commands
         to allow optimal throughput across multiple units, etc.  On the
         Unibone, the underlying storage and the execution speed of the
         processor is orders of magnitude faster, so even a brute-force
         braindead implementation like this can saturate the Unibus.

    TODO:
    - Some commands aren't checked as thoroughly for errors as they could be.
    - Not all Invalid Command responses include the subcode data (which should,
      per section 5.5 of the MSCP spec, be the byte offset of the offending data
      in the invalid message.)  This is only really useful for diagnostic purposes
      and so the lack of it should not normally cause issues.
    - Same for the "flag" field, this is entirely unpopulated. 
*/
#include <assert.h>
#include <cstddef>
#include <pthread.h>
#include <stdio.h>
#include <memory>
#include <queue>
 
#include "logger.hpp"
#include "utils.hpp"

#include "mscp_drive.hpp"
#include "mscp_server.hpp"
#include "uda.hpp"

//
// polling_worker():
//  Runs the main MSCP polling thread.
//
void* polling_worker(
    void *context)
{
    mscp_server* server = reinterpret_cast<mscp_server*>(context);
    server->Poll();
    return nullptr;
}

mscp_server::mscp_server(
    uda_c *port) :
        device_c(),
        _hostTimeout(0),
        _controllerFlags(0),
        _abort_polling(false),
        _pollState(PollingState::Wait),
        polling_cond(PTHREAD_COND_INITIALIZER),
        polling_mutex(PTHREAD_MUTEX_INITIALIZER),
        _credits(INIT_CREDITS) 
{
    set_workers_count(0) ; // no std worker()
    name.value = "mscp_server" ;
    type_name.value = "mscp_server_c" ;
    log_label = "MSSVR" ;
    // Alias the port pointer.  We do not own the port, we merely reference it.
    _port = port;

    enabled.set(true) ; 
    enabled.readonly = true ; // always active

    StartPollingThread();
}


mscp_server::~mscp_server()
{
    AbortPollingThread();
}


bool mscp_server::on_param_changed(parameter_c *param) 
{
    // no own parameter or "enable" logic
    if (param == &enabled) 
    {
        // accept, but do not react on enable/disable, always active
        return true ;
    }
    return device_c::on_param_changed(param) ; // more actions (for enable)
}



//
// StartPollingThread():
//  Initializes the MSCP polling thread and starts it running.
// 
void
mscp_server::StartPollingThread(void)
{
    _abort_polling = false;
    _pollState = PollingState::Wait;

    //
    // Initialize the polling thread and start it.
    // It will wait to be woken to do actual work.
    //
    pthread_attr_t attribs;
    pthread_attr_init(&attribs);

    int status = pthread_create(
        &polling_pthread,
        &attribs,
        &polling_worker,
        reinterpret_cast<void*>(this));

    if (status != 0)
    {
        FATAL("Failed to start mscp server thread.  Status 0x%x", status);
    }

    DEBUG_FAST("Polling thread created.");
}

//
// AbortPollingThread():
//  Stops the MSCP polling thread.
//
void
mscp_server::AbortPollingThread(void)
{
    pthread_mutex_lock(&polling_mutex);
    _abort_polling = true;
    _pollState = PollingState::Wait;
    pthread_cond_signal(&polling_cond);
    pthread_mutex_unlock(&polling_mutex);

    pthread_cancel(polling_pthread);

    uint32_t status = pthread_join(polling_pthread, NULL);

    if (status != 0)
    {
        FATAL("Failed to join polling thread, status 0x%x", status);
    }

    DEBUG_FAST("Polling thread aborted.");  
}

//
// Poll():
//  The MSCP polling thread.  
//  This thread waits to be awoken, then pulls messages from the MSCP command
//  ring and executes them.  When no work is left to be done, it goes back to
//  sleep.
//  This is awoken by a write to the UDA IP register.
//
void
mscp_server::Poll(void)
{
    worker_init_realtime_priority(rt_device);

    while(!_abort_polling)
    {
        //
        // Wait to be awoken, then pull commands from the command ring
        //
        pthread_mutex_lock(&polling_mutex);
        while (_pollState == PollingState::Wait)
        {
            pthread_cond_wait(
                &polling_cond,
                &polling_mutex);
        }

        // Shouldn't happen but if it does we just return to the top.
        if (_pollState == PollingState::InitRun)
        {
           _pollState = PollingState::Run;
        }

        pthread_mutex_unlock(&polling_mutex);
    
        if (_abort_polling)
        {
            break;
        }

        //
        // Read all commands from the ring into a queue; then execute them.
        //
        std::queue<std::shared_ptr<Message>> messages;

        int msgCount = 0;
        while (!_abort_polling && _pollState != PollingState::InitRestart)
        {
            bool error = false;
            std::shared_ptr<Message> message(_port->GetNextCommand(&error));
            if (error)
            {
                DEBUG_FAST("Error while reading messages, returning to idle state.");
                // The lords of STL decreed that queue should have no "clear" method
                // so we do this garbage instead:
                messages = std::queue<std::shared_ptr<Message>>(); 
                break; 
            }
            if (nullptr == message)
            {
                DEBUG_FAST("End of command ring; %d messages to be executed.", msgCount);
                break;
            }

            msgCount++;
            messages.push(message);
        } 

        //
        // Pull commands from the queue until it is empty or we're told to quit.
        //
        while(!messages.empty() && !_abort_polling && _pollState != PollingState::InitRestart)
        {
            std::shared_ptr<Message> message(messages.front());  
            messages.pop();

            //
            // Handle the message.  We dispatch on opcodes to the
            // appropriate methods.  These methods modify the message
            // object in place; this message object is then posted back
            // to the response ring.
            //
            ControlMessageHeader* header = 
                reinterpret_cast<ControlMessageHeader*>(message->Message);

            DEBUG_FAST("Message size 0x%x opcode 0x%x rsvd 0x%x mod 0x%x unit %d, ursvd 0x%x, ref 0x%x", 
                message->MessageLength,
                header->Word3.Command.Opcode,
                header->Word3.Command.Reserved,
                header->Word3.Command.Modifiers,
                header->UnitNumber,
                header->Reserved,
                header->ReferenceNumber);

            bool protocolError = false;
            uint32_t cmdStatus = 0;
            uint16_t modifiers = header->Word3.Command.Modifiers;

            switch (header->Word3.Command.Opcode)
            {
                case Opcodes::ABORT:
                    cmdStatus = Abort();
                    break;

                case Opcodes::ACCESS:
                    cmdStatus = Access(message, header->UnitNumber);
                    break;

                case Opcodes::AVAILABLE:
                    cmdStatus = Available(header->UnitNumber, modifiers);
                    break;

                case Opcodes::COMPARE_HOST_DATA:
                    cmdStatus = CompareHostData(message, header->UnitNumber);
                    break;

                case Opcodes::DETERMINE_ACCESS_PATHS:
                    cmdStatus = DetermineAccessPaths(header->UnitNumber);
                    break;

                case Opcodes::ERASE:
                    cmdStatus = Erase(message, header->UnitNumber, modifiers);
                    break;

                case Opcodes::GET_COMMAND_STATUS:
                    cmdStatus = GetCommandStatus(message);
                    break;

                case Opcodes::GET_UNIT_STATUS:
                    cmdStatus = GetUnitStatus(message, header->UnitNumber, modifiers);
                    break;

                case Opcodes::ONLINE:
                    cmdStatus = Online(message, header->UnitNumber, modifiers);
                    break;

                case Opcodes::READ:
                    cmdStatus = Read(message, header->UnitNumber, modifiers);
                    break;

                case Opcodes::REPLACE:
                    cmdStatus = Replace(message, header->UnitNumber);
                    break;

                case Opcodes::SET_CONTROLLER_CHARACTERISTICS:
                    cmdStatus = SetControllerCharacteristics(message);     
                    break;

                case Opcodes::SET_UNIT_CHARACTERISTICS:
                    cmdStatus = SetUnitCharacteristics(message, header->UnitNumber, modifiers);
                    break;

                case Opcodes::WRITE:
                    cmdStatus = Write(message, header->UnitNumber, modifiers);
                    break;

                default:
                    DEBUG_FAST("Unimplemented MSCP command 0x%x", header->Word3.Command.Opcode);
                    protocolError = true;
                    break;
            }

            if (protocolError)
            {
                uint16_t subCode = offsetof(ControlMessageHeader, Word3) + HEADER_OFFSET;
                cmdStatus = STATUS(Status::INVALID_COMMAND, subCode, 0);
            }

            DEBUG_FAST("cmd 0x%x st 0x%x fl 0x%x", cmdStatus, GET_STATUS(cmdStatus), GET_FLAGS(cmdStatus));

            //
            // Set the endcode and status bits
            //
            header->Word3.End.Status = GET_STATUS(cmdStatus);
            header->Word3.End.Flags = GET_FLAGS(cmdStatus);

            // Set the End code properly -- for a protocol error, 
            // this is just the End code, for all others it's the End code
            // or'd with the original opcode.
            if (protocolError)
            {
                 // Just the END code, no opcode
                 header->Word3.End.Endcode = Endcodes::END;
            }
            else
            {
                 header->Word3.End.Endcode |= Endcodes::END;
            }

            if (message->Word1.Info.MessageType == MessageTypes::Sequential &&
                header->Word3.End.Endcode & Endcodes::END)
            {
                //
                // We steal the credits hack from simh:
                // The controller gives all of its credits to the host,
                // thereafter it supplies one credit for every response
                // packet sent.
                // 
                uint8_t grantedCredits = std::min(_credits, static_cast<uint8_t>(MAX_CREDITS));
                _credits -= grantedCredits;
                message->Word1.Info.Credits = grantedCredits + 1;
                DEBUG_FAST("granted credits %d", grantedCredits + 1);
            }
            else
            {
                message->Word1.Info.Credits = 0;
            }

            //
            // Post the response to the port's response ring.
            // If everything is working properly, there should always be room.
            //
            if(!_port->PostResponse(message.get()))
            {
                FATAL("Unexpected: no room in response ring.");
            }

            //
            // Go around and pick up the next one.
            //
        }

        //
        // Go back to sleep.  If a UDA reset is pending, we need to signal
        // the Reset() call so it knows we've completed our poll and are
        // returning to sleep (i.e. the polling thread is now reset.)
        //
        pthread_mutex_lock(&polling_mutex); 
        if (_pollState == PollingState::InitRestart)
        {
            DEBUG_FAST("MSCP Polling thread reset.");
            // Signal the Reset call that we're done so it can return
            // and release the Host.
            _pollState = PollingState::Wait;
            pthread_cond_signal(&polling_cond);
        }
        else if (_pollState == PollingState::InitRun)
        {
            _pollState = PollingState::Run;
        }
        else
        { 
            _pollState = PollingState::Wait;
        }
        pthread_mutex_unlock(&polling_mutex);
        
    }
    DEBUG_FAST("MSCP Polling thread exiting."); 
}

//
// The following are all implementations of the MSCP commands we support.
//
 
uint32_t
mscp_server::Abort()
{
    INFO("MSCP ABORT");

    //
    // Since we do not reorder messages and in fact pick up and execute
    // them one at a time, sequentially as they appear in the ring buffer,
    // by the time we've gotten this command, the command it's referring
    // to is long gone.
    // This is semi-legal behavior and it's legal for us to ignore ABORT in this
    // case.
    //
    // We just return SUCCESS here.
    return STATUS(Status::SUCCESS, 0, 0);
}

uint32_t
mscp_server::Available(
    uint16_t unitNumber,
    uint16_t modifiers)
{
    UNUSED(modifiers);

    // Message has no message-specific data.
    // Just set the specified drive as Available if appropriate.
    // We do nothing with the spin-down modifier.
    DEBUG_FAST("MSCP AVAILABLE");

    mscp_drive_c* drive = GetDrive(unitNumber);

    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }

    drive->SetOffline();

    return STATUS(Status::SUCCESS, 0x40, 0);  // still connected    
}

uint32_t
mscp_server::Access(
    std::shared_ptr<Message> message,
    uint16_t unitNumber)
{
    INFO("MSCP ACCESS");

    return DoDiskTransfer(
        Opcodes::ACCESS,
        message,
        unitNumber,
        0);
}

uint32_t
mscp_server::CompareHostData(
    std::shared_ptr<Message> message,
    uint16_t unitNumber)
{
    INFO("MSCP COMPARE HOST DATA");
    return DoDiskTransfer(
        Opcodes::COMPARE_HOST_DATA,
        message,
        unitNumber,
        0);
}

uint32_t
mscp_server::DetermineAccessPaths(
    uint16_t unitNumber)
{
    DEBUG_FAST("MSCP DETERMINE ACCESS PATHS drive %d", unitNumber);

    // "This command must be treated as a no-op that always succeeds
    //  if the unit is incapable of being connected to more than one
    //  controller." That's us!

    mscp_drive_c* drive = GetDrive(unitNumber); 
    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }
    else
    {
        return STATUS(Status::SUCCESS, 0, 0);
    }
}

uint32_t
mscp_server::Erase(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    return DoDiskTransfer(
        Opcodes::ERASE,
        message,
        unitNumber,
        modifiers);
}

uint32_t
mscp_server::GetCommandStatus(
    std::shared_ptr<Message> message)
{
    INFO("MSCP GET COMMAND STATUS");

    #pragma pack(push,1)
    struct GetCommandStatusResponseParameters
    {
        uint32_t OutstandingReferenceNumber;
        uint32_t CommandStatus;
    };
    #pragma pack(pop)

    message->MessageLength = sizeof(GetCommandStatusResponseParameters)
        + HEADER_SIZE;

    GetCommandStatusResponseParameters* params = 
        reinterpret_cast<GetCommandStatusResponseParameters*>(
            GetParameterPointer(message));

    //
    // This will always return zero; as with the ABORT command, at this
    // point the command being referenced has already been executed.
    //
    params->CommandStatus = 0;

    return STATUS(Status::SUCCESS, 0, 0);
}

uint32_t
mscp_server::GetUnitStatus(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    #pragma pack(push,1)
    struct GetUnitStatusResponseParameters
    {
        uint16_t MultiUnitCode;
        uint16_t UnitFlags;
        uint32_t Reserved0;
        uint32_t UnitIdDeviceNumber;
        uint16_t UnitIdUnused;
        uint16_t UnitIdClassModel;
        uint32_t MediaTypeIdentifier;
        uint16_t ShadowUnit;
        uint16_t Reserved1;
        uint16_t TrackSize;
        uint16_t GroupSize;
        uint16_t CylinderSize;
        uint16_t Reserved2;   
        uint16_t RCTSize;
        uint8_t RBNs;
        uint8_t Copies;
    };
    #pragma pack(pop)

    DEBUG_FAST("MSCP GET UNIT STATUS drive %d", unitNumber);

    // Adjust message length for response
    message->MessageLength = sizeof(GetUnitStatusResponseParameters) +
        HEADER_SIZE;

    ControlMessageHeader* header =
        reinterpret_cast<ControlMessageHeader*>(message->Message);

    if (modifiers & 0x1)
    {
        // Next Unit modifier: return the next known unit >= unitNumber.
        // Unless unitNumber is greater than the number of drives we support
        // we just return the unit specified by unitNumber.
        if (unitNumber >= _port->GetDriveCount())
        {
            // In this case we act as if drive 0 was queried.
            unitNumber = 0;
            header->UnitNumber = 0;
        }
    }

    mscp_drive_c* drive = GetDrive(unitNumber);

    GetUnitStatusResponseParameters* params = 
        reinterpret_cast<GetUnitStatusResponseParameters*>(
            GetParameterPointer(message));

    if (nullptr == drive || !drive->IsAvailable())
    {
        // No such drive or drive image not loaded.
        params->UnitIdDeviceNumber = 0;
        params->UnitIdClassModel = 0;
        params->UnitIdUnused = 0;
        params->ShadowUnit = 0;
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }

    params->Reserved0 = 0;
    params->Reserved1 = 0;
    params->Reserved2 = 0;
    params->UnitFlags = 0;  // TODO: 0 for now, which is sane.
    params->MultiUnitCode = 0; // Controller dependent, we don't support multi-unit drives.
    params->UnitIdDeviceNumber = drive->GetDeviceNumber();      
    params->UnitIdClassModel = drive->GetClassModel();
    params->UnitIdUnused = 0;
    params->MediaTypeIdentifier = drive->GetMediaID(); 
    params->ShadowUnit = unitNumber;   // Always equal to unit number
    
    // From the MSCP spec: "As stated above, the host area of  a  disk  is  structured  as  a
    //  vector of logical blocks.  From a performance viewpoint, however,
    //  it  is  more  appropriate  to  view  the  host  area  as  a  four
    //  dimensional hyper-cube."
    // This has nothing whatsoever to do with what's going on here but it makes me snicker
    // every time I read it so I'm including it.
    // Let's relay some information about our data-tesseract:
    // Since our underlying storage is an image file on flash memory, we don't need to be concerned
    // about seek times, so the below is appropriate:
    //
    params->TrackSize = 1;  
    params->GroupSize = 1;  
    params->CylinderSize = 1; 

    params->RCTSize = drive->GetRCTSize();
    params->RBNs = drive->GetRBNs();
    params->Copies = drive->GetRCTCopies();

    if (drive->IsOnline())
    {
        return STATUS(Status::SUCCESS, 0, 0);
    }
    else
    {
        return STATUS(Status::UNIT_AVAILABLE, 0, 0);
    } 
}

uint32_t
mscp_server::Online(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    #pragma pack(push,1)
    struct OnlineParameters
    {
        uint16_t UnitFlags alignas(2);
        uint16_t Reserved0 alignas(2);
        uint32_t Reserved1;
        uint32_t Reserved2;
        uint32_t Reserved3;
        uint32_t DeviceParameters;
        uint32_t Reserved4;
    };
    #pragma pack(pop)

    //
    // TODO: Right now, ignoring all incoming parameters.
    // With the exception of write-protection none of them really
    // apply.
    // We still need to flag errors if someone tries to set
    // host-settable flags we can't support.
    //

    // "The ONLINE command performs a SET UNIT CHARACTERISTICS
    // operation after bringing a unit 'Unit-Online'"
    return SetUnitCharacteristicsInternal(message, unitNumber, modifiers, true /*bring online*/);
}

uint32_t
mscp_server::Replace(
    std::shared_ptr<Message> message,
    uint16_t unitNumber)
{
    INFO("MSCP REPLACE");
    //
    // We treat this as a success for valid units as we do no block replacement at all.
    // Best just to smile and nod.  We could be more vigilant and check LBNs, etc...
    //
    message->MessageLength = HEADER_SIZE;

    mscp_drive_c* drive = GetDrive(unitNumber);

    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }
    else
    {
        return STATUS(Status::SUCCESS, 0, 0);
    }
}

uint32_t
mscp_server::SetControllerCharacteristics(
    std::shared_ptr<Message> message)
{
    #pragma pack(push,1)
    struct SetControllerCharacteristicsParameters
    {
        uint16_t MSCPVersion;    
        uint16_t ControllerFlags;
        uint16_t HostTimeout;
        uint16_t Reserved;
        union
        {
            uint64_t TimeAndDate;
            struct
            {
                uint32_t UniqueDeviceNumber;
                uint16_t Unused;
                uint16_t ClassModel;
            } ControllerId;
        } w;
    };
    #pragma pack(pop)
 
    SetControllerCharacteristicsParameters* params =
        reinterpret_cast<SetControllerCharacteristicsParameters*>(
            GetParameterPointer(message));

    DEBUG_FAST("MSCP SET CONTROLLER CHARACTERISTICS");

    // Adjust message length for response
    message->MessageLength = sizeof(SetControllerCharacteristicsParameters) +
        HEADER_SIZE;
    //
    // Check the version, if non-zero we must return an Invalid Command
    // end message.
    //
    if (params->MSCPVersion != 0)
    {
        return STATUS(Status::INVALID_COMMAND, 0, 0); // TODO: set sub-status
    }  
    else
    {
        _hostTimeout = params->HostTimeout;
        _controllerFlags = params->ControllerFlags; 

        // At this time we ignore the time and date entirely.
   
        // Prepare the response message 
        params->Reserved = 0;
        params->ControllerFlags = _controllerFlags & 0xfe;  // Mask off 576 byte sectors bit.
                                                            // it's read-only and we're a 512
                                                            // byte sector shop here. 
        params->HostTimeout = 0xff;   // Controller timeout: return the max value.
        params->w.ControllerId.UniqueDeviceNumber = _port->GetControllerIdentifier();
        params->w.ControllerId.ClassModel = _port->GetControllerClassModel();
        params->w.ControllerId.Unused = 0;

        return STATUS(Status::SUCCESS, 0, 0);
    }
     
}

uint32_t
mscp_server::SetUnitCharacteristics(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    #pragma pack(push,1)
    struct SetUnitCharacteristicsParameters
    {
        uint16_t UnitFlags;
        uint16_t Reserved0;
        uint32_t Reserved1;
        uint64_t Reserved2;
        uint32_t DeviceDependent;
        uint16_t Reserved3;
        uint16_t Reserved4;
    };
    #pragma pack(pop)

    // TODO: handle Set Write Protect modifier

    DEBUG_FAST("MSCP SET UNIT CHARACTERISTICS drive %d", unitNumber);

    return SetUnitCharacteristicsInternal(message, unitNumber, modifiers, false);
}


uint32_t
mscp_server::Read(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    return DoDiskTransfer(
        Opcodes::READ,
        message,
        unitNumber,
        modifiers);
}

uint32_t
mscp_server::Write(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    return DoDiskTransfer(
        Opcodes::WRITE,
        message,
        unitNumber,
        modifiers);
}

//
// SetUnitCharacteristicsInternal():
//  Logic common to both ONLINE and SET UNIT CHARACTERISTICS commands.
//
uint32_t
mscp_server::SetUnitCharacteristicsInternal(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers,
    bool bringOnline)
{
    UNUSED(modifiers);
    // TODO: handle Set Write Protect modifier

    #pragma pack(push,1)
    struct SetUnitCharacteristicsResponseParameters
    {
        uint16_t UnitFlags;
        uint16_t MultiUnitCode;
        uint32_t Reserved0;
        uint32_t UnitIdDeviceNumber;
        uint16_t UnitIdUnused;
        uint16_t UnitIdClassModel;
        uint32_t MediaTypeIdentifier;
        uint32_t Reserved1;
        uint32_t UnitSize;
        uint32_t VolumeSerialNumber;
    };
    #pragma pack(pop)

    // Adjust message length for response
    message->MessageLength = sizeof(SetUnitCharacteristicsResponseParameters) +
        HEADER_SIZE;

    mscp_drive_c* drive = GetDrive(unitNumber);
    // Check unit
    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }

    SetUnitCharacteristicsResponseParameters* params =
        reinterpret_cast<SetUnitCharacteristicsResponseParameters*>(
            GetParameterPointer(message));

    params->UnitFlags = 0;  // TODO: 0 for now, which is sane.
    params->MultiUnitCode = 0; // Controller dependent, we don't support multi-unit drives.
    params->UnitIdDeviceNumber = drive->GetDeviceNumber();
    params->UnitIdClassModel = drive->GetClassModel();
    params->UnitIdUnused = 0;
    params->MediaTypeIdentifier = drive->GetMediaID();
    params->UnitSize = drive->GetBlockCount();
    params->VolumeSerialNumber = 0;
    params->Reserved0 = 0;
    params->Reserved1 = 0;

    if (bringOnline)
    {
        bool alreadyOnline = drive->IsOnline();
        drive->SetOnline();
        return STATUS(Status::SUCCESS,  
            (alreadyOnline ? SuccessSubcodes::ALREADY_ONLINE : SuccessSubcodes::NORMAL), 0); 
    }
    else
    {
        return STATUS(Status::SUCCESS, 0, 0);
    }
}

//
// DoDiskTransfer():
//  Common transfer logic for READ, WRITE, ERASE, COMPARE HOST DATA and ACCCESS commands.
//
uint32_t
mscp_server::DoDiskTransfer(
    uint16_t operation,
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    #pragma pack(push,1)
    struct ReadWriteEraseParameters
    {
        uint32_t ByteCount;
        uint32_t BufferPhysicalAddress;  // upper 8 bits are channel address for VAXen
        uint32_t Unused0;
        uint32_t Unused1;
        uint32_t LBN;
    };
    #pragma pack(pop)

    ReadWriteEraseParameters* params =
        reinterpret_cast<ReadWriteEraseParameters*>(GetParameterPointer(message));

    DEBUG_FAST("MSCP RWE 0x%x unit %d mod 0x%x chan o%o pa o%o count %d lbn %d",
        operation,
        unitNumber,
        modifiers,
        params->BufferPhysicalAddress >> 24,
        params->BufferPhysicalAddress & 0x00ffffff,
        params->ByteCount,
        params->LBN);

    // Adjust message length for response
    message->MessageLength = sizeof(ReadWriteEraseParameters) +
        HEADER_SIZE;

    mscp_drive_c* drive = GetDrive(unitNumber);

    // Check unit
    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }

    if (!drive->IsOnline())
    {
        return STATUS(Status::UNIT_AVAILABLE, 0, 0);
    }

    // Are we accessing the RCT area?
    bool rctAccess = params->LBN >= drive->GetBlockCount(); 
    uint32_t rctBlockNumber = params->LBN - drive->GetBlockCount();

    // Check that the LBN is valid
    if (params->LBN >= drive->GetBlockCount() + drive->GetRCTBlockCount())
    {
        uint16_t subCode = offsetof(ReadWriteEraseParameters, LBN) + HEADER_OFFSET;
        return STATUS(Status::INVALID_COMMAND, subCode, 0);
    }

    // Check byte count:  
    if (params->ByteCount > ((drive->GetBlockCount() + drive->GetRCTBlockCount()) - params->LBN) * drive->GetBlockSize())
    {
        uint16_t subCode = offsetof(ReadWriteEraseParameters, ByteCount) + HEADER_OFFSET;
        return STATUS(Status::INVALID_COMMAND, subCode, 0);
    }

    // If this is an RCT access, byte count must equal the block size.
    if (rctAccess && params->ByteCount != drive->GetBlockSize())
    {
        uint16_t subCode = offsetof(ReadWriteEraseParameters, ByteCount) + HEADER_OFFSET;
        return STATUS(Status::INVALID_COMMAND, subCode, 0);
    }

    //
    // OK: do the transfer from the PDP-11 to a buffer
    //
    switch (operation)
    {
        case Opcodes::ACCESS:
            // We don't need to actually do any sort of transfer; ACCESS merely checks
            // That the data can be read -- we checked the LBN, etc. above and we 
            // will never encounter a read error, so there's nothing left to do.
        break;

        case Opcodes::COMPARE_HOST_DATA:
        {
            // Read the data in from disk, read the data in from memory, and compare.
            std::unique_ptr<uint8_t> diskBuffer;

            if (rctAccess)
            {
                diskBuffer.reset(drive->ReadRCTBlock(rctBlockNumber));
            }
            else
            {
                diskBuffer.reset(drive->Read(params->LBN, params->ByteCount));
            }

            std::unique_ptr<uint8_t> memBuffer(_port->DMARead(
                params->BufferPhysicalAddress & 0x00ffffff,
                params->ByteCount,
                params->ByteCount));
 
            if (!memBuffer)
            {
                return STATUS(Status::HOST_BUFFER_ACCESS_ERROR, HostBufferAccessSubcodes::NXM, 0);
            }
  
            if (!memcmp(diskBuffer.get(), memBuffer.get(), params->ByteCount))
            {
                return STATUS(Status::COMPARE_ERROR, 0, 0);
            }
        }
 
        case Opcodes::ERASE:
        {
            std::unique_ptr<uint8_t> memBuffer(new uint8_t[params->ByteCount]);
            memset(reinterpret_cast<void*>(memBuffer.get()), 0, params->ByteCount);

            if (rctAccess)
            {
                drive->WriteRCTBlock(rctBlockNumber,
                    memBuffer.get());
            }
            else
            {
                drive->Write(params->LBN,
                    params->ByteCount,
                    memBuffer.get());
            }
        } 
        break;

        case Opcodes::READ:
        {
            std::unique_ptr<uint8_t> diskBuffer;
        
            if (rctAccess)
            {
                diskBuffer.reset(drive->ReadRCTBlock(rctBlockNumber));
            }
            else
            { 
                diskBuffer.reset(drive->Read(params->LBN, params->ByteCount));
            }

            if (!_port->DMAWrite(
                params->BufferPhysicalAddress & 0x00ffffff,
                params->ByteCount,
                diskBuffer.get()))
            {
                return STATUS(Status::HOST_BUFFER_ACCESS_ERROR, HostBufferAccessSubcodes::NXM, 0);
            }

        }
        break;

        case Opcodes::WRITE:
        {
            std::unique_ptr<uint8_t> memBuffer(_port->DMARead(
                params->BufferPhysicalAddress & 0x00ffffff,
                params->ByteCount,
                params->ByteCount));

            if (!memBuffer)
            {
                return STATUS(Status::HOST_BUFFER_ACCESS_ERROR, HostBufferAccessSubcodes::NXM, 0);
            }
 
            if (rctAccess)
            {
                drive->WriteRCTBlock(rctBlockNumber,
                    memBuffer.get());
            }
            else
            {
                drive->Write(params->LBN,
                    params->ByteCount,
                    memBuffer.get());
            }
        }
        break;

        default:
            // Should never happen.
            assert(false);
            break;
    }

    // Set parameters for response.
    // We leave ByteCount as is (for now anyway)
    // And set First Bad Block to 0.  (This is unnecessary since we're
    // not reporting a bad block, but we're doing it for completeness.)
    params->LBN = 0;

    return STATUS(Status::SUCCESS, 0, 0);
}

//
// GetParameterPointer():
//  Returns a pointer to the Parameter text in the given Message.
//
uint8_t*
mscp_server::GetParameterPointer(
    std::shared_ptr<Message> message)
{
    // We silence a strict aliasing warning here; this is safe (if perhaps not recommended
    // the general case.)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
    return reinterpret_cast<ControlMessageHeader*>(message->Message)->Parameters;
#pragma GCC diagnostic pop
}

//
// GetDrive():
//  Returns the mscp_drive_c object for the specified unit number,
//  or nullptr if no such object exists.
//
mscp_drive_c*
mscp_server::GetDrive(
    uint32_t unitNumber)
{
    mscp_drive_c* drive = nullptr;
    if (unitNumber < _port->GetDriveCount())
    {
        drive = _port->GetDrive(unitNumber);
    }

    return drive;
}

//
// Reset():
//  Resets the MSCP server:
//   - Waits for the polling thread to finish its current work
//   - Releases all drives into the Available state
//
void 
mscp_server::Reset(void)
{
    DEBUG_FAST("Aborting polling due to reset.");

    pthread_mutex_lock(&polling_mutex);
    if (_pollState != PollingState::Wait)
    {
        _pollState = PollingState::InitRestart;

        while (_pollState != PollingState::Wait)
        {
            pthread_cond_wait(
                &polling_cond,
                &polling_mutex);
        }
    }  
    pthread_mutex_unlock(&polling_mutex);

    _credits = INIT_CREDITS;

    // Release all drives
    for (uint32_t i=0;i<_port->GetDriveCount();i++)
    {
        GetDrive(i)->SetOffline();
    }
}

//
// InitPolling():
//  Wakes the polling thread.
//
void 
mscp_server::InitPolling(void)
{
    //
    // Wake the polling thread if not already awoken.
    //
    pthread_mutex_lock(&polling_mutex);
        DEBUG_FAST("Waking polling thread.");
        _pollState = PollingState::InitRun;
       	pthread_cond_signal(&polling_cond);
    pthread_mutex_unlock(&polling_mutex);
}

