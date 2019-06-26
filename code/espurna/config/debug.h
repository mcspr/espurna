#pragma once

// -----------------------------------------------------------------------------
// Debug messages
// -----------------------------------------------------------------------------

#define DEBUG_SUPPORT           DEBUG_SERIAL_SUPPORT || DEBUG_UDP_SUPPORT || DEBUG_TELNET_SUPPORT || DEBUG_WEB_SUPPORT

#if DEBUG_SUPPORT
    #define DEBUG_MSG(...) debugSend(__VA_ARGS__)
    #define DEBUG_MSG_P(...) debugSend_P(__VA_ARGS__)
#endif

#ifndef DEBUG_MSG
    #define DEBUG_MSG(...)
    #define DEBUG_MSG_P(...)
#endif

// -----------------------------------------------------------------------------
// Save crash info
// -----------------------------------------------------------------------------

/**
 * Structure of the single crash data set
 *
 *  1. Crash time
 *  2. Restart reason
 *  3. Exception cause
 *  4. epc1
 *  5. epc2
 *  6. epc3
 *  7. excvaddr
 *  8. depc
 *  9. adress of stack start
 * 10. adress of stack end
 * 11. stack trace bytes
 *     ...
 */
#define SAVE_CRASH_CRASH_TIME       0x00  // 4 bytes
#define SAVE_CRASH_RESTART_REASON   0x04  // 1 byte
#define SAVE_CRASH_EXCEPTION_CAUSE  0x05  // 1 byte
#define SAVE_CRASH_EPC1             0x06  // 4 bytes
#define SAVE_CRASH_EPC2             0x0A  // 4 bytes
#define SAVE_CRASH_EPC3             0x0E  // 4 bytes
#define SAVE_CRASH_EXCVADDR         0x12  // 4 bytes
#define SAVE_CRASH_DEPC             0x16  // 4 bytes
#define SAVE_CRASH_STACK_START      0x1A  // 4 bytes
#define SAVE_CRASH_STACK_END        0x1E  // 4 bytes
#define SAVE_CRASH_STACK_SIZE       0x22  // 2 bytes
#define SAVE_CRASH_STACK_TRACE      0x24  // variable...

#ifndef SAVE_CRASH_STACK_TRACE_MAX
#define SAVE_CRASH_STACK_TRACE_MAX   0x80  // ...but limit default trace length at 128 bytes
                                           // (incremented by 16 bytes)
#endif

#ifndef SAVE_CRASH_EEPROM_OFFSET
#define SAVE_CRASH_EEPROM_OFFSET     0x0100  // initial address for crash data
#endif

#ifndef SAVE_CRASH_SUPPORT
#define SAVE_CRASH_SUPPORT           1
#endif

#ifndef SAVE_CRASH_ENABLED
#define SAVE_CRASH_ENABLED           true
#endif
