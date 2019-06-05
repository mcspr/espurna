#pragma once

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------

void debugSend(const char * format, ...);
void debugSend_P(PGM_P format, ...);

#define DEBUG_SUPPORT           DEBUG_SERIAL_SUPPORT || DEBUG_UDP_SUPPORT || DEBUG_TELNET_SUPPORT || DEBUG_WEB_SUPPORT

#if DEBUG_SUPPORT
    #define DEBUG_MSG(...) debugSend(__VA_ARGS__)
    #define DEBUG_MSG_P(...) debugSend_P(__VA_ARGS__)
#endif

#ifndef DEBUG_MSG
    #define DEBUG_MSG(...)
    #define DEBUG_MSG_P(...)
#endif

#define LWIP_INTERNAL
#include <ESP8266WiFi.h>

#include "lwip/opt.h"
#include "lwip/ip.h"
#include "lwip/tcp.h"
#include "lwip/inet.h"

// not yet CONNECTING or LISTENING
extern struct tcp_pcb *tcp_bound_pcbs;
// accepting or sending data
extern struct tcp_pcb *tcp_active_pcbs;
// TIME-WAIT status
extern struct tcp_pcb *tcp_tw_pcbs;

// TODO: listen_pcbs? 
// union tcp_listen_pcbs_t tcp_listen_pcbs;

extern "C" void tcp_abort (struct tcp_pcb* pcb);

void
_debug_tcp_remove(struct tcp_pcb* pcb_list) {
  struct tcp_pcb *pcb = pcb_list;
  struct tcp_pcb *pcb2;

  while(pcb != NULL) {
      pcb2 = pcb;
      pcb = pcb->next;
      tcp_abort(pcb2);
  }
}

const char *const debug_tcp_state_str[] = {
    "CLOSED",
    "LISTEN",
    "SYN_SENT",
    "SYN_RCVD",
    "ESTABLISHED",
    "FIN_WAIT_1",
    "FIN_WAIT_2",
    "CLOSE_WAIT",
    "CLOSING",
    "LAST_ACK",
    "TIME_WAIT"
};

void _debug_tcp_print_pcb(struct tcp_pcb* pcb) {

    char remote_ip[32] = {0};
    char local_ip[32] = {0};

    inet_ntoa_r((pcb->local_ip), local_ip, sizeof(local_ip));
    inet_ntoa_r((pcb->remote_ip), remote_ip, sizeof(remote_ip));

    DEBUG_MSG_P(PSTR("state=%s local=%s:%u remote=%s:%u snd_nxt=%u rcv_nxt=%u\n"),
            debug_tcp_state_str[pcb->state],
            local_ip, pcb->local_port,
            remote_ip, pcb->remote_port,
            pcb->snd_nxt, pcb->rcv_nxt
    );

}

void debug_tcp_print_pcbs(void) {

    struct tcp_pcb *pcb;
    DEBUG_MSG_P(PSTR("Active PCB states:\n"));
    for (pcb = tcp_active_pcbs; pcb != NULL; pcb = pcb->next) {
        _debug_tcp_print_pcb(pcb);
    }
    DEBUG_MSG_P(PSTR("TIME-WAIT PCB states:\n"));
    for (pcb = tcp_tw_pcbs; pcb != NULL; pcb = pcb->next) {
        _debug_tcp_print_pcb(pcb);
    }
    DEBUG_MSG_P(PSTR("BOUND PCB states:\n"));
    for (pcb = tcp_bound_pcbs; pcb != NULL; pcb = pcb->next) {
        _debug_tcp_print_pcb(pcb);
    }

}

// TODO: export per-list?
void
debug_tcp_remove_all(void) {
  _debug_tcp_remove(tcp_bound_pcbs);
  _debug_tcp_remove(tcp_active_pcbs);
  _debug_tcp_remove(tcp_tw_pcbs);
}
