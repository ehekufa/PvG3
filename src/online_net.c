/* Asynchronous Firebase REST room client for the native game. This is not a
 * browser launcher: the existing GLES game handles every touch and pixel.
 * Room/command/state JSON is shared with the optional HTML client. */
#define _POSIX_C_SOURCE 200809L
#include "online_net.h"

#include "online_account.h"
#include "preferences.h"

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
       T_LEVEL_LIST, T_LEVEL_GET, T_LEVEL_PUBLISH,
       T_SIGN_IN, T_CREATE_ACCOUNT, T_TOKEN_ROTATE,
       T_COMMENTS, T_COMMENT_POST, T_COMMENT_HIDE, T_BAN, T_OFFICIAL };
/* Account work queued for the worker thread. */
enum { ACCOUNT_NONE, ACCOUNT_SIGN_IN, ACCOUNT_CREATE, ACCOUNT_ROTATE };

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
    /* Accounts, comments and moderation. */
    int account_job;
    unsigned account_generation;
    char account_login[ON_LOGIN_SIZE];
    char account_password[ON_PASSWORD_SIZE];
    int comments_requested;
    char comments_level[ON_LEVEL_ID_SIZE];
    int comment_post_requested;
    char comment_post_level[ON_LEVEL_ID_SIZE];
    char comment_text[ON_COMMENT_TEXT_SIZE];
    int comment_hide_requested;
    char comment_hide_level[ON_LEVEL_ID_SIZE];
    char comment_hide_id[ON_COMMENT_ID_SIZE];
    int ban_requested, ban_value;
    char ban_login[ON_LOGIN_SIZE];
    char ban_reason[ON_REASON_SIZE];
    int official_requested, official_value;
    char official_level[ON_LEVEL_ID_SIZE];
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
void on_net_random_hex(char out[ON_TOKEN_SIZE]) {
    static const char digits[] = "0123456789abcdef";
    unsigned char bytes[32];
    random_bytes(bytes, sizeof bytes);
    for (int i = 0; i < 32; i++) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[64] = 0;
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
/* The saved session comes back on launch: only the nick, the stretched hash and
 * the token the database issued — never the password. */
static void load_saved_account_locked(void) {
    char login[ON_LOGIN_SIZE], token[ON_TOKEN_SIZE], hash[ON_HASH_SIZE];
    if (net.view.account.signed_in) return;
    if (!preferences_account_login(login, sizeof login)) return;
    if (!preferences_account_token(token, sizeof token)) return;
    if (!preferences_account_hash(hash, sizeof hash)) return;
    net.view.account.signed_in = 1;
    net.view.account.admin = preferences_account_admin();
    snprintf(net.view.account.login, sizeof net.view.account.login, "%s", login);
    snprintf(net.view.account.token, sizeof net.view.account.token, "%s", token);
    snprintf(net.view.account.hash, sizeof net.view.account.hash, "%s", hash);
}
static int ensure_transport_locked(void) {
    load_saved_account_locked();
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
    /* The signed-in account owns what it publishes: the record and the
     * catalog card then carry its login, and the rules stop strangers from
     * overwriting the level. Without a session the level stays anonymous and
     * behaves exactly like before. */
    if (net.view.account.signed_in && on_account_valid_login(net.view.account.login))
        snprintf(net.level_to_publish.author, sizeof net.level_to_publish.author,
                 "%s", net.view.account.login);
    else net.level_to_publish.author[0] = 0;
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
void on_net_account_sign_in(const char *login, const char *password, int create) {
    if (!ensure_transport_locked()) {
        pthread_mutex_lock(&mu);
        snprintf(net.view.account_notice, sizeof net.view.account_notice,
                 "%s", net.view.notice[0] ? net.view.notice :
                 "Не удалось запустить сеть.");
        net.view.account_busy = 0;
        pthread_mutex_unlock(&mu);
        return;
    }
    pthread_mutex_lock(&mu);
    if (net.account_job || net.view.account_busy) {
        pthread_mutex_unlock(&mu);return; /* one request at a time */
    }
    snprintf(net.account_login, sizeof net.account_login, "%s", login ? login : "");
    snprintf(net.account_password, sizeof net.account_password, "%s",
             password ? password : "");
    net.account_job = create ? ACCOUNT_CREATE : ACCOUNT_SIGN_IN;
    net.account_generation++;
    net.view.account_busy = 1;
    net.view.account_notice[0] = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_account_sign_out(void) {
    pthread_mutex_lock(&mu);
    memset(&net.view.account, 0, sizeof net.view.account);
    net.view.comment_count = 0;
    net.view.comment_level[0] = 0;
    net.account_generation++;
    net.view.account_busy = 0;
    snprintf(net.view.account_notice, sizeof net.view.account_notice, "%s",
             "Вы вышли из аккаунта.");
    pthread_mutex_unlock(&mu);
    preferences_set_account(NULL, NULL, NULL, 0);
}
void on_net_comments_load(const char *level_id) {
    if (!on_protocol_valid_level_id(level_id)) return;
    if (!ensure_transport_locked()) return;
    pthread_mutex_lock(&mu);
    net.comments_requested = 1;
    net.account_generation++;
    snprintf(net.comments_level, sizeof net.comments_level, "%s", level_id);
    net.view.comments_busy = 1;
    net.view.account_notice[0] = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_comment_post(const char *level_id, const char *text) {
    if (!on_protocol_valid_level_id(level_id)) return;
    if (!ensure_transport_locked()) return;
    pthread_mutex_lock(&mu);
    if (net.comment_post_requested || net.comment_hide_requested) {
        pthread_mutex_unlock(&mu);return;
    }
    net.comment_post_requested = 1;
    net.account_generation++;
    snprintf(net.comment_post_level, sizeof net.comment_post_level, "%s", level_id);
    snprintf(net.comment_text, sizeof net.comment_text, "%s", text ? text : "");
    net.view.comments_busy = 1;
    net.view.account_notice[0] = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_comment_hide(const char *level_id, const char *comment_id) {
    if (!on_protocol_valid_level_id(level_id) || !comment_id || !comment_id[0]) return;
    if (!ensure_transport_locked()) return;
    pthread_mutex_lock(&mu);
    if (net.comment_post_requested || net.comment_hide_requested) {
        pthread_mutex_unlock(&mu);return;
    }
    net.comment_hide_requested = 1;
    net.account_generation++;
    snprintf(net.comment_hide_level, sizeof net.comment_hide_level, "%s", level_id);
    snprintf(net.comment_hide_id, sizeof net.comment_hide_id, "%s", comment_id);
    net.view.comments_busy = 1;
    net.view.account_notice[0] = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_account_ban(const char *login, const char *reason, int banned) {
    if (!ensure_transport_locked()) return;
    pthread_mutex_lock(&mu);
    if (net.ban_requested) {pthread_mutex_unlock(&mu);return;}
    net.ban_requested = 1;
    net.ban_value = banned ? 1 : 0;
    net.account_generation++;
    snprintf(net.ban_login, sizeof net.ban_login, "%s", login ? login : "");
    snprintf(net.ban_reason, sizeof net.ban_reason, "%s", reason ? reason : "");
    net.view.account_busy = 1;
    net.view.account_notice[0] = 0;
    pthread_mutex_unlock(&mu);
}
void on_net_level_set_official(const char *level_id, int official) {
    if (!on_protocol_valid_level_id(level_id)) return;
    if (!ensure_transport_locked()) return;
    pthread_mutex_lock(&mu);
    if (net.official_requested) {pthread_mutex_unlock(&mu);return;}
    net.official_requested = 1;
    net.official_value = official ? 1 : 0;
    net.account_generation++;
    snprintf(net.official_level, sizeof net.official_level, "%s", level_id);
    net.view.account_busy = 1;
    net.view.account_notice[0] = 0;
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

/* Defined below, with the rest of the account work: a publish that credits its
 * author has to retire the token the public record now carries. */
static void account_rotate_token(void);

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
    {
        /* Credit the level to the signed-in player so the rules can attribute
         * it, then retire the token this record just made public. */
        OnAccount session;
        pthread_mutex_lock(&mu);
        session = net.view.account;
        pthread_mutex_unlock(&mu);
        if (session.signed_in && session.login[0] && session.token[0]) {
            char author[256], author_path[56];
            size_t author_need = on_account_author_json(session.login,
                                                        session.token, NULL, 0);
            if (author_need && author_need < sizeof author &&
                on_account_author_json(session.login, session.token, author,
                                       sizeof author) == author_need) {
                snprintf(author_path, sizeof author_path, "levels/%s/author.json",
                         saved_id);
                int author_code = request(author_path, "PUT", author, NULL);
                size_t text_need = on_account_text_json(session.login, NULL, 0);
                if (author_code == 200 && text_need && text_need < sizeof author &&
                    on_account_text_json(session.login, author, sizeof author) ==
                        text_need) {
                    snprintf(author_path, sizeof author_path,
                             "levels-index/%s/author.json", saved_id);
                    request(author_path, "PUT", author, NULL);
                }
            }
            account_rotate_token();
        }
    }
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

/* ------------------------------------------------ accounts and moderation */

/* Every notice below is shown by the LVGL catalog, so it stays short and in
 * the player's language: the UI translates the fixed labels, not these. */
static void account_notice(unsigned gen, const char *text) {
    pthread_mutex_lock(&mu);
    if (gen == net.account_generation) {
        net.view.account_busy = 0;
        snprintf(net.view.account_notice, sizeof net.view.account_notice,
                 "%s", text ? text : "");
    }
    pthread_mutex_unlock(&mu);
}
static void comments_notice(unsigned gen, const char *text) {
    pthread_mutex_lock(&mu);
    if (gen == net.account_generation) {
        net.view.comments_busy = 0;
        snprintf(net.view.account_notice, sizeof net.view.account_notice,
                 "%s", text ? text : "");
    }
    pthread_mutex_unlock(&mu);
}
static void trim_text(char *out, size_t cap, const char *text) {
    const char *value = text ? text : "";
    size_t length = strlen(value), first = 0, last = length;
    while (first < last && (unsigned char)value[first] <= ' ') first++;
    while (last > first && (unsigned char)value[last - 1] <= ' ') last--;
    if (last - first >= cap) last = first + cap - 1;
    memcpy(out, value + first, last - first);
    out[last - first] = 0;
}

/* Signs in, or creates the account first. The password never leaves the
 * device: PBKDF2-SHA256 turns it into the same 64-hex hash the website
 * stores, and only the session token travels to the database. */
static void account_sign_in(unsigned gen, int create) {
    char login[ON_LOGIN_SIZE], password[ON_PASSWORD_SIZE];
    char salt[ON_HASH_SIZE], hash[ON_HASH_SIZE], token[ON_TOKEN_SIZE];
    char body[512], path[80];
    OnAccount account = {0};
    int64_t now = clock_ms(NET_CLOCK_REALTIME);
    size_t need;
    int code, admin = 0;

    pthread_mutex_lock(&mu);
    memcpy(login, net.account_login, sizeof login);
    memcpy(password, net.account_password, sizeof password);
    pthread_mutex_unlock(&mu);

    if (!on_account_normalize(login, login) || !on_account_valid_login(login)) {
        account_notice(gen, "Ник: только a-z, 0-9 и _, от 3 до 24 знаков.");
        return;
    }
    if (!on_account_valid_password(password)) {
        account_notice(gen, "Пароль: от 6 до 72 знаков без пробелов.");
        return;
    }
    on_account_salt(login, salt);
    on_account_hash(login, password, hash);

    if (create) {
        need = on_account_record_json(salt, hash, now, NULL, 0);
        if (!need || need >= sizeof body ||
            on_account_record_json(salt, hash, now, body, sizeof body) != need) {
            account_notice(gen, "Не удалось подготовить аккаунт.");
            return;
        }
        snprintf(path, sizeof path, "accounts/%s.json", login);
        code = request(path, "PUT", body, "null_etag");
        if (code == 412) {account_notice(gen, "Такой аккаунт уже есть.");return;}
        if (code != 200) {
            account_notice(gen, code == 401 || code == 403 ?
                "База не приняла аккаунт. Проверь правила Firebase." :
                "Не удалось создать аккаунт. Проверь интернет.");
            return;
        }
    }
    on_net_random_hex(token);
    need = on_account_token_json(token, hash, NULL, 0);
    if (!need || need >= sizeof body ||
        on_account_token_json(token, hash, body, sizeof body) != need) {
        account_notice(gen, "Не удалось подготовить вход.");
        return;
    }
    snprintf(path, sizeof path, "tokens/%s.json", login);
    code = request(path, "PUT", body, NULL);
    if (code != 200) {
        account_notice(gen, code == 401 || code == 403 ?
            "Неверный ник или пароль." : "Не удалось войти. Проверь интернет.");
        return;
    }
    snprintf(path, sizeof path, "admins/%s.json", login);
    if (request(path, "GET", NULL, NULL) == 200)
        admin = on_account_parse_admin(net.response);

    account.signed_in = 1;
    account.admin = admin;
    snprintf(account.login, sizeof account.login, "%s", login);
    snprintf(account.token, sizeof account.token, "%s", token);
    snprintf(account.hash, sizeof account.hash, "%s", hash);
    pthread_mutex_lock(&mu);
    if (gen == net.account_generation) net.view.account = account;
    pthread_mutex_unlock(&mu);
    preferences_set_account(account.login, account.token, account.hash, account.admin);
    account_notice(gen, admin ? "Вход выполнен. Вы модератор." : "Вход выполнен.");
}

/* Retires the token that a publish or a moderation record made public. */
static void account_rotate_token(void) {
    char token[ON_TOKEN_SIZE], body[256], path[80];
    OnAccount current;
    size_t need;
    int code;

    pthread_mutex_lock(&mu);
    current = net.view.account;
    pthread_mutex_unlock(&mu);
    if (!current.signed_in || !current.login[0] || !current.hash[0]) return;
    on_net_random_hex(token);
    need = on_account_token_json(token, current.hash, NULL, 0);
    if (!need || need >= sizeof body ||
        on_account_token_json(token, current.hash, body, sizeof body) != need) return;
    snprintf(path, sizeof path, "tokens/%s.json", current.login);
    code = request(path, "PUT", body, NULL);
    if (code != 200) return;
    pthread_mutex_lock(&mu);
    /* Only refresh the session the player is still using. */
    if (net.view.account.signed_in &&
        !strcmp(net.view.account.login, current.login) &&
        !strcmp(net.view.account.token, current.token)) {
        snprintf(net.view.account.token, sizeof net.view.account.token, "%s", token);
        current.token[0] = 0;
        snprintf(current.token, sizeof current.token, "%s", token);
    }
    pthread_mutex_unlock(&mu);
    preferences_set_account(current.login, current.token, current.hash, current.admin);
}

static void comments_load(unsigned gen, const char *level) {
    char path[64];
    OnComment list[ON_COMMENTS_CAP];
    int count = 0, code;
    snprintf(path, sizeof path, "comments/%s.json", level);
    code = request(path, "GET", NULL, NULL);
    if (code == 200)
        count = on_account_parse_comments(net.response, list, ON_COMMENTS_CAP);
    if (count < 0) count = 0; /* null or damaged data simply means «no comments» */
    pthread_mutex_lock(&mu);
    if (gen == net.account_generation) {
        memcpy(net.view.comments, list, sizeof list);
        net.view.comment_count = count;
        snprintf(net.view.comment_level, sizeof net.view.comment_level, "%s", level);
        net.view.comments_busy = 0;
    }
    pthread_mutex_unlock(&mu);
    if (code != 200 && code != 404)
        comments_notice(gen, code == 401 || code == 403 ?
            "Firebase запретил чтение сообщений. Проверь правила." :
            "Не удалось загрузить сообщения. Проверь интернет.");
    compact_response();
}

static void comment_post(unsigned gen, const char *level, const char *text) {
    char clean[ON_COMMENT_TEXT_SIZE], body[ON_COMMENT_TEXT_SIZE + 256];
    char path[96], token[ON_TOKEN_SIZE], id[ON_COMMENT_ID_SIZE];
    OnAccount session;
    size_t need;
    int code;

    pthread_mutex_lock(&mu);
    session = net.view.account;
    pthread_mutex_unlock(&mu);
    if (!session.signed_in) {comments_notice(gen, "Сначала войди в аккаунт.");return;}
    trim_text(clean, sizeof clean, text);
    if (!clean[0]) {comments_notice(gen, "Пустое сообщение.");return;}
    on_net_random_hex(token);
    memcpy(id, token, 16);
    id[16] = 0;
    need = on_account_comment_json(session.login, clean, session.token,
                                   clock_ms(NET_CLOCK_REALTIME), NULL, 0);
    if (!need || need >= sizeof body ||
        on_account_comment_json(session.login, clean, session.token,
                                clock_ms(NET_CLOCK_REALTIME), body,
                                sizeof body) != need) {
        comments_notice(gen, "Не удалось подготовить сообщение.");
        return;
    }
    snprintf(path, sizeof path, "comments/%s/%s.json", level, id);
    code = request(path, "PUT", body, NULL);
    if (code != 200) {
        comments_notice(gen, code == 401 || code == 403 ?
            "База не приняла сообщение. Возможно, ник забанен." :
            "Не удалось отправить сообщение. Проверь интернет.");
        return;
    }
    comments_load(gen, level);
}

static void comment_hide(unsigned gen, const char *level, const char *id) {
    char body[ON_COMMENT_TEXT_SIZE + 256], path[96];
    OnAccount session;
    OnComment found = {{0}, {0}, {0}, 0, 0};
    int have = 0, code;
    size_t need;

    pthread_mutex_lock(&mu);
    session = net.view.account;
    if (!strcmp(net.view.comment_level, level))
        for (int i = 0; i < net.view.comment_count && !have; i++)
            if (!strcmp(net.view.comments[i].id, id)) {found = net.view.comments[i];have = 1;}
    pthread_mutex_unlock(&mu);
    if (!session.signed_in) {comments_notice(gen, "Сначала войди в аккаунт.");return;}
    if (!have) {comments_notice(gen, "Сообщение не найдено. Обнови список.");return;}
    need = on_account_comment_hide_json(&found, session.login, session.token, NULL, 0);
    if (!need || need >= sizeof body ||
        on_account_comment_hide_json(&found, session.login, session.token, body,
                                     sizeof body) != need) {
        comments_notice(gen, "Не удалось подготовить скрытие.");
        return;
    }
    snprintf(path, sizeof path, "comments/%s/%s.json", level, id);
    code = request(path, "PUT", body, NULL);
    if (code != 200) {
        comments_notice(gen, code == 401 || code == 403 ?
            "Скрыть сообщение может только его автор или модератор." :
            "Не удалось скрыть сообщение. Проверь интернет.");
        return;
    }
    comments_load(gen, level);
    account_rotate_token();
}

static void account_ban(unsigned gen, const char *login, const char *reason,
                        int banned) {
    char name[ON_LOGIN_SIZE], body[ON_REASON_SIZE + 256], path[80];
    OnAccount session;
    size_t need;
    int code;

    pthread_mutex_lock(&mu);
    session = net.view.account;
    pthread_mutex_unlock(&mu);
    if (!session.signed_in) {account_notice(gen, "Сначала войди в аккаунт.");return;}
    if (!session.admin) {account_notice(gen, "Банить может только модератор.");return;}
    if (!on_account_normalize(name, login) || !on_account_valid_login(name)) {
        account_notice(gen, "Ник: только a-z, 0-9 и _, от 3 до 24 знаков.");
        return;
    }
    need = on_account_ban_json(banned, reason ? reason : "", session.login,
                               session.token, clock_ms(NET_CLOCK_REALTIME), NULL, 0);
    if (!need || need >= sizeof body ||
        on_account_ban_json(banned, reason ? reason : "", session.login,
                            session.token, clock_ms(NET_CLOCK_REALTIME), body,
                            sizeof body) != need) {
        account_notice(gen, "Не удалось подготовить бан.");
        return;
    }
    snprintf(path, sizeof path, "bans/%s.json", name);
    code = request(path, "PUT", body, NULL);
    if (code != 200) {
        account_notice(gen, code == 401 || code == 403 ?
            "База не приняла бан. Проверь правила /bans." :
            "Не удалось забанить. Проверь интернет.");
        return;
    }
    account_notice(gen, banned ? "Игрок забанен." : "Игрок разбанен.");
    account_rotate_token();
}

/* The «ОФИЦИАЛЬНЫЙ» badge is stored in the database, not in the client: the
 * record and its index card are both written, so the browser workshop and the
 * APK agree on which levels are official. */
static void level_set_official(unsigned gen, const char *level, int official) {
    char body[256], path[80];
    OnAccount session;
    size_t need;
    int code;

    pthread_mutex_lock(&mu);
    session = net.view.account;
    pthread_mutex_unlock(&mu);
    if (!session.signed_in) {account_notice(gen, "Сначала войди в аккаунт.");return;}
    if (!session.admin) {account_notice(gen, "Метку ставит только модератор.");return;}
    if (!on_protocol_valid_level_id(level)) {account_notice(gen, "Неверный ID уровня.");return;}
    if (official) {
        /* The rules only let a moderator's own login carry the badge. */
        need = on_account_author_json(session.login, session.token, NULL, 0);
        if (!need || need >= sizeof body ||
            on_account_author_json(session.login, session.token, body,
                                   sizeof body) != need) {
            account_notice(gen, "Не удалось подготовить автора.");
            return;
        }
        snprintf(path, sizeof path, "levels/%s/author.json", level);
        code = request(path, "PUT", body, NULL);
        if (code != 200) {
            account_notice(gen, code == 401 || code == 403 ?
                "База не приняла автора уровня. Проверь правила /levels." :
                "Не удалось пометить уровень. Проверь интернет.");
            return;
        }
        need = on_account_flag_json(1, NULL, 0);
        if (!need || need >= sizeof body ||
            on_account_flag_json(1, body, sizeof body) != need) return;
        snprintf(path, sizeof path, "levels-index/%s/author.json", level);
        need = on_account_text_json(session.login, NULL, 0);
        if (!need || need >= sizeof body ||
            on_account_text_json(session.login, body, sizeof body) != need) {
            account_notice(gen, "Не удалось подготовить каталог.");
            return;
        }
        code = request(path, "PUT", body, NULL);
        if (code != 200) {account_notice(gen, "Не удалось обновить каталог.");return;}
    }
    need = on_account_flag_json(official, NULL, 0);
    if (!need || need >= sizeof body ||
        on_account_flag_json(official, body, sizeof body) != need) {
        account_notice(gen, "Не удалось подготовить метку.");
        return;
    }
    snprintf(path, sizeof path, "levels/%s/official.json", level);
    code = request(path, "PUT", body, NULL);
    if (code != 200) {
        account_notice(gen, code == 401 || code == 403 ?
            "База не приняла метку. Проверь правила /levels." :
            "Не удалось пометить уровень. Проверь интернет.");
        return;
    }
    snprintf(path, sizeof path, "levels-index/%s/official.json", level);
    code = request(path, "PUT", body, NULL);
    if (code != 200) {
        account_notice(gen, code == 401 || code == 403 ?
            "База не приняла метку каталога. Проверь правила." :
            "Уровень помечен, но каталог не обновился.");
        return;
    }
    account_notice(gen, official ? "Уровень стал официальным." : "Метка снята.");
    pthread_mutex_lock(&mu);
    net.level_list_requested = 1;
    net.view.levels_busy = 1;
    net.view.level_loaded = 0;
    net.level_generation++;
    pthread_mutex_unlock(&mu);
    account_rotate_token();
}

void on_net_pump_once(void) {
    int task = T_IDLE, map = 1, role = 0, slot = 0;
    int ban_value = 0, official_value = 0;
    unsigned gen, level_gen, publish_gen, account_gen;
    char id[ON_ROOM_ID_SIZE] = {0}, player[ON_PLAYER_ID_SIZE] = {0};
    char level_id[ON_LEVEL_ID_SIZE] = {0};
    char text[ON_COMMENT_TEXT_SIZE] = {0};
    char comment_id[ON_COMMENT_ID_SIZE] = {0};
    char reason[ON_REASON_SIZE] = {0};
    OnCommand command = {0};
    OnMatch state;
    unsigned revision = 0;
    int64_t now = clock_ms(NET_CLOCK_MONOTONIC);
    pthread_mutex_lock(&mu);
    if (net.stopping || !net.response ||
        (net.view.mode == ON_NET_CLOSED && net.action != A_LEAVE &&
         !net.level_publish_requested && !net.level_fetch_requested &&
         !net.level_list_requested && !net.account_job &&
         !net.comments_requested && !net.comment_post_requested &&
         !net.comment_hide_requested && !net.ban_requested &&
         !net.official_requested)) {
        pthread_mutex_unlock(&mu);return;
    }
    gen = net.generation;level_gen = net.level_generation;
    publish_gen = net.level_publish_generation;
    account_gen = net.account_generation;
    memcpy(player, net.player_id, sizeof(player));
    if (net.account_job) {
        task = net.account_job == ACCOUNT_SIGN_IN ? T_SIGN_IN :
               net.account_job == ACCOUNT_CREATE ? T_CREATE_ACCOUNT : T_TOKEN_ROTATE;
        net.account_job = ACCOUNT_NONE;
    } else if (net.comments_requested) {
        task = T_COMMENTS;
        memcpy(level_id, net.comments_level, sizeof(level_id));
        net.comments_requested = 0;
    } else if (net.comment_post_requested) {
        task = T_COMMENT_POST;
        memcpy(level_id, net.comment_post_level, sizeof(level_id));
        memcpy(text, net.comment_text, sizeof(text));
        net.comment_post_requested = 0;
    } else if (net.comment_hide_requested) {
        task = T_COMMENT_HIDE;
        memcpy(level_id, net.comment_hide_level, sizeof(level_id));
        memcpy(comment_id, net.comment_hide_id, sizeof(comment_id));
        net.comment_hide_requested = 0;
    } else if (net.ban_requested) {
        task = T_BAN;
        ban_value = net.ban_value;
        memcpy(comment_id, net.ban_login, sizeof(comment_id));
        memcpy(reason, net.ban_reason, sizeof(reason));
        net.ban_requested = 0;
    } else if (net.official_requested) {
        task = T_OFFICIAL;
        official_value = net.official_value;
        memcpy(level_id, net.official_level, sizeof(level_id));
        net.official_requested = 0;
    } else if (net.level_fetch_requested) {
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
    if (task == T_SIGN_IN || task == T_CREATE_ACCOUNT) {
        char login[ON_LOGIN_SIZE] = {0}, password[ON_PASSWORD_SIZE] = {0};
        pthread_mutex_lock(&mu);
        memcpy(login, net.account_login, sizeof login);
        memcpy(password, net.account_password, sizeof password);
        pthread_mutex_unlock(&mu);
        account_sign_in(account_gen, task == T_CREATE_ACCOUNT);
        return;
    }
    if (task == T_TOKEN_ROTATE) {account_rotate_token();return;}
    if (task == T_COMMENTS) {comments_load(account_gen, level_id);return;}
    if (task == T_COMMENT_POST) {comment_post(account_gen, level_id, text);return;}
    if (task == T_COMMENT_HIDE) {comment_hide(account_gen, level_id, comment_id);return;}
    if (task == T_BAN) {account_ban(account_gen, comment_id, reason, ban_value);return;}
    if (task == T_OFFICIAL) {level_set_official(account_gen, level_id, official_value);return;}

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
