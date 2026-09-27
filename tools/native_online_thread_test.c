/* The production pthread loop actually fetches rooms without blocking UI.
 * No live requests: this transport only returns an empty Firebase tree. */
#define _POSIX_C_SOURCE 200809L
#include "online_net.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static atomic_int gets;
int on_http_request(const char *path, const char *method, const char *body,
                    const char *if_match, char *response, size_t cap) {
    (void)body;(void)if_match;
    if (!strcmp(path, "rooms.json") && !strcmp(method, "GET") && cap > 5) {
        atomic_fetch_add(&gets, 1);
        strcpy(response, "null");return 200;
    }
    return -1;
}
static void delay(void) {
    struct timespec ts = {.tv_sec=0,.tv_nsec=160000000};
    nanosleep(&ts, NULL);
}
int main(void) {
    on_net_open();delay();
    OnNetView v;on_net_view(&v);
    assert(v.mode == ON_NET_ROOMS && v.connected && atomic_load(&gets) >= 1);
    on_net_close();delay();
    on_net_open();on_net_refresh();delay();
    on_net_view(&v);
    assert(v.mode == ON_NET_ROOMS && v.connected && atomic_load(&gets) >= 2);
    on_net_shutdown();
    puts("Background native network worker opens, closes and restarts safely");
    return 0;
}
