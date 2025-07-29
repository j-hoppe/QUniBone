/*
    mscp_server.hpp: Implementation of a simple MSCP server.

    Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
    Contributed under the BSD 2-clause license.
*/

#pragma once

#include <stdint.h>
#include <memory>

class uda_c;
class Message;
class mscp_drive_c;

// Builds a uint32_t containing the status, flags, and endcode for a response message,
// used to simplify returning the appropriate status bits from command functions.
// This looks like:
// 31           15           0
// |subcode|code|flags|unused|
// The upper 16 bits correspond to the subcode/code field returned in the MSCP end message.
//
#define STATUS(code, subcode, flags) ((flags) << 8) | ((((code) & 0x1f) | (((subcode) & 0x7ff) << 5)) << 16)

#define GET_STATUS(status) (((status) >> 16) & 0xffff)
#define GET_FLAGS(status) (((status) >> 8) & 0xff)

#define MAX_CREDITS 14
#define INIT_CREDITS 1

#define HEADER_OFFSET 4

//
// ControlMessageHeader encapsulates the standard MSCP control
// message header: a 12-byte header followed by up to 36 bytes of
// parameters.
// Note: This struct (and many others like it in this code) assumes
// little-endian byte orderings.  Probably not a big deal unless this
// someday runs on something other than a Beaglebone.
//
#pragma pack(push,1)
struct ControlMessageHeader
{
    uint32_t ReferenceNumber;
    uint16_t UnitNumber;
    uint16_t Reserved;

    union
    {
        struct
        {
            uint8_t Opcode : 8;
            uint8_t Reserved : 8;
            uint16_t Modifiers : 16;
        } Command;

        struct
        {
            uint8_t Endcode : 8;
            uint8_t Flags : 8;
            uint16_t Status : 16;
        } End;
    } Word3;

    // M9312 DU boot loader writes invalid big messages sizes.
    // Temporary patch: enlarge buffer behind all reasonable limits.
    // uint8_t Parameters[512];
    uint8_t Parameters[10240];	// overkill
};
#pragma pack(pop)

// Size in bytes of the non-parameter portion of a ControlMessageHeader
#define HEADER_SIZE  12

//
// This inherits from device_c solely so the logging macros work.
//
class mscp_server : public device_c
{
	// enums scoped to mscp_server
    enum Opcodes
    {
        ABORT = 0x1,
        ACCESS = 0x10,
        AVAILABLE = 0x8,
        COMPARE_HOST_DATA = 0x20,
        DETERMINE_ACCESS_PATHS = 0x0b,
        ERASE = 0x12,
        GET_COMMAND_STATUS = 0x2,
        GET_UNIT_STATUS = 0x3,
        ONLINE = 0x9,
        READ = 0x21,
        REPLACE = 0x14,
        SET_CONTROLLER_CHARACTERISTICS = 0x4,
        SET_UNIT_CHARACTERISTICS = 0xa,
        WRITE = 0x22
    };

    enum Endcodes
    {
        END = 0x80,
        SERIOUS_EXCEPTION = 0x7,
    };

    enum Status
    {
        SUCCESS = 0x0,
        INVALID_COMMAND = 0x1,
        COMMAND_ABORTED = 0x2,
        UNIT_OFFLINE = 0x3,
        UNIT_AVAILABLE = 0x4,
        MEDIA_FORMAT_ERROR = 0x5,
        WRITE_PROTECTED = 0x6,
        COMPARE_ERROR = 0x7,
        DATA_ERROR = 0x8,
        HOST_BUFFER_ACCESS_ERROR = 0x9,
        CONTROLLER_ERROR = 0xa,
        DRIVE_ERROR = 0xb,
        DIAGNOSTIC_MESSAGE = 0x1f
    };

    enum SuccessSubcodes
    {
        NORMAL = 0x0,
        SPIN_DOWN_IGNORED = 0x20,
        STILL_CONNECTED = 0x40,
        DUPLICATE_UNIT_NUMBER = 0x80,
        ALREADY_ONLINE = 0x100,
        STILL_ONLINE = 0x200,
    };

    enum UnitOfflineSubcodes
    {
        UNIT_UNKNOWN = 0x0,
        NO_VOLUME = 0x1,
        UNIT_INOPERATIVE = 0x2,
        DUPLICATE_UNIT = 0x4,
        UNIT_DISABLED = 0x8,
        EXCLUSIVE = 0x10,
    };

    enum HostBufferAccessSubcodes
    {
        CAUSE_UNAVAILABLE = 0x0,
        ODD_TRANSFER_ADDRESS = 0x1,
        ODD_BYTE_COUNT = 0x2,
        NXM = 0x3,
        HOST_PARITY_ERROR = 0x4,
        INVALID_PTE = 0x5,
        INVALID_BUFFER_NAME = 0x6,
        BUFFER_LENGTH_VIOLATION = 0x7,
        ACL_VIOLATION = 0x8,
    };

    enum MessageTypes
    {
        Sequential = 0,
        Datagram = 1,
        CreditNotification = 2,
        Maintenance = 15,
    };

public:



    mscp_server(uda_c *port);
    ~mscp_server();
    bool on_param_changed(parameter_c *param) override ;

public:
    void Reset(void);
    void InitPolling(void);
    void Poll(void);

public:
    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override {
        UNUSED(aclo_edge) ;
        UNUSED(dclo_edge) ;
    }
    void on_init_changed(void) override {}

private:
    uint32_t Abort(void);
    uint32_t Access(std::shared_ptr<Message> message, uint16_t unitNumber);
    uint32_t Available(uint16_t unitNumber, uint16_t modifiers);
    uint32_t CompareHostData(std::shared_ptr<Message> message, uint16_t unitNumber);
    uint32_t DetermineAccessPaths(uint16_t unitNumber);
    uint32_t Erase(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);
    uint32_t GetCommandStatus(std::shared_ptr<Message> message);
    uint32_t GetUnitStatus(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);
    uint32_t Online(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);
    uint32_t SetControllerCharacteristics(std::shared_ptr<Message> message);
    uint32_t SetUnitCharacteristics(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);
    uint32_t Read(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);
    uint32_t Replace(std::shared_ptr<Message> message, uint16_t unitNumber);
    uint32_t Write(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);

    uint32_t SetUnitCharacteristicsInternal(
        std::shared_ptr<Message> message,
        uint16_t unitNumber,
        uint16_t modifiers,
        bool bringOnline);
    uint32_t DoDiskTransfer(uint16_t operation, std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);
    uint8_t* GetParameterPointer(std::shared_ptr<Message> message);
    mscp_drive_c* GetDrive(uint32_t unitNumber);

private:
    void StartPollingThread(void);
    void AbortPollingThread(void);

private:
    uint32_t _hostTimeout;
    uint32_t _controllerFlags;

private:
    uda_c* _port;

    enum PollingState
    {
        Wait = 0,
        Run,
        InitRestart,
        InitRun
    };

    bool _abort_polling;
    PollingState _pollState;

    pthread_t polling_pthread;
    pthread_cond_t polling_cond;
    pthread_mutex_t polling_mutex;

    // Credits available
    uint8_t _credits;
};

