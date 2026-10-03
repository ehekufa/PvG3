/* Desktop-only transport: screenshot/simulation tests never contact Firebase.
 * Native network integration tests provide their own in-memory REST server. */
#include "online_net.h"
#include <stdio.h>
#include <string.h>

static int fake_levels_enabled;
void fake_online_set_levels_enabled(int enabled) {fake_levels_enabled = !!enabled;}

static const char test_level_index[] =
    "{\"104\":{\"id\":\"104\",\"title\":\"Невероятное приключение через тайный мост к финишу\","
    "\"description\":\"Найди скрытый мост и монеты, затем доберись до финиша по платформам.\",\"updatedAt\":1}}";
static const char test_level[] =
    "{\"format\":\"PVG3-PUBLISHED-LEVEL\",\"version\":1,\"id\":\"104\","
    "\"title\":\"Невероятное приключение через тайный мост к финишу\","
    "\"description\":\"Найди скрытый мост и монеты, затем доберись до финиша по платформам.\","
    "\"project\":{\"format\":\"PVG3-MAKER\","
    "\"version\":1,\"title\":\"Невероятное приключение через тайный мост к финишу\",\"description\":\"Найди скрытый мост и монеты, затем доберись до финиша по платформам.\",\"levelId\":\"104\","
    "\"templateId\":\"classic\",\"width\":16,\"height\":10,\"objects\":["
    "{\"id\":1,\"type\":\"ground\",\"name\":\"Ground\",\"x\":0,\"y\":9,"
    "\"w\":16,\"h\":1,\"angle\":0,\"color\":\"#64844c\",\"number\":0,\"visible\":true},"
    "{\"id\":2,\"type\":\"player\",\"name\":\"Start\",\"x\":1,\"y\":8.12,"
    "\"w\":0.65,\"h\":0.85,\"angle\":0,\"color\":\"#7db9dd\",\"number\":0,\"visible\":true},"
    "{\"id\":3,\"type\":\"goal\",\"name\":\"Goal\",\"x\":14.7,\"y\":7.8,"
    "\"w\":0.8,\"h\":1.2,\"angle\":0,\"color\":\"#e8cf77\",\"number\":0,\"visible\":true}]},\"updatedAt\":1}";

int on_http_request(const char *path, const char *method, const char *body,
                    const char *if_match, char *response, size_t response_cap) {
    (void)body;(void)if_match;
    if (!strcmp(path, "rooms.json") && !strcmp(method, "GET") && response_cap >= 5) {
        snprintf(response, response_cap, "null");
        return 200;
    }
    if (fake_levels_enabled && !strcmp(method, "GET") && response &&
        !strcmp(path, "levels-index.json") && response_cap > sizeof test_level_index) {
        memcpy(response, test_level_index, sizeof test_level_index);
        return 200;
    }
    if (fake_levels_enabled && !strcmp(method, "GET") && response &&
        !strcmp(path, "levels/104.json") && response_cap > sizeof test_level) {
        memcpy(response, test_level, sizeof test_level);
        return 200;
    }
    if (response && response_cap) response[0] = 0;
    return -1;
}
