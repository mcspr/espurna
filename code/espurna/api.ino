/*

API MODULE

Copyright (C) 2016-2018 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if WEB_SUPPORT

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <vector>

typedef struct {
    char * key;
    api_get_callback_f getFn = NULL;
    api_put_callback_f putFn = NULL;
} web_api_t;
std::vector<web_api_t> _apis;

typedef struct {
    char * name;
    api_action_callback_f callbackFn = NULL;
} web_api_action_t;
std::vector<web_api_action_t> _api_actions;

typedef struct {
    char * key;
    json_api_get_callback_f getFn = NULL;
    json_api_put_callback_f putFn = NULL;
} web_json_api_t;
std::vector<web_json_api_t> _json_apis;

// -----------------------------------------------------------------------------

bool _apiWebSocketOnReceive(const char * key, JsonVariant& value) {
    return (strncmp(key, "api", 3) == 0);
}

void _apiWebSocketOnSend(JsonObject& root) {
    root["apiEnabled"] = getSetting("apiEnabled", API_ENABLED).toInt() == 1;
    root["apiKey"] = getSetting("apiKey");
    root["apiRealTime"] = getSetting("apiRealTime", API_REAL_TIME_VALUES).toInt() == 1;
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

bool _authAPI(AsyncWebServerRequest *request) {

    if (getSetting("apiEnabled", API_ENABLED).toInt() == 0) {
        DEBUG_MSG_P(PSTR("[WEBSERVER] HTTP API is not enabled\n"));
        request->send(403);
        return false;
    }

    if (!request->hasParam("apikey", (request->method() == HTTP_PUT))) {
        DEBUG_MSG_P(PSTR("[WEBSERVER] Missing apikey parameter\n"));
        request->send(403);
        return false;
    }

    AsyncWebParameter* p = request->getParam("apikey", (request->method() == HTTP_PUT));
    if (!p->value().equals(getSetting("apiKey"))) {
        DEBUG_MSG_P(PSTR("[WEBSERVER] Wrong apikey parameter\n"));
        request->send(403);
        return false;
    }

    return true;

}

bool _asJson(AsyncWebServerRequest *request) {
    bool asJson = false;
    if (request->hasHeader("Accept")) {
        AsyncWebHeader* h = request->getHeader("Accept");
        asJson = h->value().equals("application/json");
    }
    return asJson;
}

ArRequestHandlerFunction _bindAPI(unsigned int apiID) {

    return [apiID](AsyncWebServerRequest *request) {

        webLog(request);
        if (!_authAPI(request)) return;

        web_api_t api = _apis[apiID];

        // Check if its a PUT
        if (api.putFn != NULL) {
            if (request->hasParam("value", request->method() == HTTP_PUT)) {
                AsyncWebParameter* p = request->getParam("value", request->method() == HTTP_PUT);
                (api.putFn)((p->value()).c_str());
            }
        }

        // Get response from callback
        char value[API_BUFFER_SIZE] = {0};
        (api.getFn)(value, API_BUFFER_SIZE);

        // The response will be a 404 NOT FOUND if the resource is not available
        if (0 == value[0]) {
            DEBUG_MSG_P(PSTR("[API] Sending 404 response\n"));
            request->send(404);
            return;
        }
        DEBUG_MSG_P(PSTR("[API] Sending response '%s'\n"), value);

        // Format response according to the Accept header
        if (_asJson(request)) {
            char buffer[64];
            if (isNumber(value)) {
                snprintf_P(buffer, sizeof(buffer), PSTR("{ \"%s\": %s }"), api.key, value);
            } else {
                snprintf_P(buffer, sizeof(buffer), PSTR("{ \"%s\": \"%s\" }"), api.key, value);
            }
            request->send(200, "application/json", buffer);
        } else {
            request->send(200, "text/plain", value);
        }

    };

}

#if JSON_API_SUPPORT

bool _validateJsonAPIRequest(AsyncWebServerRequest *request) {
    if (getSetting("apiEnabled", API_ENABLED).toInt() == 0) {
        DEBUG_MSG_P(PSTR("[WEBSERVER] HTTP JSON API is not enabled\n"));
        request->send(403); // Forbiddden
        return false;
    }

    if (!_asJson(request)) {
        request->send(400);
        return false;
    }

    if (!request->hasHeader(F("Authorization"))) {
        request->send(400);
        return false;
    }

    // Authorization: token <apiKey value>
    AsyncWebHeader* auth = request->getHeader(F("Authorization"));
    String value = auth->value();
    if (!value.substring(6).equals(getSetting("apiKey"))) {
        request->send(403);
        return false;
    }

    return true;
}


ArRequestHandlerFunction _bindJsonAPIGet(unsigned int apiID) {
    return [apiID](AsyncWebServerRequest *request) {
        webLog(request);
        if (!_validateJsonAPIRequest(request)) return;
        if (request->method() == HTTP_PUT) {
            // WAIT FOR BODY
            return;
        }

        web_json_api_t api = _json_apis[apiID];

        AsyncJsonResponse *response = new AsyncJsonResponse();
        JsonObject& root = response->getRoot();

        if (request->method() == HTTP_GET) {
            (api.getFn)(root);

            response->setLength();
            request->send(response);

            return;
        }

        request->send(405); // Method not allowed
    };
}

ArRequestHandlerFunction _bindJsonAPIPut(unsigned int apiID) {
    return [apiID](AsyncWebServerRequest *request) {
        webLog(request);
        if (!_validateJsonAPIRequest(request)) return;

        AsyncJsonResponse *response = new AsyncJsonResponse();
        JsonObject& response_root = response->getRoot();

        // No content-type set or zero length body
        if (!request->_tempObject) {
            response_root.set("error", F("No data"));
            response->setCode(400); // Bad request
            response->setLength();
            request->send(response);
            return;
        }

        const char *buffer = (const char*)request->_tempObject;
        DynamicJsonBuffer jsonBuffer;
        JsonObject& request_root = jsonBuffer.parseObject(buffer);
        if (!request_root.success()) {
            response_root.set("error", F("Cannot parse json data"));
            response->setCode(400);
            response->setLength();
            request->send(response);
            return;
        }

        web_json_api_t api = _json_apis[apiID];

        (api.putFn)(request_root, response_root);
        if (response_root.containsKey("error")) {
            response->setCode(400);
        }

        response->setLength();
        request->send(response);
    };
}

ArBodyHandlerFunction _bindJsonAPIPutBody(unsigned int apiID) {
    return [apiID](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DEBUG_MSG_P(PSTR("Handling PUT JSON api body\n"));
        if (!_validateJsonAPIRequest(request)) return;

        if (index == 0) {
            request->_tempObject = new char[total + 1];
        }

        char *buffer = (char *) request->_tempObject + index;
        memcpy(buffer, data, len);
        if ((index + len) == total) {
            ((char *)request->_tempObject)[total] = '\0';
        }
    };
}

void _onAPIs(AsyncWebServerRequest *request) {

    webLog(request);
    if (!_authAPI(request)) return;

    bool asJson = _asJson(request);

    char buffer[40];

    String output;
    if (asJson) {
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        for (unsigned int i=0; i < _apis.size(); i++) {
            snprintf_P(buffer, sizeof(buffer), PSTR("/api/%s"), _apis[i].key);
            root[_apis[i].key] = String(buffer);
        }
        for (unsigned int i=0; i < _json_apis.size(); i++) {
            snprintf_P(buffer, sizeof(buffer), PSTR("/api/%s"), _json_apis[i].key);
            root[_json_apis[i].key] = String(buffer);
        }
        root.printTo(output);
        jsonBuffer.clear();
        request->send(200, "application/json", output);

    } else {
        for (unsigned int i=0; i < _apis.size(); i++) {
            snprintf_P(buffer, sizeof(buffer), PSTR("/api/%s"), _apis[i].key);
            output += _apis[i].key + String(" -> ") + String(buffer) + String("\n");
        }
        request->send(200, "text/plain", output);
    }

}

void _listJsonRPCActions(JsonObject& response) {
    JsonArray& array = response.createNestedArray("actions");
    for (unsigned int i=0; i < _api_actions.size(); i++) {
        array.add(_api_actions[i].name);
    }
}

void _handleJsonRPCAction(JsonObject& request, JsonObject& response) {
    if (request.containsKey("action")) {
        String action = request.get<String>("action");
        DEBUG_MSG_P(PSTR("[RPC] Action: %s\n"), action.c_str());

        for (unsigned int i=0; i < _api_actions.size(); i++) {
            if (action.equals(_api_actions[i].name)) {
                (_api_actions[i].callbackFn)();
                response.set("result", "success");
                return;
            }
        }
    }

    response.set("error", "no matching action");
}

#endif // JSON_API_SUPPORT

void _onRPC(AsyncWebServerRequest *request) {

    webLog(request);
    if (!_authAPI(request)) return;

    //bool asJson = _asJson(request);
    int response = 404;

    if (request->hasParam("action")) {

        AsyncWebParameter* p = request->getParam("action");
        String action = p->value();
        DEBUG_MSG_P(PSTR("[RPC] Action: %s\n"), action.c_str());

        if (action.equals("reboot")) {
            response = 200;
            deferredReset(100, CUSTOM_RESET_RPC);
        }

    }

    request->send(response);

}

// -----------------------------------------------------------------------------

void apiRegister(const char * key, api_get_callback_f getFn, api_put_callback_f putFn) {

    // Store it
    web_api_t api;
    char buffer[40];
    snprintf_P(buffer, sizeof(buffer), PSTR("/api/%s"), key);
    api.key = strdup(key);
    api.getFn = getFn;
    api.putFn = putFn;
    _apis.push_back(api);

    // Bind call
    unsigned int methods = HTTP_GET;
    if (putFn != NULL) methods += HTTP_PUT;
    webServer()->on(buffer, methods, _bindAPI(_apis.size() - 1));

}

#if JSON_API_SUPPORT
void apiRegister(const char * key, json_api_get_callback_f getFn, json_api_put_callback_f putFn) {
    web_json_api_t api;
    char buffer[40];
    snprintf_P(buffer, sizeof(buffer), PSTR("/api/%s"), key);
    api.key = strdup(key);
    api.getFn = getFn;
    api.putFn = putFn;
    _json_apis.push_back(api);

    unsigned int idx = _json_apis.size() - 1;

    webServer()->on(buffer, HTTP_GET, _bindJsonAPIGet(idx));
    if (putFn != NULL) {
        webServer()->on(buffer, HTTP_PUT, _bindJsonAPIPut(idx), NULL, _bindJsonAPIPutBody(idx));
    }
}
#endif

void apiRegisterAction(const char * name, api_action_callback_f callbackFn) {
    web_api_action_t action;
    action.name = strdup(name);
    action.callbackFn = callbackFn;
    _api_actions.push_back(action);
}

void apiSetup() {
    webServer()->on("/apis", HTTP_GET, _onAPIs);
    webServer()->on("/rpc", HTTP_GET, _onRPC);
    apiRegisterAction("reboot", [](void) { deferredReset(1000, CUSTOM_RESET_RPC); });

    #if JSON_API_SUPPORT
        apiRegister("_/device", info_device, NULL);
        apiRegister("_/status", info_status, NULL);

        apiRegister("_/rpc", _listJsonRPCActions, _handleJsonRPCAction);
    #endif

    wsOnSendRegister(_apiWebSocketOnSend);
    wsOnReceiveRegister(_apiWebSocketOnReceive);
}

#endif // WEB_SUPPORT
