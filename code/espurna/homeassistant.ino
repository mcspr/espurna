/*

HOME ASSISTANT MODULE

Copyright (C) 2017-2019 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if HOMEASSISTANT_SUPPORT

bool _haEnabled = false;
bool _haSendFlag = false;

// -----------------------------------------------------------------------------
// UTILS
// -----------------------------------------------------------------------------

// per yaml 1.1 spec, following scalars are converted to bool. we want the string, so quoting the output
// y|Y|yes|Yes|YES|n|N|no|No|NO |true|True|TRUE|false|False|FALSE |on|On|ON|off|Off|OFF
String _haFixPayload(const String& value) {
    if (value.equalsIgnoreCase("y")
        || value.equalsIgnoreCase("n")
        || value.equalsIgnoreCase("yes")
        || value.equalsIgnoreCase("no")
        || value.equalsIgnoreCase("true")
        || value.equalsIgnoreCase("false")
        || value.equalsIgnoreCase("on")
        || value.equalsIgnoreCase("off")
    ) {
        String temp;
        temp.reserve(value.length() + 2);
        temp = "\"";
        temp += value;
        temp += "\"";
        return temp;
    }
    return value;
}

String& _haFixName(String& name) {
    for (unsigned char i=0; i<name.length(); i++) {
        if (!isalnum(name.charAt(i))) name.setCharAt(i, '_');
    }
    return name;
}

#if (LIGHT_PROVIDER != LIGHT_PROVIDER_NONE) || (defined(ITEAD_SLAMPHER))
const String switchType("light");
#else
const String switchType("switch");
#endif

struct ha_config_t {

    static const size_t DEFAULT_BUFFER_SIZE = 1024;
    static const size_t MINIMUM_BUFFER_SIZE = JSON_OBJECT_SIZE(4);

    ha_config_t(size_t size) :
        jsonBuffer(size),
        deviceConfig(jsonBuffer.createObject()),
        root(jsonBuffer.createObject()),
        switch_root(jsonBuffer.createObject()),
        #if SENSOR_SUPPORT
            sensor_root(jsonBuffer.createObject()),
        #endif
        identifier(getIdentifier()),
        name(getSetting("desc", getSetting("hostname"))),
        version(String(APP_NAME " " APP_VERSION " (") + getCoreVersion() + ")")
    {
        deviceConfig.createNestedArray("identifiers").add(identifier.c_str());
        deviceConfig["name"] = name.c_str();
        deviceConfig["sw_version"] = version.c_str();
        deviceConfig["manufacturer"] = MANUFACTURER;
        deviceConfig["model"] = DEVICE;
    }


    ha_container_t& loadSwitch(unsigned char id);

    #if SENSOR_SUPPORT
        ha_container_t& loadMagnitude(unsigned char id);
    #endif // SENSOR_SUPPORT

    ha_config_t() : ha_config_t(DEFAULT_BUFFER_SIZE) {}

    size_t size() { return jsonBuffer.size(); }

    DynamicJsonBuffer jsonBuffer;
    JsonObject& deviceConfig;
    JsonObject& root;

    JsonObject& switch_root;
    ha_container_t switches;

    #if SENSOR_SUPPORT
        JsonObject& sensor_root;
        ha_container_t sensors;
    #endif

    const String identifier;
    const String name;
    const String version;
};

void _haSendContainer(ha_container_t& source, JsonObject& target) {
    for (const auto &kv : source) {
        if (!kv.second.length()) continue;
        target[kv.first.c_str()] = kv.second.c_str();
    }
}

ha_container_t& ha_config_t::loadSwitch(unsigned char i) {

    switches["uniq_id"] = getIdentifier() + "_" + switchType + "_" + String(i);

    String name = getSetting("hostname");
    if (relayCount() > 1) {
        name += '_';
        name += int(i);
    }
    switches["name"] = _haFixName(name);

    if (relayCount()) {
        switches["state_topic"] =  mqttTopic(MQTT_TOPIC_RELAY, i, false);
        switches["command_topic"] = mqttTopic(MQTT_TOPIC_RELAY, i, true);
        switches["payload_on"] = relayPayload(RelayStatus::ON);
        switches["payload_off"] = relayPayload(RelayStatus::OFF);
        switches["availability_topic"] = mqttTopic(MQTT_TOPIC_STATUS, false);
        switches["payload_available"] = mqttPayloadStatus(true);
        switches["payload_not_available"] = mqttPayloadStatus(false);
    }

    #if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE

        if (i == 0) {

            switches["brightness_state_topic"] = mqttTopic(MQTT_TOPIC_BRIGHTNESS, false);
            switches["brightness_command_topic"] = mqttTopic(MQTT_TOPIC_BRIGHTNESS, true);

            if (lightHasColor()) {
                switches["rgb_state_topic"] = mqttTopic(MQTT_TOPIC_COLOR_RGB, false);
                switches["rgb_command_topic"] = mqttTopic(MQTT_TOPIC_COLOR_RGB, true);
            }

            if (lightUseCCT()) {
                switches["color_temp_state_topic"] = mqttTopic(MQTT_TOPIC_MIRED, false);
                switches["color_temp_command_topic"] = mqttTopic(MQTT_TOPIC_MIRED, true);
            }

            if (lightChannels() > 3) {
                switches["white_value_state_topic"] = mqttTopic(MQTT_TOPIC_CHANNEL, 3, false);
                switches["white_value_command_topic"] = mqttTopic(MQTT_TOPIC_CHANNEL, 3, true);
            }

        }

    #endif // LIGHT_PROVIDER != LIGHT_PROVIDER_NONE

    return switches;

}

// -----------------------------------------------------------------------------
// SENSORS
// -----------------------------------------------------------------------------

#if SENSOR_SUPPORT

ha_container_t& ha_config_t::loadMagnitude(unsigned char id) {
    const auto type = magnitudeType(id);

    sensors["uniq_id"] = getIdentifier() + "_" + magnitudeTopic(magnitudeType(id)) + "_" + String(id);
    sensors["name"] = _haFixName(getSetting("hostname") + String(" ") + magnitudeTopic(type));
    sensors["state_topic"] = mqttTopic(magnitudeTopicIndex(id).c_str(), false);
    sensors["unit"] = magnitudeUnits(type);

    return sensors;
}

void _haSendMagnitudes(ha_config_t& config) {

    for (unsigned char i=0; i<magnitudeCount(); i++) {

        const String topic = getSetting("haPrefix", HOMEASSISTANT_PREFIX) +
            "/sensor/" +
            getSetting("hostname") + "_" + String(i) +
            "/config";

        String output;
        if (_haEnabled) {
            JsonObject& root = config.sensor_root;
            _haSendContainer(config.loadMagnitude(i), root);
            root["device"] = config.deviceConfig;
            
            output.reserve(root.measureLength());
            root.printTo(output);
        }

        mqttSendRaw(topic.c_str(), output.c_str());

    }

    mqttSendStatus();

}

#endif // SENSOR_SUPPORT

// -----------------------------------------------------------------------------
// SWITCHES & LIGHTS
// -----------------------------------------------------------------------------

void _haSendSwitches(ha_config_t& config) {

    for (unsigned char i=0; i<relayCount(); i++) {

        const String topic = getSetting("haPrefix", HOMEASSISTANT_PREFIX) +
            "/" + switchType +
            "/" + getSetting("hostname") + "_" + String(i) +
            "/config";

        String output;
        if (_haEnabled) {
            JsonObject& root = config.switch_root;

            _haSendContainer(config.loadSwitch(i), root);
            root["device"] = config.deviceConfig;

            output.reserve(root.measureLength());
            root.printTo(output);
        }

        mqttSendRaw(topic.c_str(), output.c_str());
    }

    mqttSendStatus();

}

// -----------------------------------------------------------------------------

constexpr const size_t HA_YAML_BUFFER_SIZE = 1024;

String _haYaml(std::shared_ptr<ha_config_t> ha_config, const String& prefix, const ha_container_t& data, unsigned char index, JsonObject& root) {

    String output;
    output.reserve(HA_YAML_BUFFER_SIZE);

    if (index == 0) output += "\n\n" + prefix + ":";
    output += "\n";
    bool first = true;

    for (const auto &kv : data) {
        if (!kv.second.length()) continue;
        if (first) {
            output += "  - platform: mqtt\n";
            output += "    ";
            first = false;
        } else {
            output += "    ";
        }
        output += kv.first;
        output += ": ";
        if (kv.first.startsWith("payload_")) {
            output += _haFixPayload(kv.second);
        #if SENSOR_SUPPORT
            } else if (kv.first.equals("unit")) {
                String value = kv.second;
                value.replace("%", "'%'");
                output += value;
        #endif // SENSOR_SUPPORT
        } else {
            output += kv.second;
        }
        output += "\n";
    }
    output += " ";

    return output;
}

String _haSwitchYaml(std::shared_ptr<ha_config_t> ha_config, unsigned char index, JsonObject& root) {
    return _haYaml(ha_config, switchType, ha_config->loadSwitch(index), index, root);
}

#if SENSOR_SUPPORT

String _haSensorYaml(std::shared_ptr<ha_config_t> ha_config, unsigned char index, JsonObject& root) {
    return _haYaml(ha_config, "sensor", ha_config->loadMagnitude(index), index, root);
}

#endif // SENSOR_SUPPORT

void _haSend() {

    // Pending message to send?
    if (!_haSendFlag) return;

    // Are we connected?
    if (!mqttConnected()) return;

    DEBUG_MSG_P(PSTR("[HA] Sending autodiscovery MQTT message\n"));

    // Get common device config
    ha_config_t config;

    // Send messages
    _haSendSwitches(config);
    #if SENSOR_SUPPORT
        _haSendMagnitudes(config);
    #endif
    
    _haSendFlag = false;

}

void _haConfigure() {
    bool enabled = getSetting("haEnabled", HOMEASSISTANT_ENABLED).toInt() == 1;
    _haSendFlag = (enabled != _haEnabled);
    _haEnabled = enabled;
    _haSend();
}

#if WEB_SUPPORT

bool _haWebSocketOnKeyCheck(const char * key, JsonVariant& value) {
    return (strncmp(key, "ha", 2) == 0);
}

void _haWebSocketOnVisible(JsonObject& root) {
    root["haVisible"] = 1;
}

void _haWebSocketOnConnected(JsonObject& root) {
    root["haPrefix"] = getSetting("haPrefix", HOMEASSISTANT_PREFIX);
    root["haEnabled"] = getSetting("haEnabled", HOMEASSISTANT_ENABLED).toInt() == 1;
}

void _haWebSocketOnAction(uint32_t client_id, const char * action, JsonObject& data) {
    if (strcmp(action, "haconfig") == 0) {
        ws_on_send_callback_list_t callbacks;
        #if SENSOR_SUPPORT
            callbacks.reserve(magnitudeCount() + relayCount());
        #else
            callbacks.reserve(relayCount());
        #endif // SENSOR_SUPPORT
        if (!callbacks.capacity()) return;

        auto ha_config = std::make_shared<ha_config_t>(ha_config_t::MINIMUM_BUFFER_SIZE);
        auto buffer = std::make_shared<String>();
        buffer->reserve(HA_YAML_BUFFER_SIZE);

        {
            for (unsigned char idx=0; idx<relayCount(); ++idx) {
                callbacks.push_back([idx, ha_config, buffer](JsonObject& root) {
                    *buffer.get() = _haSwitchYaml(ha_config, idx, root);
                    root["haConfig"] = buffer->c_str();
                });
            }
        }
        #if SENSOR_SUPPORT
        {
            for (unsigned char idx=0; idx<magnitudeCount(); ++idx) {
                callbacks.push_back([idx, ha_config, buffer](JsonObject& root) {
                    *buffer.get() = _haSensorYaml(ha_config, idx, root);
                    root["haConfig"] = buffer->c_str();
                });
            }
        }
        #endif // SENSOR_SUPPORT
        if (callbacks.size()) wsPostSequence(client_id, std::move(callbacks));
    }
}

#endif // WEB_SUPPORT

#if TERMINAL_SUPPORT

void _haInitCommands() {

    terminalRegisterCommand(F("HA.CONFIG"), [](Embedis* e) {
        auto ha_config = std::make_shared<ha_config_t>();
        for (unsigned char idx=0; idx<relayCount(); ++idx) {
            DEBUG_MSG(_haSwitchYaml(ha_config, idx, ha_config->switch_root).c_str());
        }
        #if SENSOR_SUPPORT
            for (unsigned char idx=0; idx<magnitudeCount(); ++idx) {
                DEBUG_MSG(_haSensorYaml(ha_config, idx, ha_config->sensor_root).c_str());
            }
        #endif // SENSOR_SUPPORT
        DEBUG_MSG("\n");
        terminalOK();
    });

    terminalRegisterCommand(F("HA.SEND"), [](Embedis* e) {
        setSetting("haEnabled", "1");
        _haConfigure();
        #if WEB_SUPPORT
            wsPost(_haWebSocketOnConnected);
        #endif
        terminalOK();
    });

    terminalRegisterCommand(F("HA.CLEAR"), [](Embedis* e) {
        setSetting("haEnabled", "0");
        _haConfigure();
        #if WEB_SUPPORT
            wsPost(_haWebSocketOnConnected);
        #endif
        terminalOK();
    });

}

#endif

// -----------------------------------------------------------------------------

void haSetup() {

    _haConfigure();

    #if WEB_SUPPORT
        wsRegister()
            .onVisible(_haWebSocketOnVisible)
            .onConnected(_haWebSocketOnConnected)
            .onAction(_haWebSocketOnAction)
            .onKeyCheck(_haWebSocketOnKeyCheck);
    #endif

    #if TERMINAL_SUPPORT
        _haInitCommands();
    #endif

    // On MQTT connect check if we have something to send
    mqttRegister([](unsigned int type, const char * topic, const char * payload) {
        if (type == MQTT_CONNECT_EVENT) _haSend();
        if (type == MQTT_DISCONNECT_EVENT) _haSendFlag = _haEnabled;
    });

    // Main callbacks
    espurnaRegisterReload(_haConfigure);

}

#endif // HOMEASSISTANT_SUPPORT
