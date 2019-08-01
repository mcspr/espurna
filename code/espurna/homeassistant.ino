/*

HOME ASSISTANT MODULE

Copyright (C) 2017-2019 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if HOMEASSISTANT_SUPPORT

#include <ArduinoJson.h>
#include <queue>

bool _haEnabled = false;

// -----------------------------------------------------------------------------
// UTILS
// -----------------------------------------------------------------------------

String _haFixName(String name) {
    for (unsigned char i=0; i<name.length(); i++) {
        if (!isalnum(name.charAt(i))) name.setCharAt(i, '_');
    }
    return name;
}

// -----------------------------------------------------------------------------
// SENSORS
// -----------------------------------------------------------------------------

#if SENSOR_SUPPORT

void _haSendMagnitude(unsigned char i, JsonObject& config) {

    unsigned char type = magnitudeType(i);
    config["name"] = _haFixName(getSetting("hostname") + String(" ") + magnitudeTopic(type));
    config["platform"] = "mqtt";
    config["state_topic"] = mqttTopic(magnitudeTopicIndex(i).c_str(), false);
    config["unit_of_measurement"] = magnitudeUnits(type);
}

void _haSendMagnitudes(const JsonObject& deviceConfig) {

    for (unsigned char i=0; i<magnitudeCount(); i++) {

        String topic = getSetting("haPrefix", HOMEASSISTANT_PREFIX) +
            "/sensor/" +
            getSetting("hostname") + "_" + String(i) +
            "/config";

        String output;
        if (_haEnabled) {
            output.reserve(768);
            DynamicJsonDocument doc(512);
            JsonObject config = doc.as<JsonObject>();
            _haSendMagnitude(i, config);
            config["uniq_id"] = getIdentifier() + "_" + magnitudeTopic(magnitudeType(i)) + "_" + String(i);
            config["device"] = deviceConfig;
            
            serializeJson(config, output);
        }

        mqttSendRaw(topic.c_str(), output.c_str());
        mqttSend(MQTT_TOPIC_STATUS, MQTT_STATUS_ONLINE, true);

    }

}

#endif // SENSOR_SUPPORT

// -----------------------------------------------------------------------------
// SWITCHES & LIGHTS
// -----------------------------------------------------------------------------

void _haSendSwitch(unsigned char i, JsonObject& config) {

    String name = getSetting("hostname");
    if (relayCount() > 1) {
        name += String("_") + String(i);
    }

    config["name"] = _haFixName(name);
    config["platform"] = "mqtt";

    if (relayCount()) {
        config["state_topic"] = mqttTopic(MQTT_TOPIC_RELAY, i, false);
        config["command_topic"] = mqttTopic(MQTT_TOPIC_RELAY, i, true);
        config["payload_on"] = String(HOMEASSISTANT_PAYLOAD_ON);
        config["payload_off"] = String(HOMEASSISTANT_PAYLOAD_OFF);
        config["availability_topic"] = mqttTopic(MQTT_TOPIC_STATUS, false);
        config["payload_available"] = String(HOMEASSISTANT_PAYLOAD_AVAILABLE);
        config["payload_not_available"] = String(HOMEASSISTANT_PAYLOAD_NOT_AVAILABLE);
    }

    #if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE

        if (i == 0) {

            config["brightness_state_topic"] = mqttTopic(MQTT_TOPIC_BRIGHTNESS, false);
            config["brightness_command_topic"] = mqttTopic(MQTT_TOPIC_BRIGHTNESS, true);

            if (lightHasColor()) {
                config["rgb_state_topic"] = mqttTopic(MQTT_TOPIC_COLOR_RGB, false);
                config["rgb_command_topic"] = mqttTopic(MQTT_TOPIC_COLOR_RGB, true);
            }
            if (lightUseCCT()) {
                config["color_temp_command_topic"] = mqttTopic(MQTT_TOPIC_MIRED, true);
            }

            if (lightChannels() > 3) {
                config["white_value_state_topic"] = mqttTopic(MQTT_TOPIC_CHANNEL, 3, false);
                config["white_value_command_topic"] = mqttTopic(MQTT_TOPIC_CHANNEL, 3, true);
            }

        }

    #endif // LIGHT_PROVIDER != LIGHT_PROVIDER_NONE

}

void _haSendSwitches(const JsonObject& deviceConfig) {

    #if (LIGHT_PROVIDER != LIGHT_PROVIDER_NONE) || (defined(ITEAD_SLAMPHER))
        String type = String("light");
    #else
        String type = String("switch");
    #endif

    for (unsigned char i=0; i<relayCount(); i++) {

        String topic = getSetting("haPrefix", HOMEASSISTANT_PREFIX) +
            "/" + type +
            "/" + getSetting("hostname") + "_" + String(i) +
            "/config";

        String output;
        if (_haEnabled) {
            output.reserve(512);
            DynamicJsonDocument doc(768);
            JsonObject config = doc.as<JsonObject>();
            _haSendSwitch(i, config);
            config["uniq_id"] = getIdentifier() + "_" + type + "_" + String(i);
            config["device"] = deviceConfig;

            serializeJson(config, output);
        }

        mqttSendRaw(topic.c_str(), output.c_str());
        mqttSend(MQTT_TOPIC_STATUS, MQTT_STATUS_ONLINE, true);

    }

}

// -----------------------------------------------------------------------------

void _haDumpConfig(std::function<void(String&)> printer, bool wrapJson = false) {

    #if (LIGHT_PROVIDER != LIGHT_PROVIDER_NONE) || (defined(ITEAD_SLAMPHER))
        String type = String("light");
    #else
        String type = String("switch");
    #endif

    for (unsigned char i=0; i<relayCount(); i++) {

        DynamicJsonDocument doc(1024);
        JsonObject config = doc.as<JsonObject>();
        _haSendSwitch(i, config);

        String output;
        output.reserve(config.size() + 32);

        if (wrapJson) {
            output += "{\"haConfig\": \"";
        }

        output += "\n\n" + type + ":\n";
        bool first = true;

        for (auto kv : config) {
            if (first) {
                output += "  - ";
                first = false;
            } else {
                output += "    ";
            }
            output += kv.key().c_str();
            output += ": ";
            output += kv.value().as<char*>();
            output += '\n';
        }
        output += " ";

        if (wrapJson) {
            output += "\"}";
        }

        printer(output);

    }

    #if SENSOR_SUPPORT

        for (unsigned char i=0; i<magnitudeCount(); i++) {

            DynamicJsonDocument doc(768);
            JsonObject config = doc.as<JsonObject>();
            _haSendMagnitude(i, config);

            String output;
            output.reserve(config.size() + 32);

            if (wrapJson) {
                output += "{\"haConfig\": \"";
            }

            output += "\n\nsensor:\n";
            bool first = true;

            for (auto kv : config) {
                if (first) {
                    output += "  - ";
                    first = false;
                } else {
                    output += "    ";
                }
                String value = kv.value().as<const char*>();
                value.replace("%", "'%'");
                output += kv.key().c_str();
                output += ": ";
                output += value;
                output += "\n";
            }
            output += " ";

            if (wrapJson) {
                output += "\"}";
            }

            printer(output);

        }

    #endif
}

void _haGetDeviceConfig(JsonObject& config) {
    String identifier = getIdentifier();
    
    config.createNestedArray("identifiers").add(identifier);
    config["name"] = getSetting("desc", getSetting("hostname"));
    config["manufacturer"] = String(MANUFACTURER);
    config["model"] = String(DEVICE);
    config["sw_version"] = String(APP_NAME) + " " + String(APP_VERSION) + " (" + getCoreVersion() + ")";
}

void _haSend() {

    // Are we connected?
    if (!mqttConnected()) return;

    DEBUG_MSG_P(PSTR("[HA] Sending autodiscovery MQTT message\n"));

    // Get common device config
    DynamicJsonDocument doc(1024);
    JsonObject config = doc.as<JsonObject>();
    _haGetDeviceConfig(config);

    // Send messages
    _haSendSwitches(config);
    #if SENSOR_SUPPORT
        _haSendMagnitudes(config);
    #endif

}

void _haConfigure() {
    bool enabled = getSetting("haEnabled", HOMEASSISTANT_ENABLED).toInt() == 1;
    _haEnabled = enabled;
    _haSend();
}

#if WEB_SUPPORT

std::queue<uint32_t> _ha_send_config;

bool _haWebSocketKeyCheck(const char * key) {
    return (strncmp(key, "ha", 2) == 0);
}

void _haWebSocketOnSend(uint32_t client_id) {
    const String prefix = getSetting("haPrefix", HOMEASSISTANT_PREFIX);

    StaticJsonDocument<JSON_OBJECT_SIZE(3) + 2> root;
    root["haVisible"] = 1;
    root["haPrefix"] = prefix.c_str();
    root["haEnabled"] = getSetting("haEnabled", HOMEASSISTANT_ENABLED).toInt() == 1;

    wsSend(client_id, root);
}

void _haWebSocketOnAction(uint32_t client_id, const char * action, JsonObject& data) {
    if (strcmp(action, "haconfig") == 0) {
        _ha_send_config.push(client_id);
    }
}

#endif

#if TERMINAL_SUPPORT

void _haInitCommands() {

    terminalRegisterCommand(F("HA.CONFIG"), [](Embedis* e) {
        _haDumpConfig([](String& data) {
            DEBUG_MSG(data.c_str());
        });
        DEBUG_MSG("\n");
        terminalOK();
    });

    terminalRegisterCommand(F("HA.SEND"), [](Embedis* e) {
        setSetting("haEnabled", "1");
        _haConfigure();
        #if WEB_SUPPORT
            _haWebSocketOnSend(0);
        #endif
        terminalOK();
    });

    terminalRegisterCommand(F("HA.CLEAR"), [](Embedis* e) {
        setSetting("haEnabled", "0");
        _haConfigure();
        #if WEB_SUPPORT
            _haWebSocketOnSend(0);
        #endif
        terminalOK();
    });

}

#endif

// -----------------------------------------------------------------------------

#if WEB_SUPPORT
void _haLoop() {
    if (_ha_send_config.empty()) return;

    uint32_t client_id = _ha_send_config.front();
    _ha_send_config.pop();

    if (!wsConnected(client_id)) return;

    // TODO check wsConnected after each "printer" call?
    _haDumpConfig([client_id](String& output) {
        wsSend(client_id, output.c_str());
        yield();
    }, true);
}
#endif

void haSetup() {

    _haConfigure();

    #if WEB_SUPPORT
        wsOnSendRegister(_haWebSocketOnSend);
        wsOnActionRegister(_haWebSocketOnAction);
        wsKeyCheckRegister(_haWebSocketKeyCheck);
        espurnaRegisterLoop(_haLoop);
    #endif

    #if TERMINAL_SUPPORT
        _haInitCommands();
    #endif

    // On MQTT connect check if we have something to send
    mqttRegister([](unsigned int type, const char * topic, const char * payload) {
        if ((type == MQTT_CONNECT_EVENT) && _haEnabled) _haSend();
    });

    // Main callbacks
    espurnaRegisterReload(_haConfigure);

}

#endif // HOMEASSISTANT_SUPPORT
