/*

HOME ASSISTANT MODULE

Copyright (C) 2017-2019 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if HOMEASSISTANT_SUPPORT

#include <ArduinoJson.h>

#if SENSOR_SUPPORT
    struct ha_sensor_t {
        String uniq_id;
        String name;
        String state_topic;
        String unit;
    };
#endif

struct ha_switch_t {
    String name;
    String uniq_id;
    String state_topic;
    String command_topic;
    String payload_on;
    String payload_off;
    String availability_topic;
    String payload_available;
    String payload_not_available;

    #if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
        String brightness_state_topic;
        String brightness_command_topic;

        String rgb_state_topic;
        String rgb_command_topic;
        String color_temp_command_topic;
        String color_temp_state_topic;

        String white_value_state_topic;
        String white_value_command_topic;
    #endif // LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
};

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

    static const size_t DEFAULT_BUFFER_SIZE = 2048;

    ha_config_t(size_t size) :
        jsonBuffer(size),
        deviceConfig(jsonBuffer.createObject()),
        root(jsonBuffer.createObject()),
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

    JsonObject& get(const char* key) {
        if (!root.containsKey(key)) {
            return root.createNestedObject(key);
        }

        return root[key];
    }

    #if SENSOR_SUPPORT
        const ha_sensor_t& loadMagnitude(unsigned char id);
    #endif // SENSOR_SUPPORT

    const ha_switch_t& loadSwitch(unsigned char id);

    ha_config_t() : ha_config_t(DEFAULT_BUFFER_SIZE) {}

    size_t size() { return jsonBuffer.size(); }

    DynamicJsonBuffer jsonBuffer;
    JsonObject& deviceConfig;
    JsonObject& root;

    ha_switch_t switch_buffer;
    #if SENSOR_SUPPORT
        ha_sensor_t sensor_buffer;
    #endif

    const String identifier;
    const String name;
    const String version;
};

const ha_switch_t& ha_config_t::loadSwitch(unsigned char i) {

    switch_buffer.uniq_id = getIdentifier() + "_" + switchType + "_" + String(i);

    String name = getSetting("hostname");
    if (relayCount() > 1) {
        name += String("_") + String(i);
    }

    switch_buffer.name = _haFixName(name);

    if (relayCount()) {
        switch_buffer.state_topic = mqttTopic(MQTT_TOPIC_RELAY, i, false);
        switch_buffer.command_topic = mqttTopic(MQTT_TOPIC_RELAY, i, true);
        switch_buffer.payload_on = relayPayload(RelayStatus::ON);
        switch_buffer.payload_off = relayPayload(RelayStatus::OFF);
        switch_buffer.availability_topic = mqttTopic(MQTT_TOPIC_STATUS, false);
        switch_buffer.payload_available = mqttPayloadStatus(true);
        switch_buffer.payload_not_available = mqttPayloadStatus(false);
    }

    #if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE

        if (i == 0) {

            switch_buffer.brightness_state_topic = mqttTopic(MQTT_TOPIC_BRIGHTNESS, false);
            switch_buffer.brightness_command_topic = mqttTopic(MQTT_TOPIC_BRIGHTNESS, true);

            if (lightHasColor()) {
                switch_buffer.rgb_state_topic = mqttTopic(MQTT_TOPIC_COLOR_RGB, false);
                switch_buffer.rgb_command_topic = mqttTopic(MQTT_TOPIC_COLOR_RGB, true);
            }
            if (lightUseCCT()) {
                switch_buffer.color_temp_command_topic = mqttTopic(MQTT_TOPIC_MIRED, true);
                switch_buffer.color_temp_state_topic = mqttTopic(MQTT_TOPIC_MIRED, false);
            }

            if (lightChannels() > 3) {
                switch_buffer.white_value_state_topic = mqttTopic(MQTT_TOPIC_CHANNEL, 3, false);
                switch_buffer.white_value_command_topic = mqttTopic(MQTT_TOPIC_CHANNEL, 3, true);
            }

        }

    #endif // LIGHT_PROVIDER != LIGHT_PROVIDER_NONE

    return switch_buffer;

}

// -----------------------------------------------------------------------------
// SENSORS
// -----------------------------------------------------------------------------

#if SENSOR_SUPPORT

const ha_sensor_t& ha_config_t::loadMagnitude(unsigned char id) {
    unsigned char type = magnitudeType(id);
    sensor_buffer.uniq_id = getIdentifier() + "_" + magnitudeTopic(magnitudeType(id)) + "_" + String(id);
    sensor_buffer.name = _haFixName(getSetting("hostname") + String(" ") + magnitudeTopic(type));
    sensor_buffer.state_topic = mqttTopic(magnitudeTopicIndex(id).c_str(), false);
    sensor_buffer.unit = magnitudeUnits(type);
    return sensor_buffer;
}

void _haSendMagnitude(const ha_sensor_t& sensor, JsonObject& config) {
    config["name"] = sensor.name.c_str();
    config["state_topic"] = sensor.state_topic.c_str();
    config["unit_of_measurement"] = sensor.unit.c_str();
}

void _haSendMagnitudes(ha_config_t& config) {

    for (unsigned char i=0; i<magnitudeCount(); i++) {

        String topic = getSetting("haPrefix", HOMEASSISTANT_PREFIX) +
            "/sensor/" +
            getSetting("hostname") + "_" + String(i) +
            "/config";

        String output;
        if (_haEnabled) {
            JsonObject& root = config.get("sensor");
            _haSendMagnitude(config.loadMagnitude(i), root);
            root["uniq_id"] = config.sensor_buffer.uniq_id.c_str();
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

void _haSendSwitch(const ha_switch_t& switch_, JsonObject& config) {

    config["state_topic"] = switch_.state_topic.c_str();
    config["command_topic"] = switch_.command_topic.c_str();
    config["payload_on"] = switch_.payload_on.c_str();
    config["payload_off"] = switch_.payload_off.c_str();
    config["availability_topic"] = switch_.availability_topic.c_str();
    config["payload_available"] = switch_.payload_available.c_str();
    config["payload_not_available"] = switch_.payload_not_available.c_str();

    #if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE

        if (switch_.brightness_state_topic.length()) {

            config["brightness_state_topic"] = switch_.brightness_state_topic.c_str();
            config["brightness_command_topic"] = switch_.brightness_command_topic.c_str();

            if (lightHasColor()) {
                config["rgb_state_topic"] = switch_.rgb_state_topic.c_str();
                config["rgb_command_topic"] = switch_.rgb_command_topic.c_str();
            }
            if (lightUseCCT()) {
                config["color_temp_command_topic"] = switch_.color_temp_command_topic.c_str();
                config["color_temp_state_topic"] = switch_.color_temp_state_topic.c_str();
            }

            if (lightChannels() > 3) {
                config["white_value_state_topic"] = switch_.white_value_state_topic.c_str();
                config["white_value_command_topic"] = switch_.white_value_command_topic.c_str();
            }

        }

    #endif // LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
}

void _haSendSwitches(ha_config_t& config) {

    for (unsigned char i=0; i<relayCount(); i++) {

        String topic = getSetting("haPrefix", HOMEASSISTANT_PREFIX) +
            "/" + switchType +
            "/" + getSetting("hostname") + "_" + String(i) +
            "/config";

        String output;
        if (_haEnabled) {
            JsonObject& root = config.get("switch");

            _haSendSwitch(config.loadSwitch(i), root);
            root["uniq_id"] = config.switch_buffer.uniq_id.c_str();
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

void _haSwitchYaml(std::shared_ptr<ha_config_t> ha_config, unsigned char index, JsonObject& root) {

    String output;
    output.reserve(HA_YAML_BUFFER_SIZE);

    JsonObject& config = ha_config->get("switch");
    _haSendSwitch(ha_config->loadSwitch(index), config);
    config["platform"] = "mqtt";

    if (index == 0) output += "\n\n" + switchType + ":";
    output += "\n";
    bool first = true;

    for (auto kv : config) {
        if (first) {
            output += "  - ";
            first = false;
        } else {
            output += "    ";
        }
        output += kv.key;
        output += ": ";
        if (strncmp(kv.key, "payload_", strlen("payload_")) == 0) {
            output += _haFixPayload(kv.value.as<String>());
        } else {
            output += kv.value.as<String>();
        }
        output += "\n";
    }
    output += " ";

    root["haConfig"] = output;
}

#if SENSOR_SUPPORT

void _haSensorYaml(std::shared_ptr<ha_config_t> ha_config, unsigned char index, JsonObject& root) {

    String output;
    output.reserve(HA_YAML_BUFFER_SIZE);

    JsonObject& config = ha_config->get("sensor");
    _haSendMagnitude(ha_config->loadMagnitude(index), config);
    config["platform"] = "mqtt";

    if (index == 0) output += "\n\nsensor:";
    output += "\n";
    bool first = true;

    for (auto kv : config) {
        if (first) {
            output += "  - ";
            first = false;
        } else {
            output += "    ";
        }
        String value = kv.value.as<String>();
        value.replace("%", "'%'");
        output += kv.key;
        output += ": ";
        output += value;
        output += "\n";
    }
    output += " ";

    root["haConfig"] = output;

}

#endif // SENSOR_SUPPORT

void _haGetDeviceConfig(JsonObject& config) {
    config.createNestedArray("identifiers").add(getIdentifier());
    config["name"] = getSetting("desc", getSetting("hostname"));
    config["manufacturer"] = MANUFACTURER;
    config["model"] = DEVICE;
    config["sw_version"] = String(APP_NAME) + " " + APP_VERSION + " (" + getCoreVersion() + ")";
}

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
        if (!callbacks.size()) return;

        auto ha_config = std::make_shared<ha_config_t>();
        {
            for (unsigned char idx=0; idx<relayCount(); ++idx) {
                callbacks.push_back([idx, ha_config](JsonObject& root) {
                    _haSwitchYaml(ha_config, idx, root);
                });
            }
        }
        #if SENSOR_SUPPORT
        {
            for (unsigned char idx=0; idx<magnitudeCount(); ++idx) {
                callbacks.push_back([idx, ha_config](JsonObject& root) {
                    _haSensorYaml(ha_config, idx, root);
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
            DynamicJsonBuffer jsonBuffer(1024);
            JsonObject& root = jsonBuffer.createObject();
            _haSwitchYaml(ha_config, idx, root);
            DEBUG_MSG(root["haConfig"].as<String>().c_str());
        }
        #if SENSOR_SUPPORT
            for (unsigned char idx=0; idx<magnitudeCount(); ++idx) {
                DynamicJsonBuffer jsonBuffer(1024);
                JsonObject& root = jsonBuffer.createObject();
                _haSensorYaml(ha_config, idx, root);
                DEBUG_MSG(root["haConfig"].as<String>().c_str());
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
