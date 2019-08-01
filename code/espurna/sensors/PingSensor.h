// -----------------------------------------------------------------------------
// Ping Sensor (IPv4 ICMP network probing)
// Copyright (C) 2017-2019 by Xose Pérez <xose dot perez at gmail dot com>
// -----------------------------------------------------------------------------

#if SENSOR_SUPPORT && PING_SUPPORT

#pragma once

#include <Arduino.h>
#include "BaseSensor.h"

#include <lwip/init.h>
#include <lwip/opt.h>

#if !LWIP_RAW // this is unlikely to happen with ESP8266... (are there sockets with IDF?) but place this here anyways to avoid further compilation errors below this point.
#error "LWIP_RAW is required for ping sensor to work!"
#endif

#include <lwip/tcpip.h>
#include <lwip/ip_addr.h>
#include <lwip/mem.h>
#include <lwip/raw.h>
#include <lwip/icmp.h>
#include <lwip/netif.h>
#include <lwip/sys.h>
#include <lwip/inet_chksum.h>

#if LWIP_VERSION_MAJOR == 1
#include <lwip/timers.h>
#else
#include <lwip/timeouts.h>
#endif

/**
 * @file
 * Ping sender module
 *
 */

/*
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * (modified to use local context and different API to notify about the successful ping)
 *
 */

struct ping_ctx_t {
    enum state_t {
        STOPPED,
        IDLE,
        SENT,
    };

    state_t state = STOPPED;
    uint16_t id = PING_ID;
    uint16_t delay = PING_DELAY;

    uint16_t seq_retries = PING_RETRIES;
    uint16_t seq_fail = 0;
    uint16_t seq_num = 0;

    uint32_t time = 0;

    ip_addr_t target;
    struct raw_pcb *pcb;
};

/** Prepare a echo ICMP request */
void ping_prepare_echo (struct ping_ctx_t *ctx, struct icmp_echo_hdr *iecho, u16_t len) {

    size_t i;
    size_t data_len = len - sizeof(struct icmp_echo_hdr);
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->chksum = 0;
    iecho->id     = ctx->id;
    iecho->seqno  = lwip_htons(++ctx->seq_num);

    /* fill the additional data buffer with some data */
    for(i = 0; i < data_len; i++) {
        ((char*)iecho)[sizeof(struct icmp_echo_hdr) + i] = (char)i;
    }

    iecho->chksum = inet_chksum(iecho, len);
}

/* Ping using the raw ip */
#if LWIP_VERSION_MAJOR == 1
u8_t ping_recv(void* arg, struct raw_pcb* pcb, struct pbuf* p, ip_addr_t* addr) {
#else
u8_t ping_recv(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
#endif

    struct ping_ctx_t* ctx = (struct ping_ctx_t*)arg;

    struct icmp_echo_hdr* iecho;
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(addr);
    LWIP_ASSERT("p != NULL", p != NULL);

    if ((iecho->id == ctx->id) && (iecho->seqno == lwip_htons(ctx->seq_num))) {
        iecho = (struct icmp_echo_hdr*)((long)p->payload + PBUF_IP_HLEN);
        if (iecho->id == ctx->id)
        {
            ctx->state = ping_ctx_t::OK;
            --ctx->seq_fail;
            pbuf_free(p);
            return 1; /* eat the packet */
        }
    }

    return 0; /* don't eat the packet */
}

#define PING_DATA_SIZE 32

void ping_send(struct ping_ctx_t *ctx) {

    if (ctx->target.addr == IPADDR_ANY) return;

    struct pbuf *p;
    struct icmp_echo_hdr *iecho;
    size_t ping_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;

    p = pbuf_alloc(PBUF_IP, ping_size, PBUF_RAM);
    if (!p) {
        return;
    }

    if ((p->len == p->tot_len) && (p->next == NULL)) {
        iecho = (struct icmp_echo_hdr *)p->payload;
        ping_prepare_echo(ctx, iecho, (u16)ping_size);
        DEBUG_MSG("[PING] ping_send to=%s seq=%u fail=%u size=%u\n",
            ipaddr_ntoa(&ctx->target), ctx->seq_num, ctx->seq_fail, ping_size);
        ++ctx->seq_fail;
        ctx->time = sys_now();
        raw_sendto(ctx->pcb, p, &ctx->target);
    }
    pbuf_free(p);
}

void ping_send_and_wait(struct ping_ctx_t*);

void ping_cb(void* arg) {

    struct ping_ctx_t* ctx = (struct ping_ctx_t*)arg;

    if (ctx->seq_fail > ctx->seq_retries) {
        ctx->state = ping_ctx_t::FAILED;
        ctx->seq_num = 0;
        ctx->seq_fail = 0;
        return;
    }

    ping_send_and_wait(ctx);

}

void ping_send_and_wait(struct ping_ctx_t *ctx) {
    ctx->state = ping_ctx_t::SENT;
    ping_send(ctx);
    sys_timeout(ctx->delay, ping_cb, ctx);
}

void ping_raw_init(ping_ctx_t *ctx, const ip_addr_t* ping_addr) {
    if (ctx->state == ping_ctx_t::STOPPED) {
        ctx->pcb = raw_new(IP_PROTO_ICMP);
        ip_addr_copy(ctx->target, *ping_addr);
        raw_recv(ctx->pcb, ping_recv, ctx);
        raw_bind(ctx->pcb, IP_ADDR_ANY);
        sys_timeout(ctx->delay, ping_cb, ctx);
    }
}

const char* ping_state_debug(ping_ctx_t *ctx) {

    switch (ctx->state) {
        case ping_ctx_t::STOPPED: return "STOPPED";
        case ping_ctx_t::IDLE: return "IDLE";
        case ping_ctx_t::SENT: return "SENT";
        default: break;
    }

    return "UNKNOWN";
}

extern "C" struct netif* eagle_lwip_getif (int netif_index);

class PingSensor : public BaseSensor {

    public:
        PingSensor(): BaseSensor() {
            _count = 1;
            _sensor_id = SENSOR_PING_ID;
        }

        // ---------------------------------------------------------------------

        const ip_addr_t& getAddress() {
            return _address;
        }

        void setAddress(const String& address) {
            ipaddr_aton(address.c_str(), &_address);
        }

        void setAddress(const ip_addr_t& address) {
            ip_addr_copy(_address, address);
        }

        bool useGatewayIP() {
            return _use_gateway_ip;
        }

        void useGatewayIP(bool value) {
            _use_gateway_ip = value;
        }

        // Modify struct directly, instead of caching
        void setDelay(uint16_t value) {
            ping_ctx.delay = value;
        }

        void setRetries(uint16_t value) {
            ping_ctx.seq_retries = value;
        }

        void setID(uint16_t value) {
            ping_ctx.id = value;
        }

        // XXX: latest core supports direct conversion from WiFi.gatewayIP() -> IPAddress
        //      in theory, this would allow ipv6 support out-of-the-box
        //      Right now, use SDK API to directly reference netif gw
        void updateGatewayIP() {
            _address_set = true;
            struct netif* sta = eagle_lwip_getif(STATION_IF);
            ip_addr_copy(_address, sta->gw);
            DEBUG_MSG("[PING] Updating target=%s gw=%s\n", ipaddr_ntoa(&_address), ipaddr_ntoa(&sta->gw));
        }

        // ---------------------------------------------------------------------
        // Generic Sensor API
        // ---------------------------------------------------------------------

        // Always ready after initial connection
        void begin() {
            if (!wifiConnected()) return;
            if (!_address_set) return;
            ping_raw_init(&ping_ctx, &_address);
            _ready = true;
        }

        // Descriptive name of the sensor
        String description() {
            char buffer[20];
            snprintf(buffer, sizeof(buffer), "PING @ %s", ipaddr_ntoa(&_address));
            return String(buffer);
        }

        // Descriptive name of the slot # index
        String slot(unsigned char index) {
            return description();
        };

        // Address of the sensor (it could be the GPIO or I2C address)
        String address(unsigned char index) {
            return ipaddr_ntoa(&_address);
        }

        // Type for slot # index
        unsigned char type(unsigned char index) {
            if (index == 0) return MAGNITUDE_DIGITAL;
            return MAGNITUDE_NONE;
        }

        void tick() {
            static uint32_t report_last = millis();
            static uint32_t reset_last = millis();

            if (millis() - report_last > 1000) {
                report_last = millis();
                DEBUG_MSG("[PING] ping_ctx state=%s seq_num=%u seq_fail=%u\n", ping_state_debug(&ping_ctx), ping_ctx.seq_num, ping_ctx.seq_fail);
            }

            if (millis() - reset_last > 15000) {
                reset_last = millis();
                if (ping_ctx.seq_fail > ping_ctx.seq_retries) {
                    ping_ctx.seq_fail = 0;
                    ping_ctx.seq_num = 0;
                }
            }
        }

        // Current value for slot # index
        double value(unsigned char index) {
            if (index == 0) {
                return (ping_ctx.state == ping_ctx_t::OK) ? 1 : 0;
            }

            return 0;
        }

        void configure() {
            setDelay(getSetting("pingDelay", PING_DELAY).toInt());
            setID(getSetting("pingID", PING_ID).toInt());
            setRetries(getSetting("pingRetries", PING_RETRIES).toInt());

            String addr = getSetting("pingAddress", PING_ADDRESS);
            if (addr.toInt() == 0) {
                useGatewayIP(true);
            } else {
                setAddress(addr);
            }
        }

    protected:

        // ---------------------------------------------------------------------
        // Protected
        // ---------------------------------------------------------------------

        ping_ctx_t ping_ctx;
        ip_addr_t _address;
        bool _use_gateway_ip = false;
        bool _address_set = false;

};

#endif // SENSOR_SUPPORT && PING_SUPPORT
