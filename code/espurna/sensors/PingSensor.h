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

    uint16_t seq_timeout = PING_TIMEOUT;
    uint16_t seq_retries = PING_RETRIES;
    uint16_t seq_num = 0;

    uint32_t last_recv = 0;

    ip_addr_t target;
    struct raw_pcb *pcb;
};

/* LWIP callback for recv. Will trigger when ICMP answer is received */
#if LWIP_VERSION_MAJOR == 1
u8_t ping_recv(void* arg, struct raw_pcb* pcb, struct pbuf* p, ip_addr_t* addr) {
#else
u8_t ping_recv(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
#endif

    DEBUG_MSG("[PING] ping_recv\n");

    struct ping_ctx_t* ctx = (struct ping_ctx_t*)arg;

    struct icmp_echo_hdr* iecho;
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(addr);
    LWIP_ASSERT("p != NULL", p != NULL);

    ctx->state = ping_ctx_t::IDLE;

    // STABLE-1_4_0: if (pbuf_header( p, -PBUF_IP_HLEN)==0) {
    DEBUG_MSG("[PING] ping_recv pbuf tot_len=%u expected=%u\n", p->tot_len, (PBUF_IP_HLEN + sizeof(struct icmp_echo_hdr)));
    if ((p->tot_len >= (PBUF_IP_HLEN + sizeof(struct icmp_echo_hdr)))
    #if LWIP_VERSION_MAJOR == 1
            && (pbuf_header(p, -PBUF_IP_HLEN) == 0)
    #else
            && (pbuf_remove_header(p, PBUF_IP_HLEN) == 0)
    #endif
        ) {
        iecho = (struct icmp_echo_hdr *)p->payload;
        DEBUG_MSG("[PING] ping_recv iecho id=%x seqno=%u expected=%u\n", iecho->id, lwip_ntohs(iecho->seqno), ctx->seq_num);
        if ((iecho->id == ctx->id) && (iecho->seqno == lwip_htons(ctx->seq_num))) {
            DEBUG_MSG("[PING] ping_recv OK\n");
            ctx->last_recv = sys_now();
            pbuf_free(p);
            return 1; /* eat the packet */
        }
        #if LWIP_VERSION_MAJOR == 1
            pbuf_header(p, PBUF_IP_HLEN);
        #else
            pbuf_add_header(p, PBUF_IP_HLEN);
        #endif
    }

    DEBUG_MSG("[PING] ping_recv fail\n");

    return 0; /* don't eat the packet */
}

/* Fill in all required fields for iecho struct */
void ping_prepare_echo(ping_ctx_t *ctx, struct icmp_echo_hdr *iecho, u16_t len) {

    size_t data_len = len - sizeof(struct icmp_echo_hdr);

    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);

    iecho->chksum = 0;
    iecho->id     = ctx->id;
    iecho->seqno  = lwip_htons(ctx->seq_num);

    /* fill the additional data buffer with some data */
    for(int i = 0; i < data_len; i++) {
        ((char*)iecho)[sizeof(struct icmp_echo_hdr) + i] = (char)i;
    }

    iecho->chksum = inet_chksum(iecho, len);
}

#define PING_DATA_SIZE 32

/* Periodically called, send when available and process ping timeout */
void ping_periodic(struct ping_ctx_t *ctx) {

    DEBUG_MSG("[PING] ping_periodic \n");

    // lwip2 note: ip_route(src, dest) instead of ip_route(dest), and the code below will work anyways without this check
    #if LWIP_VERSION_MAJOR == 1
    {
        netif* interface = ip_route(&ctx->target);
        if (!interface) {
            DEBUG_MSG("[PING] No route to %s\n", ipaddr_ntoa(&ctx->target));
            return;
        }
    }
    #endif

    // bail out after timeout
    if (sys_now() - ctx->last_recv > ctx->seq_timeout) {
        ctx->state = ping_ctx_t::IDLE;
        DEBUG_MSG("[PING] ping timeout\n");
    }

    // don't try to send multiple requests
    //if (ctx->state == ping_ctx_t::SENT) {
    //    DEBUG_MSG("[PING] ping_periodic waiting until IDLE\n");
    //    return;
    //}

    struct pbuf *p;
    struct icmp_echo_hdr *iecho;
    size_t ping_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;

    p = pbuf_alloc(PBUF_IP, ping_size, PBUF_RAM);
    if (!p) {
        DEBUG_MSG("[PING] ping_periodic pbuf_alloc failed\n");
        return;
    }

    if ((p->len == p->tot_len) && (p->next == nullptr)) {
        // lwip1 XXX: this was moved from inside of ping_prepare_echo
        // lwip_htons is a one-line macro, increment happens twice
        ++ctx->seq_num;

        iecho = (struct icmp_echo_hdr *)p->payload;
        ping_prepare_echo(ctx, iecho, (u16)ping_size);

        DEBUG_MSG("[PING] ping_periodic raw_send to=%s seq_num=%u size=%u\n",
            ipaddr_ntoa(&ctx->target), ctx->seq_num, ping_size);
        raw_sendto(ctx->pcb, p, &ctx->target);

        ctx->state = ping_ctx_t::SENT;
    }
    pbuf_free(p);
}

void ping_send_and_wait(ping_ctx_t*);

void ping_cb(void* arg) {

    struct ping_ctx_t* ctx = (struct ping_ctx_t*)arg;
    ping_send_and_wait(ctx);

}

void ping_send_and_wait(ping_ctx_t *ctx) {
    ping_periodic(ctx);
    sys_timeout(ctx->delay, ping_cb, ctx);
}

void ping_raw_init(ping_ctx_t *ctx) {
    if (ctx->state == ping_ctx_t::STOPPED) {
        ctx->pcb = raw_new(IP_PROTO_ICMP);
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
            return ping_ctx.target;
        }

        void setAddress(const String& address) {
            ipaddr_aton(address.c_str(), &ping_ctx.target);
        }

        void setAddress(const ip_addr_t& address) {
            ip_addr_copy(ping_ctx.target, address);
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
            ping_ctx.seq_timeout = ping_ctx.delay * value;
            ping_ctx.seq_retries = value;
        }

        void setID(uint16_t value) {
            ping_ctx.id = value;
        }

        // XXX: latest core supports direct conversion from WiFi.gatewayIP() -> IPAddress
        //      in theory, this would allow ipv6 support out-of-the-box
        //      Right now, use SDK API to directly reference netif gw
        void updateGatewayIP() {
            DEBUG_MSG("[PING] updateGatewayIP() enter\n");
            _address_set = true;
            struct netif* sta = eagle_lwip_getif(STATION_IF);
            DEBUG_MSG("[PING] Updating target=%s gw=%s\n", ipaddr_ntoa(&ping_ctx.target), ipaddr_ntoa(&sta->gw));
            ip_addr_copy(ping_ctx.target, sta->gw);
        }

        // ---------------------------------------------------------------------
        // Generic Sensor API
        // ---------------------------------------------------------------------

        // Always ready after initial connection
        void begin() {
            if (!wifiConnected()) return;
            if (!_address_set) return;
            DEBUG_MSG("[PING] Ready\n");
            ping_raw_init(&ping_ctx);
            _ready = true;
        }

        // Descriptive name of the sensor
        String description() {
            char buffer[20];
            snprintf(buffer, sizeof(buffer), "PING @ %s", ipaddr_ntoa(&ping_ctx.target));
            return String(buffer);
        }

        // Descriptive name of the slot # index
        String slot(unsigned char index) {
            return description();
        };

        // Address of the sensor (it could be the GPIO or I2C address)
        String address(unsigned char index) {
            UNUSED(index);
            return ipaddr_ntoa(&ping_ctx.target);
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
                DEBUG_MSG("[PING] ping_ctx state=%s seq_num=%u\n", ping_state_debug(&ping_ctx), ping_ctx.seq_num);
            }
        }

        // Current value for slot # index
        double value(unsigned char index) {
            if (index == 0) {
                return ((ping_ctx.last_recv > 0) && (sys_now() - ping_ctx.last_recv < ping_ctx.seq_timeout)) ? 1 : 0;
            }

            return 0;
        }

        void configure() {
            setDelay(getSetting("pingDelay", PING_DELAY).toInt());
            setID(getSetting("pingID", PING_ID).toInt());
            setRetries(getSetting("pingRetries", PING_RETRIES).toInt());
            DEBUG_MSG("[PING] configure() seq_retries=%u delay=%u id=%x\n", ping_ctx.seq_retries, ping_ctx.delay, ping_ctx.id);

            String addr = getSetting("pingAddress", PING_ADDRESS);
            if (addr.toInt() == 0) {
                DEBUG_MSG("[PING] Using GW address\n");
                useGatewayIP(true);
            } else {
                DEBUG_MSG("[PING] Using %s\n", addr.c_str());
                setAddress(addr);
                _address_set = true;
            }
        }

    protected:

        // ---------------------------------------------------------------------
        // Protected
        // ---------------------------------------------------------------------

        ping_ctx_t ping_ctx;

        bool _use_gateway_ip = false;
        bool _address_set = false;

};

#endif // SENSOR_SUPPORT && PING_SUPPORT
