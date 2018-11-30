/*

DNS MODULE

Copyright (C) 2018 by Maxim Prokhorov <prokhorov dot max at outlook dot com>

*/
#if DNS_SUPPORT

// lwip1 ref: <core>/tools/sdk/lwip/include/lwipopts.h
// lwip2 ref: <core>/tools/sdk/lwip2/builder/glue-lwip/arduino/lwipopts.h
// 256 and 128 respectively
char _dns_hostname[DNS_MAX_NAME_LENGTH];
IPAddress _dns_ipaddr;
enum class dns_state_t {
    ERROR,
    IDLE,
    INPROGRESS,
    NOTFOUND,
    DONE
};

dns_state_t _dns_state = dns_state_t::IDLE;
err_t _dns_lasterr = ERR_OK;

void _dnsDefaultCb(const char* ip) {
        (void)ip;
        DEBUG_MSG_P(PSTR("[DNS] Found IP for %s: %s\n"), _dns_hostname, _dns_ipaddr.toString().c_str());
}
dns_resolver_cb_f _dns_cb = &_dnsDefaultCb;

#if (LWIP_VERSION_MAJOR == 1)
void _dns_found_callback(const char *name, ip_addr_t *ipaddr, void *arg) {
#else
void _dns_found_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
#endif
    if (!ipaddr) {
        _dns_state = dns_state_t::NOTFOUND;
        return;
    }

    _dns_state = dns_state_t::DONE;
    _dns_ipaddr = IPAddress(ipaddr);
}

void _dns_gethostbyname(const char* name) {

    if (strlen(name) > (sizeof(_dns_hostname) - 1)) {
        DEBUG_MSG_P(PSTR("[DNS] Hostname is too long!\n"));
        return;
    }

    memset(&_dns_hostname, 0, sizeof(_dns_hostname));
    memcpy(&_dns_hostname, name, strlen(name));

    ip_addr_t result;
    err_t error;

    error = dns_gethostbyname(name, &result, (dns_found_callback)&_dns_found_callback, nullptr);

    if (error == ERR_OK) {
        _dns_state = dns_state_t::DONE;
        _dns_ipaddr = IPAddress(result);
        return;
    } else if (error == ERR_INPROGRESS) {
        _dns_state = dns_state_t::INPROGRESS;
        return;
    } else {
        _dns_state = dns_state_t::ERROR;
        _dns_lasterr = error;
        return;
    }

}

void _dnsLoop() {
    switch (_dns_state) {
        case (dns_state_t::ERROR):
            if (_dns_lasterr != ERR_OK) {
                DEBUG_MSG_P(PSTR("[DNS] gethostbyname error: %s\n"), lwip_strerr(_dns_lasterr));
                _dns_lasterr = ERR_OK;
            }
            _dns_state = dns_state_t::IDLE;
            return;
        case (dns_state_t::IDLE):
        case (dns_state_t::INPROGRESS):
            return;
        case (dns_state_t::NOTFOUND):
            _dns_state = dns_state_t::IDLE;
            DEBUG_MSG_P(PSTR("[DNS] Did not find IP for %s\n"), _dns_hostname);
            return;
        case (dns_state_t::DONE):
            _dns_state = dns_state_t::IDLE;
            _dns_cb(_dns_ipaddr.toString().c_str());
            _dns_cb = _dnsDefaultCb;
            return;
    }
}

void dnsLastRequest(String* request, IPAddress* response) {
    *request = _dns_hostname;
    *response = _dns_ipaddr;
}

void dnsResolve(const char* hostname, const dns_resolver_cb_f& callback) {
    if (_dns_state != dns_state_t::IDLE) {
        return;
    }

    if (!jw.connected()) {
        return;
    }

    _dns_cb = callback;
    _dns_gethostbyname(hostname);
}

void dnsSetup() {
    #if TERMINAL_SUPPORT
        settingsRegisterCommand(F("NSLOOKUP"), [](Embedis* e) {
            if (!jw.connected()) {
                DEBUG_MSG_P(PSTR("[DNS] Not connected to WiFi\n"));
                return;
            }

            if (_dns_state != dns_state_t::IDLE) {
                DEBUG_MSG_P(PSTR("[DNS] Resolver is busy\n"));
                return;
            }

            if (e->argc != 2) {
                DEBUG_MSG_P(PSTR("-ERROR: Wrong arguments\n"));
                return;
            }

            _dns_gethostbyname(e->argv[1]);
        });
    #endif

    espurnaRegisterLoop(_dnsLoop);
}

#endif // DNS_SUPPORT
