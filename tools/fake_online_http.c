/* Desktop-only transport: screenshot/simulation tests never contact Firebase.
 * Native network integration tests provide their own in-memory REST server. */
#include "online_net.h"
#include <stdio.h>
#include <string.h>

int on_http_request(const char *path, const char *method, const char *body,
                    const char *if_match, char *response, size_t response_cap) {
    (void)body;(void)if_match;
    if (!strcmp(path, "rooms.json") && !strcmp(method, "GET") && response_cap >= 5) {
        snprintf(response, response_cap, "null");
        return 200;
    }
    if (response && response_cap) response[0] = 0;
    return -1;
}
