#pragma once

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------

#define DEBUG_SUPPORT           DEBUG_SERIAL_SUPPORT || DEBUG_UDP_SUPPORT || DEBUG_TELNET_SUPPORT || DEBUG_WEB_SUPPORT


#if DEBUG_SUPPORT
    #include "../libs/Debug.h"
#else
    #define DEBUG_MSG(...) do { (void)0; } while (0)
    #define DEBUG_MSG_P(...) do { (void)0; } while (0)
#endif
