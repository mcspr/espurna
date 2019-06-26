// -----------------------------------------------------------------------------
// Save crash info
// Taken from krzychb EspSaveCrash
// https://github.com/krzychb/EspSaveCrash
// -----------------------------------------------------------------------------

#include <stdio.h>
#include <stdarg.h>
#include <EEPROM_Rotate.h>

#include "libs/PrintWrap.h"

extern "C" {
    #include "user_interface.h"
}

/**
 * Save crash information in EEPROM
 * This function is called automatically if ESP8266 suffers an exception
 * It should be kept quick / consise to be able to execute before hardware wdt may kick in
 * This method assumes EEPROM has already been initialized, which is the first thing ESPurna does
 */

uint16_t _save_crash_stack_trace_max = SAVE_CRASH_STACK_TRACE_MAX;
bool _save_crash_enabled = SAVE_CRASH_ENABLED;

#if SAVE_CRASH_SUPPORT
extern "C" void custom_crash_callback(struct rst_info * rst_info, uint32_t stack_start, uint32_t stack_end ) {

    // Do not record crash data when resetting the board
    if (checkNeedsReset()) {
        return;
    }

    // Check if runtime setting disabled this callback
    if (!_save_crash_enabled) {
        return;
    }

    // write crash time to EEPROM, which we will later use as a marker that there was a crash
    uint32_t crash_time = millis();
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_CRASH_TIME, crash_time);

    // rst_info::reason and ::exccause are uint32_t, but are holding small values
    EEPROMr.write(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_RESTART_REASON, rst_info->reason);
    EEPROMr.write(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EXCEPTION_CAUSE, rst_info->exccause);

    // write epc1, epc2, epc3, excvaddr and depc to EEPROM as uint32_t
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC1, rst_info->epc1);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC2, rst_info->epc2);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC3, rst_info->epc3);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EXCVADDR, rst_info->excvaddr);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_DEPC, rst_info->depc);

    // EEPROM size is limited, write as little as possible.
    // we sometimes want to avoid big stack traces, e.g. if stack_end == 0x3fffffb0, we are in SYS context.
    // but still should get enough relevant info and it is possible to set needed size at build/runtime
    const uint16_t stack_size = constrain((stack_end - stack_start), 0, _save_crash_stack_trace_max);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_START, stack_start);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_END, stack_end);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_SIZE, stack_size);

    // starting EEPROM address of Embedis data plus reserve
    const uint16_t settings_start = (
        ((SPI_FLASH_SEC_SIZE - settingsSize() + 31) & -32) - 0x20);

    // write stack trace to EEPROM and avoid overwriting settings
    int16_t eeprom_addr = SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_TRACE;
    for (uint32_t* addr = (uint32_t*)stack_start; addr < (uint32_t*)(stack_start + stack_size); addr++) {
        if (eeprom_addr >= settings_start) break;
        EEPROMr.put(eeprom_addr, *addr);
        eeprom_addr += sizeof(uint32_t);
    }

    EEPROMr.commit();

}
#endif

/**
 * Clears crash info
 */
void crashClear() {
    uint32_t crash_time = 0xFFFFFFFF;
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_CRASH_TIME, crash_time);
    EEPROMr.commit();
}

/**
 * Print out crash information that has been previusly saved in EEPROM
 */

#if defined(ARDUINO_ESP8266_RELEASE_2_3_0)
void crashDump(Print& orig_printable) {
    PrintWrap printable(orig_printable);
#else
void crashDump(Print& printable) {
#endif

    uint32_t crash_time;
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_CRASH_TIME, crash_time);
    if ((crash_time == 0) || (crash_time == 0xFFFFFFFF)) {
        printable.printf_P(PSTR("[DEBUG] No crash info\n"));
        return;
    }

    printable.printf_P(PSTR("[DEBUG] Latest crash was at %lu ms after boot\n\n"), crash_time);
    printable.printf_P(PSTR("Reason of restart: %u\n"), EEPROMr.read(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_RESTART_REASON));
    printable.printf_P(PSTR("Exception cause: %u\n\n"), EEPROMr.read(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EXCEPTION_CAUSE));

    uint32_t epc1, epc2, epc3, excvaddr, depc;
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC1, epc1);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC2, epc2);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC3, epc3);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EXCVADDR, excvaddr);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_DEPC, depc);

    printable.printf_P(PSTR("epc1=0x%08x epc2=0x%08x epc3=0x%08x\n"), epc1, epc2, epc3);
    printable.printf_P(PSTR("excvaddr=0x%08x depc=0x%08x\n\n"), excvaddr, depc);

    uint32_t stack_start, stack_end;
    uint16_t stack_size;

    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_START, stack_start);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_END, stack_end);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_SIZE, stack_size);

    printable.printf_P(PSTR("sp=0x%08x end=0x%08x saved=0x%04x\n\n"), stack_start, stack_end, stack_size);
    if (0xFFFF == stack_size) return;

    int16_t current_address = SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_TRACE;

    uint32_t stack_trace;

    printable.printf_P(PSTR(">>>stack>>>\n\n"));

    for (int16_t i = 0; i < stack_size; i += 0x10) {
        printable.printf_P(PSTR("%08x: "), stack_start + i);
        for (byte j = 0; j < 4; j++) {
            EEPROMr.get(current_address, stack_trace);
            printable.printf_P(PSTR("%08x "), stack_trace);
            current_address += 4;
        }
        printable.printf_P(PSTR("\n"));
    }
    printable.printf_P(PSTR("\n<<<stack<<<\n\n"));

}

void crashSetup() {

    #if TERMINAL_SUPPORT
        terminalRegisterCommand(F("CRASH"), [](Embedis* e) {
            infoBoot();
            {
                DebugPrinter printer;
                printer.reserve(256);
                crashDump(printer);
            }
            crashClear();
            terminalOK();
        });
    #endif

    // Minumum of 16 and align for column formatter in crashDump()
    _save_crash_stack_trace_max = getSetting("sysScTraceMax", SAVE_CRASH_STACK_TRACE_MAX).toInt();
    _save_crash_stack_trace_max = (_save_crash_stack_trace_max + 15) & -16;
    setSetting("sysScTraceMax", _save_crash_stack_trace_max);

    _save_crash_enabled = getSetting("sysScEnabled", SAVE_CRASH_ENABLED).toInt() == 1;

}
