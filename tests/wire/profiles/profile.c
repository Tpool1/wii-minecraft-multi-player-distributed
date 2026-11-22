/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <winsock2.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lan_transport.h"
#include "lan_frame.h"
#include "lan_rpc.h"

enum { COUNT = 200, PROBE = 32, RPG_BYTES = 9 };
void wire_fixture_registry(bool alternate_material, bool alternate_texture);
static bool host, linked, done, failed, stall_used;
static const char *profile;
static unsigned sent, received, connection_count;
static uint64_t joined_us, first_us, last_us, frequency, frame_due;
static unsigned frame_left, stall_started, holds30, holds50, holds70, holds100;
static uint32_t queued_peak, sent_at[COUNT], received_us[COUNT], return_us[COUNT], roundtrip_us[COUNT];
static uint32_t tx_hash = 2166136261u, rx_hash = 2166136261u;
static unsigned duration_ms, wire_sends, envelope_errors;
static uint64_t now_us(void) {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return (uint64_t)(q.QuadPart / (int64_t)frequency) * 1000000
        + (uint64_t)(q.QuadPart % (int64_t)frequency) * 1000000 / frequency;
}
uint32_t lan_test_clock(void) { return (uint32_t)(now_us()/1000); }
static uint32_t update(uint32_t hash, const uint8_t *data, size_t n) {
    while(n--) hash = (hash ^ *data++) * 16777619u;
    return hash;
}
static void trailer(uint8_t* data) {
    uint32_t hash = update(2166136261u, data, RPG_BYTES);
    for(unsigned i = RPG_BYTES; i < PROBE; ++i) {
        hash = hash * 1664525u + 1013904223u;
        data[i] = (uint8_t)(hash >> 24);
    }
}
static size_t extend(uint8_t* data, size_t capacity, size_t n) {
    if(n != RPG_BYTES || capacity < PROBE) return 0;
    trailer(data);
    tx_hash = update(tx_hash, data, PROBE);
    return PROBE;
}
static bool verified(const uint8_t* data, size_t n) {
    uint8_t expected[PROBE];
    if(n != PROBE) { ++envelope_errors; return false; }
    memcpy(expected, data, RPG_BYTES);
    trailer(expected);
    if(memcmp(expected, data, PROBE)) { ++envelope_errors; return false; }
    rx_hash = update(rx_hash, data, PROBE);
    return true;
}
size_t probe_encode_state(uint8_t* p, size_t n, const struct client_rpc* r) {
    return extend(p,n,lan_encode_state(p,n,r));
}
size_t probe_encode_command(uint8_t* p, size_t n, const struct server_rpc* r) {
    return extend(p,n,lan_encode_command(p,n,r));
}
bool probe_decode_state(const uint8_t* p, size_t n, struct client_rpc* r) {
    return verified(p,n) && lan_decode_state(p,RPG_BYTES,r);
}
bool probe_decode_command(const uint8_t* p, size_t n, struct server_rpc* r) {
    return verified(p,n) && lan_decode_command(p,RPG_BYTES,r);
}
static unsigned hold_ms(void) {
    if(!strcmp(profile,"constant50")) { ++holds50; return 50; }
    if(!strcmp(profile,"constant100")) { ++holds100; return 100; }
    if(!strcmp(profile,"jitter")) {
        if(wire_sends % 2) { ++holds70; return 70; }
        ++holds30; return 30;
    }
    if(!strcmp(profile,"stall") && wire_sends == COUNT/2 && !stall_used) {
        stall_used = true; stall_started = wire_sends+1;
        return 2000;
    }
    return 0;
}
int lan_test_send(SOCKET fd, const char* data, size_t n, int flags) {
    /* The production writer can coalesce multiple records. Limit each actual
     * send to the first frame and retain its deadline across short writes. */
    if(!frame_left) {
        if(n < LAN_HEADER) { failed = true; return SOCKET_ERROR; }
        uint32_t len;
        memcpy(&len,data+8,sizeof len);
        frame_left = LAN_HEADER + ntohl(len);
        if(frame_left > n) { failed=true; return SOCKET_ERROR; }
        unsigned hold = 0;
        if(host && (unsigned char)data[5] == LAN_STATE) {
            hold = hold_ms();
            ++wire_sends;
        }
        frame_due = now_us() + hold * 1000u;
    }
    if(now_us() < frame_due) {
        WSASetLastError(WSAEWOULDBLOCK);
        return SOCKET_ERROR;
    }
    int r = send(fd,data,(int)frame_left,flags);
    if(r > 0) frame_left -= (unsigned)r;
    return r;
}
int lan_test_recv(SOCKET fd, char* data, size_t n, int flags) {
    return recv(fd,data,(int)n,flags);
}
static void link_change(bool up, void* user) {
    (void)user;
    if(up && !linked) {
        ++connection_count;
        linked = true;
        joined_us = now_us();
        printf("JOIN profile=%s role=host duration_ms=%u\n",profile,duration_ms);
    } else failed = true;
}
static void reply(struct server_rpc* r,void* user) {
    (void)user;
    if(received >= COUNT || r->type != SRPC_ENTITY_ATTACK || r->action_seq != received+1) {
        failed = true; return;
    }
    uint32_t finished = (uint32_t)now_us(), echoed = r->payload.entity_attack.entity_id;
    unsigned i = received++;
    received_us[i] = echoed-sent_at[i];
    return_us[i] = finished-echoed;
    roundtrip_us[i] = finished-sent_at[i];
    printf("sample,%u,%u,%u,%u\n",i+1,received_us[i],return_us[i],roundtrip_us[i]);
}
static void state(struct client_rpc* r) {
    if(received >= COUNT || r->type != CRPC_TIME_SET || (r->payload.time_set >> 32) != received+1) { failed=true; return; }
    ++received;
    if(received == 1) { ++connection_count; first_us = now_us(); }
    last_us = now_us();
    struct server_rpc ack = { .type=SRPC_ENTITY_ATTACK, .action_seq=received,
                              .payload.entity_attack.entity_id=(uint32_t)last_us };
    if(!lan_peer_send(&ack)) failed=true; else ++sent;
}
static void peer_lost(void) {
    if(received==COUNT) done=true; else failed=true;
}
static int order(const void* a, const void* b) {
    uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;
    return (x>y)-(x<y);
}
static void summarize(const char* name, uint32_t* data) {
    qsort(data,COUNT,sizeof *data,order);
    printf("latency_us,%s,min=%u,p50=%u,p95=%u,p99=%u,max=%u\n",name,data[0],data[99],data[189],data[197],data[199]);
}
int main(int argc, char** argv) {
    if(argc != 5) return 2;
    host = !strcmp(argv[1],"host");
    profile=argv[3]; duration_ms=(unsigned)atoi(argv[4]);
    LARGE_INTEGER q;
    QueryPerformanceFrequency(&q);
    frequency=(uint64_t)q.QuadPart;
    setvbuf(stdout,NULL,_IOLBF,0);
    wire_fixture_registry(false,!host);
    if(!lan_configure(host?LAN_HOST:LAN_PEER,"127.0.0.1","127.0.0.1",(uint16_t)atoi(argv[2]))) return 3;
    const uint64_t start = now_us();
    while(!failed && !done && now_us()-start < ((uint64_t)duration_ms+14000)*1000) {
        if(host) {
            lan_host_pump(link_change,reply,NULL);
            uint64_t elapsed = linked ? now_us()-joined_us : 0;
            if(linked && sent < COUNT && elapsed >= (uint64_t)sent * duration_ms * 1000 / COUNT) {
                uint32_t stamp=(uint32_t)now_us();
                sent_at[sent]=stamp;
                struct client_rpc r={.type=CRPC_TIME_SET,.payload.time_set=((uint64_t)(sent+1)<<32)|stamp};
                if(!lan_host_send(&r)) failed=true;
                ++sent;
            }
            size_t bytes=lan_host_queued_bytes();
            if(bytes>queued_peak) queued_peak=(uint32_t)bytes;
            if(linked && received==COUNT && elapsed >= (uint64_t)duration_ms*1000) done=true;
        } else lan_peer_pump(state,peer_lost);
        Sleep(8);
    }
    bool pass=done && !failed && sent==COUNT && received==COUNT && !envelope_errors && connection_count==1;
    if(host) {
        if(!strcmp(profile,"constant50")) pass &= holds50==COUNT;
        if(!strcmp(profile,"constant100")) pass &= holds100==COUNT;
        if(!strcmp(profile,"jitter")) pass &= holds30==COUNT/2 && holds70==COUNT/2;
        if(!strcmp(profile,"stall")) pass &= stall_used && received_us[COUNT/2]>=2000000;
        if(pass) {
            summarize("forward",received_us);
            summarize("reverse",return_us);
            summarize("roundtrip",roundtrip_us);
        }
        printf("gates,sent=%u,30ms=%u,50ms=%u,70ms=%u,100ms=%u,stall_seq=%u,peak_queue_bytes=%u\n",
               wire_sends,holds30,holds50,holds70,holds100,stall_started,queued_peak);
    }
    printf("result,%s,role=%s,profile=%s,count_out=%u,count_in=%u,links=%u,tx_fnv=%08x,rx_fnv=%08x,"
           "connected_ms=%llu,peer_sample_span_ms=%llu,envelope_errors=%u\n",pass?"PASS":"FAIL",
           host?"host":"peer",profile,sent,received,connection_count,tx_hash,rx_hash,
           (unsigned long long)(joined_us?(now_us()-joined_us)/1000:0),
           (unsigned long long)(first_us?(last_us-first_us)/1000:0),envelope_errors);
    return pass?0:1;
}
