/* Asynchronous Firebase REST room client for the native game. This is not a
 * browser launcher: the existing GLES game handles every touch and pixel.
 * Room/command/state JSON is shared with the optional HTML client. */
#define _POSIX_C_SOURCE 200809L
#include "online_net.h"

#include <pthread.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#define NET_CLOCK_REALTIME 0
#define NET_CLOCK_MONOTONIC 1
#else
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#define NET_CLOCK_REALTIME CLOCK_REALTIME
#define NET_CLOCK_MONOTONIC CLOCK_MONOTONIC
#endif

#define RESPONSE_BASE_CAP (1024u * 1024u + 1u)
#define RESPONSE_CAP ON_LEVEL_JSON_CAP

enum { A_NONE, A_CREATE, A_JOIN, A_ROLE, A_LEAVE, A_REFRESH };
enum { T_IDLE, T_CREATE, T_JOIN, T_ROLE, T_LEAVE, T_REFRESH,
       T_LIST, T_POLL, T_HEARTBEAT, T_COMMAND, T_PUBLISH,
       T_LEVEL_LIST, T_LEVEL_GET, T_LEVEL_PUBLISH };

typedef struct {
    OnNetView view;
    pthread_t worker;
    int thread_started, stopping;
    unsigned generation;
    int action, action_map, action_role, leave_slot;
    char action_id[ON_ROOM_ID_SIZE], leave_id[ON_ROOM_ID_SIZE];
    char player_id[ON_PLAYER_ID_SIZE];
    int queued_command;
    OnCommand command;
    OnMatch published;
    unsigned pub_revision, sent_revision;
    int next_seq;
    int64_t next_list, next_room, next_ping, next_publish;
    int level_list_requested, level_fetch_requested;
    unsigned level_generation;
    char level_fetch_id[ON_LEVEL_ID_SIZE];
    OnPublishedLevel loaded_level;
    char *response;
    size_t response_cap;
    int level_publish_requested;
    unsigned level_publish_generation;
    OnPublishedLevel level_to_publish;
} Net;
static Net net;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static int64_t clock_ms(int clock_kind) {
#ifdef _WIN32
    if (clock_kind == NET_CLOCK_REALTIME) {
        FILETIME file_time;
        ULARGE_INTEGER ticks;
        GetSystemTimeAsFileTime(&file_time);
        ticks.LowPart = file_time.dwLowDateTime;
        ticks.HighPart = file_time.dwHighDateTime;
        return (int64_t)(ticks.QuadPart / 10000ULL) - 11644473600000LL;
    }
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(clock_kind, &ts)) return 0;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}
static void random_bytes(unsigned char *bytes, size_t n) {
    size_t got = 0;
#ifdef _WIN32
    if (n <= ULONG_MAX &&
        BCryptGenRandom(NULL, bytes, (ULONG)n,
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0)
        got = n;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        while (got < n) {
            ssize_t count = read(fd, bytes + got, n - got);
            if (count <= 0) break;
            got += (size_t)count;
        }
        close(fd);
    }
#endif
    uint64_t seed = (uint64_t)clock_ms(NET_CLOCK_REALTIME) ^
                    ((uint64_t)clock_ms(NET_CLOCK_MONOTONIC) << 19) ^ (uintptr_t)bytes;
    for (size_t i = got; i < n; i++) {
        seed ^= seed << 13;seed ^= seed >> 7;seed ^= seed << 17;
        bytes[i] = (unsigned char)seed;
    }
}
static void make_id(char id[ON_PLAYER_ID_SIZE]) {
    const char hex[] = "0123456789abcdef";
    unsigned char bytes[16];random_bytes(bytes, sizeof bytes);
    for (int i = 0; i < 16; i++) {
        id[i * 2] = hex[bytes[i] >> 4];
        id[i * 2 + 1] = hex[bytes[i] & 15];
    }
    id[32] = 0;
}
static void room_code(char id[ON_ROOM_ID_SIZE]) {
    const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    unsigned char bytes[6];random_bytes(bytes, sizeof bytes);
    for (int i = 0; i < 6; i++) id[i] = alphabet[bytes[i] & 31];
    id[6] = 0;
}
static void level_code(char id[ON_LEVEL_ID_SIZE]) {
    unsigned char bytes[4];random_bytes(bytes, sizeof bytes);
    unsigned value = ((unsigned)bytes[0] << 24) | ((unsigned)bytes[1] << 16) |
                     ((unsigned)bytes[2] << 8) | bytes[3];
    snprintf(id, ON_LEVEL_ID_SIZE, "%u", value % 999999u + 1u);
}
static void message(unsigned gen, const char *text, int connected) {
    pthread_mutex_lock(&mu);
    if (gen == net.generation) {
        net.view.busy = 0;
        net.view.connected = connected;
        snprintf(net.view.notice, sizeof(net.view.notice), "%s", text);
    }
    pthread_mutex_unlock(&mu);
}
static const char *http_error(int code) {
    if (code == 401 || code == 403) return "FIREBASE ЗАПРЕТИЛ ДОСТУП К /ROOMS";
    if (code == 412) return "КОМНАТА ЗАНЯТА. ОБНОВИ СПИСОК.";
    if (code == -2) return "СЛИШКОМ МНОГО КОМНАТ. ОТВЕТ БОЛЬШОЙ.";
    if (code <= 0) return "НЕТ СВЯЗИ С FIREBASE. ПРОВЕРЬ ИНТЕРНЕТ.";
    return "ОШИБКА FIREBASE. ПОПРОБУЙ ЕЩЁ РАЗ.";
}
static void compact_response(void) {
    if (!net.response || net.response_cap <= RESPONSE_BASE_CAP) return;
    char *smaller = (char *)realloc(net.response, RESPONSE_BASE_CAP);
    if (smaller) {net.response = smaller;net.response_cap = RESPONSE_BASE_CAP;}
}
static int request(const char *path, const char *method, const char *body,
                   const char *if_match) {
    if (!net.response) return -1;
    int large_level = path && !strncmp(path, "levels/", 7);
    int large_level_get = large_level && method && !strcmp(method, "GET");
    size_t needed = large_level_get ? RESPONSE_CAP : RESPONSE_BASE_CAP;
    if (net.response_cap < needed) {
        char *larger = (char *)realloc(net.response, needed);
        if (!larger) return -1;
        net.response = larger;net.response_cap = needed;
    }
    net.response[0] = 0;
    int code = on_http_request(path, method, body, if_match, net.response,
                               net.response_cap);
    /* PUT responses are only status-checked; don't retain their echoed level. */
    if (large_level && method && strcmp(method, "GET")) compact_response();
    return code;
}
#ifndef ON_NET_MANUAL
static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        int stop = net.stopping;
        pthread_mutex_unlock(&mu);
        if (stop) break;
        on_net_pump_once();
#ifdef _WIN32
        Sleep(80);
#else
        struct timespec wait = {0, 80 * 1000000L};
        nanosleep(&wait, NULL);
#endif
    }
    return NULL;
}
#endif
static int ensure_transport_locked(void) {
    if (!net.player_id[0]) make_id(net.player_id);
    if (!net.response) {
        net.response = (char *)malloc(RESPONSE_BASE_CAP);
        if (net.response) net.response_cap = RESPONSE_BASE_CAP;
    }
    if (!net.response) {
        snprintf(net.view.notice, sizeof(net.view.notice), "НЕ ХВАТИЛО ПАМЯТИ ДЛЯ СЕТИ");
        return 0;
    }
#ifndef ON_NET_MANUAL
    if (!net.thread_started) {
        net.stopping = 0;
        if (pthread_create(&net.worker, NULL, worker, NULL) == 0) net.thread_started = 1;
        else {
            snprintf(net.view.notice, sizeof(net.view.notice), "НЕ УДАЛОСЬ ЗАПУСТИТЬ СЕТЬ");
            return 0;
        }
    }
#endif
    return 1;
}
void on_net_open(void) {
    pthread_mutex_lock(&mu);
    if (!ensure_transport_locked()) {
        net.view.mode = ON_NET_ROOMS;
        pthread_mutex_unlock(&mu);
        return;
    }
    if (net.view.mode == ON_NET_CLOSED) {
        net.view.mode = ON_NET_ROOMS;
        net.view.notice[0] = 0;
        net.view.connected = 0;
        net.next_list = 0;
    }
    pthread_mutex_unlock(&mu);
}
void on_net_close(void) {
    pthread_mutex_lock(&mu);
    net.generation++;
    net.view.mode = ON_NET_CLOSED;
    net.view.slot = ON_SLOT_NONE;
    net.view.room_id[0] = 0;
    net.view.guest_id[0] = 0;
    net.view.busy = net.view.pending = 0;
    net.queued_command = 0;
    if (net.action != A_LEAVE) net.action = A_NONE;
    pthread_mutex_unlock(&mu);
}
void on_net_shutdown(void) {
    pthread_mutex_lock(&mu);
    int started = net.thread_started;
    net.stopping = 1;
    pthread_mutex_unlock(&mu);
    if (started) pthread_join(net.worker, NULL);
    pthread_mutex_lock(&mu);
    net.thread_started = 0;
    net.stopping = 0;
    net.view.mode = ON_NET_CLOSED;
    net.view.slot = ON_SLOT_NONE;
    net.view.room_id[0] = 0;
    net.queued_command = 0;
    net.action = A_NONE;
    net.level_publish_requested = 0;
    net.view.level_publish_busy = 0;
    net.generation++;
    free(net.response);net.response = NULL;net.response_cap = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_view(OnNetView *out) {
    if (!out) return;
    pthread_mutex_lock(&mu);
    *out = net.view;
    pthread_mutex_unlock(&mu);
}
int on_net_take_loaded_level(OnPublishedLevel *out) {
    if (!out) return 0;
    pthread_mutex_lock(&mu);
    if (!net.view.level_loaded) {
        pthread_mutex_unlock(&mu);return 0;
    }
    *out = net.loaded_level;
    net.view.level_loaded = 0;
    pthread_mutex_unlock(&mu);
    return 1;
}
void on_net_refresh(void) {
    pthread_mutex_lock(&mu);
    if (net.view.mode == ON_NET_ROOMS) net.next_list = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_levels_refresh(void) {
    pthread_mutex_lock(&mu);
    net.level_list_requested = 1;
    net.level_fetch_requested = 0;
    net.level_generation++;
    net.view.levels_busy = 1;
    net.view.level_loaded = 0;
    net.view.levels_notice[0] = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_level_fetch(const char *id) {
    if (!on_protocol_valid_level_id(id)) return;
    pthread_mutex_lock(&mu);
    net.level_fetch_requested = 1;
    net.level_list_requested = 0;
    net.level_generation++;
    snprintf(net.level_fetch_id, sizeof net.level_fetch_id, "%s", id);
    net.view.levels_busy = 1;
    net.view.level_loaded = 0;
    net.view.levels_notice[0] = 0;
    pthread_mutex_unlock(&mu);
}
int on_net_level_publish(const OnPublishedLevel *level) {
    if (!level) return 0;
    pthread_mutex_lock(&mu);
    if (net.view.level_publish_busy || net.level_publish_requested) {
        pthread_mutex_unlock(&mu);return 0;
    }
    if (!ensure_transport_locked()) {
        net.view.level_publish_busy = 0;
        snprintf(net.view.level_publish_notice, sizeof net.view.level_publish_notice,
                 "%s", net.view.notice[0] ? net.view.notice :
                 "НЕ УДАЛОСЬ ЗАПУСТИТЬ ПУБЛИКАЦИЮ");
        pthread_mutex_unlock(&mu);return 0;
    }
    net.level_to_publish = *level;
    net.level_publish_requested = 1;
    net.level_publish_generation++;
    net.view.level_publish_busy = 1;
    net.view.level_publish_id[0] = 0;
    net.view.level_publish_notice[0] = 0;
    pthread_mutex_unlock(&mu);return 1;
}
void on_net_level_cancel(void) {
    pthread_mutex_lock(&mu);
    net.level_fetch_requested = net.level_list_requested = 0;
    net.level_generation++;
    net.view.levels_busy = 0;
    net.view.level_loaded = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_level_consumed(void) {
    pthread_mutex_lock(&mu);
    net.view.level_loaded = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_create(int map) {
    pthread_mutex_lock(&mu);
    if (net.view.mode == ON_NET_ROOMS && !net.view.busy) {
        net.action = A_CREATE;net.action_map = map == 5 ? 5 : 1;
        net.view.busy = 1;net.view.notice[0] = 0;
        net.generation++;
    }
    pthread_mutex_unlock(&mu);
}
void on_net_join(const char *id) {
    if (!on_protocol_valid_room_id(id)) return;
    pthread_mutex_lock(&mu);
    if (net.view.mode == ON_NET_ROOMS && !net.view.busy) {
        net.action = A_JOIN;
        memcpy(net.action_id, id, ON_ROOM_ID_SIZE);
        net.view.busy = 1;net.view.notice[0] = 0;
        net.generation++;
    }
    pthread_mutex_unlock(&mu);
}
void on_net_choose(int role) {
    if (role != ON_ROLE_PLANTS && role != ON_ROLE_ZOMBIES) return;
    pthread_mutex_lock(&mu);
    if (net.view.mode == ON_NET_LOBBY && !net.view.busy &&
        ((net.view.slot == ON_SLOT_HOST && net.view.guest_role != role) ||
         (net.view.slot == ON_SLOT_GUEST && net.view.host_role != role))) {
        net.action = A_ROLE;net.action_role = role;net.view.busy = 1;
    }
    pthread_mutex_unlock(&mu);
}
void on_net_leave(void) {
    pthread_mutex_lock(&mu);
    if (net.view.slot && net.view.room_id[0]) {
        memcpy(net.leave_id, net.view.room_id, sizeof(net.leave_id));
        net.leave_slot = net.view.slot;
        net.action = A_LEAVE;
    } else net.action = A_REFRESH;
    net.generation++;
    net.view.mode = ON_NET_ROOMS;
    net.view.slot = ON_SLOT_NONE;
    net.view.room_id[0] = 0;
    net.view.map = 0;
    net.view.host_role = net.view.guest_role = 0;
    net.view.has_state = net.view.has_command = 0;
    net.view.pending = net.queued_command = 0;
    net.view.busy = 0;
    net.view.notice[0] = 0;
    net.pub_revision = net.sent_revision = 0;
    net.next_seq = 0;
    net.next_list = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_publish(const OnMatch *match) {
    if (!on_match_valid(match)) return;
    pthread_mutex_lock(&mu);
    if (net.view.mode == ON_NET_LOBBY && net.view.slot == ON_SLOT_HOST) {
        net.published = *match;
        net.pub_revision++;
    }
    pthread_mutex_unlock(&mu);
}
int on_net_send(OnCommand command) {
    pthread_mutex_lock(&mu);
    int good = net.view.mode == ON_NET_LOBBY && net.view.slot == ON_SLOT_GUEST &&
               net.view.has_state && !net.view.state.winner &&
               !net.view.pending && !net.queued_command &&
               net.next_seq < 2000000000;
    if (good) {
        net.next_seq++;
        command.seq = net.next_seq;
        net.command = command;
        net.queued_command = net.view.pending = 1;
    }
    pthread_mutex_unlock(&mu);
    return good;
}

static void finish_level_publish(unsigned publish_gen, const char *id,
                                 const char *notice) {
    pthread_mutex_lock(&mu);
    if (publish_gen == net.level_publish_generation) {
        net.view.level_publish_busy = 0;
        snprintf(net.view.level_publish_id, sizeof net.view.level_publish_id, "%s", id ? id : "");
        snprintf(net.view.level_publish_notice, sizeof net.view.level_publish_notice,
                 "%s", notice ? notice : "");
    }
    pthread_mutex_unlock(&mu);
}
static void publish_level_record(unsigned publish_gen, OnPublishedLevel *record) {
    char *body = NULL;
    size_t body_cap = 0;
    int code = 0;char saved_id[ON_LEVEL_ID_SIZE] = {0};
    for (int attempt = 0; attempt < 5; ++attempt) {
        level_code(record->id);
        size_t body_size = on_protocol_published_level_json(record, NULL, 0);
        if (!body_size || body_size >= RESPONSE_CAP) {
            free(body);
            finish_level_publish(publish_gen, "", "ЧЕРНОВИК ПОВРЕЖДЁН ИЛИ НЕ ПОДДЕРЖИВАЕТСЯ");
            return;
        }
        if (body_cap < body_size + 1) {
            char *larger = (char *)realloc(body, body_size + 1);
            if (!larger) {
                free(body);
                finish_level_publish(publish_gen, "", "НЕ ХВАТИЛО ПАМЯТИ ДЛЯ ПУБЛИКАЦИИ");
                return;
            }
            body = larger;body_cap = body_size + 1;
        }
        if (on_protocol_published_level_json(record, body, body_cap) != body_size) {
            free(body);
            finish_level_publish(publish_gen, "", "ЧЕРНОВИК ПОВРЕЖДЁН ИЛИ НЕ ПОДДЕРЖИВАЕТСЯ");
            return;
        }
        char path[48];snprintf(path, sizeof path, "levels/%s.json", record->id);
        code = request(path, "PUT", body, "null_etag");
        if (code != 412) break;
    }
    if (code == 412) {
        free(body);
        finish_level_publish(publish_gen, "", "НЕ УДАЛОСЬ НАЙТИ СВОБОДНЫЙ ID УРОВНЯ");
        return;
    }
    if (code != 200) {
        free(body);
        finish_level_publish(publish_gen, "", code == 401 || code == 403 ?
            "FIREBASE ЗАПРЕТИЛ ЗАПИСЬ В /LEVELS. ПРОВЕРЬ ПРАВИЛА." :
            "НЕ УДАЛОСЬ ОПУБЛИКОВАТЬ УРОВЕНЬ. ПРОВЕРЬ ИНТЕРНЕТ.");
        return;
    }
    snprintf(saved_id, sizeof saved_id, "%s", record->id);
    char summary[ON_LEVEL_TITLE_SIZE + ON_LEVEL_DESCRIPTION_SIZE + 128];
    if (!on_protocol_level_summary_json(record, summary, sizeof summary,
                                        clock_ms(NET_CLOCK_REALTIME))) {
        free(body);
        finish_level_publish(publish_gen, saved_id,
            "УРОВЕНЬ ЗАПИСАН, НО НЕ УДАЛОСЬ ПОДГОТОВИТЬ КАТАЛОГ.");
        return;
    }
    char index_path[56];snprintf(index_path, sizeof index_path,
                                 "levels-index/%s.json", saved_id);
    code = request(index_path, "PUT", summary, "null_etag");
    free(body);
    if (code != 200) {
        finish_level_publish(publish_gen, saved_id, code == 401 || code == 403 ?
            "УРОВЕНЬ ЗАПИСАН, НО FIREBASE ЗАПРЕТИЛ /LEVELS-INDEX. ПРОВЕРЬ ПРАВИЛА." :
            "УРОВЕНЬ ЗАПИСАН, НО НЕ УДАЛОСЬ ДОБАВИТЬ ЕГО В КАТАЛОГ.");
        return;
    }
    char notice[144];snprintf(notice, sizeof notice,
                              "УРОВЕНЬ ОПУБЛИКОВАН · ID %s", saved_id);
    finish_level_publish(publish_gen, saved_id, notice);
    pthread_mutex_lock(&mu);
    if (net.view.mode != ON_NET_CLOSED) {
        net.level_list_requested = 1;net.view.levels_busy = 1;
        net.view.level_loaded = 0;net.view.levels_notice[0] = 0;
    }
    pthread_mutex_unlock(&mu);
}

void on_net_pump_once(void) {
    int task = T_IDLE, map = 1, role = 0, slot = 0;
    unsigned gen, level_gen, publish_gen;
    char id[ON_ROOM_ID_SIZE] = {0}, player[ON_PLAYER_ID_SIZE] = {0};
    char level_id[ON_LEVEL_ID_SIZE] = {0};
    OnCommand command = {0};
    OnMatch state;
    unsigned revision = 0;
    int64_t now = clock_ms(NET_CLOCK_MONOTONIC);
    pthread_mutex_lock(&mu);
    if (net.stopping || !net.response ||
        (net.view.mode == ON_NET_CLOSED && net.action != A_LEAVE &&
         !net.level_publish_requested && !net.level_fetch_requested &&
         !net.level_list_requested)) {
        pthread_mutex_unlock(&mu);return;
    }
    gen = net.generation;level_gen = net.level_generation;
    publish_gen = net.level_publish_generation;
    memcpy(player, net.player_id, sizeof(player));
    if (net.level_fetch_requested) {
        task = T_LEVEL_GET;
        memcpy(level_id, net.level_fetch_id, sizeof(level_id));
        net.level_fetch_requested = 0;
    } else if (net.level_list_requested) {
        task = T_LEVEL_LIST;
        net.level_list_requested = 0;
    } else if (net.level_publish_requested) {
        task = T_LEVEL_PUBLISH;net.level_publish_requested = 0;
    } else if (net.action) {
        task = net.action == A_CREATE ? T_CREATE : net.action == A_JOIN ? T_JOIN :
               net.action == A_ROLE ? T_ROLE : net.action == A_LEAVE ? T_LEAVE : T_REFRESH;
        net.action = A_NONE;
        map = net.action_map;role = net.action_role;
        memcpy(id, task == T_LEAVE ? net.leave_id :
                   task == T_ROLE ? net.view.room_id : net.action_id, sizeof(id));
        slot = task == T_LEAVE ? net.leave_slot : net.view.slot;
    } else if (net.view.mode == ON_NET_ROOMS) {
        if (now >= net.next_list) {task = T_LIST;net.next_list = now + 4000;}
    } else if (net.view.mode == ON_NET_LOBBY) {
        memcpy(id, net.view.room_id, sizeof(id));
        slot = net.view.slot;
        if (slot == ON_SLOT_GUEST && net.queued_command) {
            task = T_COMMAND;command = net.command;net.queued_command = 0;
        } else if (now >= net.next_ping) {
            task = T_HEARTBEAT;net.next_ping = now + 5000;
        } else if (now >= net.next_room) {
            task = T_POLL;net.next_room = now + 650;
        } else if (slot == ON_SLOT_HOST && net.pub_revision != net.sent_revision &&
                   now >= net.next_publish) {
            task = T_PUBLISH;state = net.published;revision = net.pub_revision;
            net.next_publish = now + 350;
        }
    }
    pthread_mutex_unlock(&mu);
    if (task == T_IDLE) return;
    if (task == T_LEVEL_PUBLISH) {
        /* The busy flag keeps this shared, large record immutable until done. */
        publish_level_record(publish_gen, &net.level_to_publish);return;
    }

    char path[96], body[256];
    int code;
    if (task == T_LEVEL_LIST) {
        code = request("levels-index.json", "GET", NULL, NULL);
        if (code != 200) {
            pthread_mutex_lock(&mu);
            if (level_gen == net.level_generation) {
                net.view.levels_busy = 0;
                snprintf(net.view.levels_notice, sizeof net.view.levels_notice,
                    code == 401 || code == 403 ?
                    "Firebase запретил чтение /levels-index. Проверь правила." :
                    "Не удалось получить каталог уровней. Проверь интернет.");
            }
            pthread_mutex_unlock(&mu);return;
        }
        OnPublishedLevelSummary levels[ON_LEVEL_LIST_CAP];
        int count = on_protocol_level_index(net.response, levels, ON_LEVEL_LIST_CAP);
        pthread_mutex_lock(&mu);
        if (level_gen == net.level_generation) {
            net.view.levels_busy = 0;
            net.view.level_loaded = 0;
            if (count < 0) {
                snprintf(net.view.levels_notice, sizeof net.view.levels_notice,
                         "Каталог уровней повреждён или слишком велик.");
            } else {
                memcpy(net.view.levels, levels, (size_t)count * sizeof levels[0]);
                net.view.level_count = count;net.view.levels_notice[0] = 0;
            }
        }
        pthread_mutex_unlock(&mu);return;
    }
    if (task == T_LEVEL_GET) {
        snprintf(path, sizeof path, "levels/%s.json", level_id);
        code = request(path, "GET", NULL, NULL);
        /* A fetch request clears level_loaded before this worker writes. The
         * main thread only copies this storage while holding mu after success. */
        int valid = code == 200 &&
            on_protocol_published_level(net.response, level_id, &net.loaded_level);
        pthread_mutex_lock(&mu);
        if (level_gen == net.level_generation) {
            net.view.levels_busy = 0;
            net.view.level_loaded = valid;
            if (valid) {
                snprintf(net.view.loaded_level_id, sizeof net.view.loaded_level_id,
                         "%s", net.loaded_level.id);
                snprintf(net.view.loaded_level_title, sizeof net.view.loaded_level_title,
                         "%s", net.loaded_level.title);
                net.view.levels_notice[0] = 0;
            } else {
                snprintf(net.view.levels_notice, sizeof net.view.levels_notice,
                    code == 401 || code == 403 ?
                    "Firebase запретил чтение уровня. Проверь правила /levels." :
                    code == 200 ? "Уровень не найден или его формат не поддерживается." :
                    "Не удалось загрузить уровень. Проверь интернет.");
            }
        }
        pthread_mutex_unlock(&mu);compact_response();return;
    }
    if (task == T_REFRESH) {on_net_refresh();return;}
    if (task == T_LIST) {
        code = request("rooms.json", "GET", NULL, NULL);
        if (code != 200) {message(gen, http_error(code), 0);return;}
        OnRoomSummary rooms[ON_ROOM_LIST_CAP];
        int count = on_protocol_rooms(net.response, rooms, ON_ROOM_LIST_CAP,
                                      clock_ms(NET_CLOCK_REALTIME));
        if (count < 0) {message(gen, "НЕ УДАЛОСЬ ПРОЧИТАТЬ СПИСОК КОМНАТ", 0);return;}
        pthread_mutex_lock(&mu);
        if (gen == net.generation && net.view.mode == ON_NET_ROOMS) {
            memcpy(net.view.rooms, rooms, (size_t)count * sizeof(rooms[0]));
            net.view.room_count = count;net.view.connected = 1;
            net.view.notice[0] = 0;
        }
        pthread_mutex_unlock(&mu);
        return;
    }
    if (task == T_CREATE) {
        int last = 0;
        for (int attempt = 0; attempt < 4; attempt++) {
            room_code(id);
            snprintf(path, sizeof path, "rooms/%s.json", id);
            snprintf(body, sizeof body,
                     "{\"version\":1,\"map\":%d,\"host\":{\"id\":\"%s\","
                     "\"ping\":%lld}}", map, player,
                     (long long)clock_ms(NET_CLOCK_REALTIME));
            code = request(path, "PUT", body, "null_etag");
            last = code;
            if (code != 412) break;
        }
        if (last != 200) {message(gen, http_error(last), 0);return;}
        pthread_mutex_lock(&mu);
        if (gen == net.generation && net.view.mode == ON_NET_ROOMS) {
            net.view.mode = ON_NET_LOBBY;net.view.slot = ON_SLOT_HOST;
            memcpy(net.view.room_id, id, sizeof(id));
            net.view.map = map;net.view.host_role = net.view.guest_role = 0;
            net.view.has_state = net.view.has_command = 0;
            net.view.pending = 0;net.view.busy = 0;net.view.connected = 1;
            net.view.notice[0] = 0;
            net.next_room = 0;net.next_ping = now + 5000;
        }
        pthread_mutex_unlock(&mu);
        return;
    }
    if (task == T_JOIN) {
        snprintf(path, sizeof path, "rooms/%s.json", id);
        code = request(path, "GET", NULL, NULL);
        OnRoomData r;
        if (code != 200) {message(gen, http_error(code), 0);return;}
        if (!on_protocol_room(net.response, &r) || !r.present ||
            r.guest_id[0] || r.has_state ||
            clock_ms(NET_CLOCK_REALTIME) - r.host_ping > 600000) {
            message(gen, "КОМНАТА ЗАКРЫТА ИЛИ УЖЕ ЗАНЯТА", 1);return;
        }
        snprintf(path, sizeof path, "rooms/%s/guest.json", id);
        snprintf(body, sizeof body, "{\"id\":\"%s\",\"ping\":%lld}",
                 player, (long long)clock_ms(NET_CLOCK_REALTIME));
        code = request(path, "PUT", body, "null_etag");
        if (code != 200) {message(gen, http_error(code), 0);return;}
        snprintf(path, sizeof path, "rooms/%s.json", id);
        code = request(path, "GET", NULL, NULL);
        if (code != 200 || !on_protocol_room(net.response, &r) || !r.present ||
            strcmp(r.guest_id, player)) {
            message(gen, "МЕСТО УЖЕ ЗАНЯЛ ДРУГОЙ ИГРОК", 0);return;
        }
        pthread_mutex_lock(&mu);
        if (gen == net.generation && net.view.mode == ON_NET_ROOMS) {
            net.view.mode = ON_NET_LOBBY;net.view.slot = ON_SLOT_GUEST;
            memcpy(net.view.room_id, id, sizeof(id));
            net.view.map = r.map;net.view.host_role = r.host_role;
            net.view.guest_role = r.guest_role;
            memcpy(net.view.guest_id, r.guest_id, sizeof(r.guest_id));
            net.view.has_state = r.has_state;
            if (r.has_state) net.view.state = r.state;
            net.view.busy = 0;net.view.connected = 1;
            net.view.notice[0] = 0;
            net.next_seq = r.has_state ? r.state.ack_guest : 0;
            net.next_room = 0;net.next_ping = now + 5000;
        }
        pthread_mutex_unlock(&mu);
        return;
    }
    if (task == T_LEAVE) {
        snprintf(path, sizeof path, "rooms/%s.json", id);
        code = request(path, "GET", NULL, NULL);
        OnRoomData r;
        if (code == 200 && on_protocol_room(net.response, &r) && r.present &&
            !strcmp(slot == ON_SLOT_HOST ? r.host_id : r.guest_id, player)) {
            snprintf(path, sizeof path, slot == ON_SLOT_HOST ?
                     "rooms/%s.json" : "rooms/%s/guest.json", id);
            (void)request(path, "DELETE", NULL, NULL);
        }
        on_net_refresh();
        return;
    }
    if (task == T_ROLE) {
        const char *side = role == ON_ROLE_PLANTS ? "\"plants\"" : "\"zombies\"";
        if (!id[0] || !slot) return;
        snprintf(path, sizeof path, "rooms/%s/%s/role.json", id,
                 slot == ON_SLOT_HOST ? "host" : "guest");
        code = request(path, "PUT", side, NULL);
        if (code != 200) {message(gen, http_error(code), 0);return;}
        pthread_mutex_lock(&mu);
        if (gen == net.generation && net.view.mode == ON_NET_LOBBY) {
            if (slot == ON_SLOT_HOST) net.view.host_role = role;
            else net.view.guest_role = role;
            net.view.busy = 0;net.view.connected = 1;
        }
        pthread_mutex_unlock(&mu);
        return;
    }
    if (task == T_COMMAND) {
        if (!on_protocol_command_json(&command, player, body, sizeof body)) {
            message(gen, "НЕ УДАЛОСЬ СОБРАТЬ ХОД", 0);return;
        }
        snprintf(path, sizeof path, "rooms/%s/command.json", id);
        code = request(path, "PUT", body, NULL);
        if (code != 200) {
            pthread_mutex_lock(&mu);
            if (gen == net.generation) net.view.pending = 0;
            pthread_mutex_unlock(&mu);
            message(gen, http_error(code), 0);
        }
        return;
    }
    if (task == T_PUBLISH) {
        char *json = (char *)malloc(ON_STATE_JSON_CAP);
        if (!json) {message(gen, "НЕ ХВАТИЛО ПАМЯТИ ДЛЯ БОЯ", 0);return;}
        size_t len = on_protocol_match_json(&state, json, ON_STATE_JSON_CAP);
        if (!len) {free(json);message(gen, "ОШИБКА СОСТОЯНИЯ БОЯ", 0);return;}
        snprintf(path, sizeof path, "rooms/%s/state.json", id);
        code = request(path, "PUT", json, NULL);
        free(json);
        pthread_mutex_lock(&mu);
        if (gen == net.generation && net.view.slot == ON_SLOT_HOST) {
            net.next_publish = clock_ms(NET_CLOCK_MONOTONIC) + 350;
            if (code == 200) {
                net.sent_revision = revision;
                net.view.connected = 1;
                net.view.notice[0] = 0;
            } else {
                net.view.connected = 0;
                snprintf(net.view.notice, sizeof(net.view.notice), "%s", http_error(code));
                net.next_publish = clock_ms(NET_CLOCK_MONOTONIC) + 1200;
            }
        }
        pthread_mutex_unlock(&mu);
        return;
    }
    if (task == T_HEARTBEAT) {
        snprintf(path, sizeof path, "rooms/%s/%s/ping.json", id,
                 slot == ON_SLOT_HOST ? "host" : "guest");
        snprintf(body, sizeof body, "%lld", (long long)clock_ms(NET_CLOCK_REALTIME));
        code = request(path, "PUT", body, NULL);
        if (code != 200) message(gen, http_error(code), 0);
        return;
    }
    if (task == T_POLL) {
        snprintf(path, sizeof path, "rooms/%s.json", id);
        code = request(path, "GET", NULL, NULL);
        pthread_mutex_lock(&mu);
        if (gen == net.generation) net.next_room = clock_ms(NET_CLOCK_MONOTONIC) + 650;
        pthread_mutex_unlock(&mu);
        if (code != 200) {message(gen, http_error(code), 0);return;}
        OnRoomData r;
        if (!on_protocol_room(net.response, &r)) {
            message(gen, "НЕВЕРНОЕ СОСТОЯНИЕ КОМНАТЫ", 0);return;
        }
        if (!r.present || strcmp(slot == ON_SLOT_HOST ? r.host_id : r.guest_id, player)) {
            pthread_mutex_lock(&mu);
            if (gen == net.generation) {
                net.view.mode = ON_NET_ROOMS;net.view.slot = ON_SLOT_NONE;
                net.view.room_id[0] = 0;net.view.has_state = 0;
                net.view.pending = 0;net.view.connected = 0;
                snprintf(net.view.notice, sizeof(net.view.notice),
                         "КОМНАТА ЗАКРЫЛАСЬ. ВЕРНУЛИСЬ К СПИСКУ.");
                net.generation++;net.next_list = 0;
            }
            pthread_mutex_unlock(&mu);
            return;
        }
        pthread_mutex_lock(&mu);
        if (gen == net.generation && net.view.mode == ON_NET_LOBBY) {
            net.view.map = r.map;
            net.view.host_role = r.host_role;
            net.view.guest_role = r.guest_role;
            memcpy(net.view.guest_id, r.guest_id, sizeof(r.guest_id));
            net.view.has_command = r.has_command && r.guest_id[0] &&
                !strcmp(r.command.player_id, r.guest_id);
            if (net.view.has_command) net.view.command = r.command;
            if (r.has_state && (slot == ON_SLOT_HOST ||
                !net.view.has_state || r.state.time >= net.view.state.time || r.state.winner)) {
                net.view.state = r.state;net.view.has_state = 1;
            }
            if (slot == ON_SLOT_GUEST && net.view.has_state &&
                net.view.state.ack_guest > net.next_seq) net.next_seq = net.view.state.ack_guest;
            if (slot == ON_SLOT_GUEST && net.view.pending && net.view.has_state &&
                net.view.state.ack_guest >= net.next_seq) net.view.pending = 0;
            net.view.connected = 1;net.view.notice[0] = 0;
        }
        pthread_mutex_unlock(&mu);
    }
}
