// -----------------------------------------------------------------------------
// UPS Voltronic protocol Sensor
// Uses SoftwareUART library
// Contribution by Antonio Pérez <aperez@skarcha.com>
// -----------------------------------------------------------------------------

#if SENSOR_SUPPORT && UPS_VOLTRONIC_SUPPORT

#pragma once

#include "Arduino.h"
#include "BaseSensor.h"

#include <SoftwareUART.h>

// Generic data
#define VOLTRONIC_BAUD_RATE 2400

#define VOLTRONIC_SLOT_MAX 15

const static struct {
    const char *name;
    unsigned char type;
} slot_spec[VOLTRONIC_SLOT_MAX] = {
    {"Input voltage", MAGNITUDE_VOLTAGE},
    {"Input fault voltage", MAGNITUDE_VOLTAGE},
    {"Output voltage", MAGNITUDE_VOLTAGE},
    {"Output load level", MAGNITUDE_LOAD},
    {"Output frequency", MAGNITUDE_FREQUENCY},
    {"Battery voltage", MAGNITUDE_VOLTAGE},
    {"Internal Temperature", MAGNITUDE_TEMPERATURE},
    {"Utility fail", MAGNITUDE_DIGITAL},
    {"Battery Low", MAGNITUDE_DIGITAL},
    {"Boost or buck mode", MAGNITUDE_DIGITAL},
    {"UPS Fault", MAGNITUDE_DIGITAL},
    {"Type line-interactive or on-line", MAGNITUDE_DIGITAL},
    {"Self-test in progress", MAGNITUDE_DIGITAL},
    {"Shutdown active status", MAGNITUDE_DIGITAL},
    {"Beeper is active", MAGNITUDE_DIGITAL}
};

class UPSVoltronicSensor : public BaseSensor {

    public:
        // ---------------------------------------------------------------------
        // Public
        // ---------------------------------------------------------------------

        UPSVoltronicSensor(): BaseSensor() {
            _count = VOLTRONIC_SLOT_MAX;
            _sensor_id = SENSOR_UPS_VOLTRONIC_ID;
        }

        ~UPSVoltronicSensor() {
            if (_serial && _soft) delete static_cast<SoftwareUART*>(_serial);
        }

        void setRX(unsigned char pin_rx) {
            if (_pin_rx == pin_rx) return;
            _pin_rx = pin_rx;
            _dirty = true;
        }

        void setTX(unsigned char pin_tx) {
            if (_pin_tx == pin_tx) return;
            _pin_tx = pin_tx;
            _dirty = true;
        }

        void setSerial(HardwareSerial * serial) {
            _soft = false;
            _serial = serial;
            _dirty = true;
        }

        // ---------------------------------------------------------------------

        unsigned char getRX() {
            return _pin_rx;
        }

        unsigned char getTX() {
            return _pin_tx;
        }

        // ---------------------------------------------------------------------
        // Sensor API
        // ---------------------------------------------------------------------

        // Initialization method, must be idempotent
        void begin() {

            if (!_dirty) return;

            if (_soft) {
                if (_serial) delete static_cast<SoftwareUART*>(_serial);
                _serial = new SoftwareUART(_pin_rx, _pin_tx, false, 64);
            }

            if (_soft) {
                static_cast<SoftwareUART*>(_serial)->begin(VOLTRONIC_BAUD_RATE);
            } else {
                static_cast<HardwareSerial*>(_serial)->begin(VOLTRONIC_BAUD_RATE);
            }

            _serial->setTimeout(50);

            _ready = true;
            _dirty = false;
        }

        // Descriptive name of the sensor
        String description() {
            char buffer[38];
            if (_soft) {
                snprintf(buffer, sizeof(buffer), "UPS Voltronic @ SwSerial(%u,%u)", _pin_rx, _pin_tx);
            } else {
                strncpy(buffer, "UPS Voltronic @ HwSerial", sizeof(buffer));
            }

            return String(buffer);
        }

        // Descriptive name of the slot # index
        String slot(unsigned char index) {
            char buffer[67] = {0};
            if (_soft) {
                snprintf(buffer, sizeof(buffer), "%s @ UPS Voltronic @ SwSerial(%u,%u)", slot_spec[index].name, _pin_rx, _pin_tx);
            } else {
                snprintf(buffer, sizeof(buffer), "%s @ UPS Voltronic @ HwSerial", slot_spec[index].name);
            }
            return String(buffer);
        }

        // Address of the sensor (it could be the GPIO or I2C address)
        String address(unsigned char index) {
            char buffer[6];
            snprintf(buffer, sizeof(buffer), "%u:%u", _pin_rx, _pin_tx);
            return String(buffer);
        }

        // Type for slot # index
        unsigned char type(unsigned char index) {
            return slot_spec[index].type;
        }

        void pre() {
            _requestStatus();
        }

        // Current value for slot # index
        double value(unsigned char index) {
            return _slot_values[index];
        }

    protected:

        // ---------------------------------------------------------------------
        // Protected
        // ---------------------------------------------------------------------

        void _requestStatus() {
            String response;

            _serial->print("QS\r");
            _serial->flush();

            response = _readResponse(46);

            if (_error != SENSOR_ERROR_OK) {
                return;
            }

            _parseQueryStatusResponse(response);
        }

        void _parseQueryStatusResponse(String response) {
            _slot_values[0] = (double)response.substring(1,6).toFloat();
            _slot_values[1] = (double)response.substring(7,12).toFloat();
            _slot_values[2] = (double)response.substring(13,18).toFloat();
            _slot_values[3] = (double)response.substring(19,22).toFloat();
            _slot_values[4] = (double)response.substring(23,27).toFloat();
            _slot_values[5] = (double)response.substring(28,32).toFloat();
            _slot_values[6] = (double)response.substring(34,37).toFloat();
            _slot_values[7] = (double)response.substring(38,39).toFloat();
            _slot_values[8] = (double)response.substring(39,40).toFloat();
            _slot_values[9] = (double)response.substring(40,41).toFloat();
            _slot_values[10] = (double)response.substring(41,42).toFloat();
            _slot_values[11] = (double)response.substring(42,43).toFloat();
            _slot_values[12] = (double)response.substring(43,44).toFloat();
            _slot_values[13] = (double)response.substring(44,45).toFloat();
            _slot_values[14] = (double)response.substring(45).toFloat();
        }

        String _readResponse(size_t count) {
            String response;

            if (_serial->available()) {
                response = _serial->readStringUntil('\r');
            }

            if (response.length() < count || response.charAt(0) != '(') {
                _error = SENSOR_ERROR_TIMEOUT;
            } else {
                _error = SENSOR_ERROR_OK;
            }

            return response;
        }

        Stream * _serial = NULL;
        bool _soft = true;
        unsigned int _pin_rx;
        unsigned int _pin_tx;
        double _slot_values[VOLTRONIC_SLOT_MAX] = {0};
};

#endif // SENSOR_SUPPORT && UPS_VOLTRONIC_SUPPORT
