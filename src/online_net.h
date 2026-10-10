#ifndef PVG3_ONLINE_NET_H
#define PVG3_ONLINE_NET_H

#include "online_protocol.h"
#include "online_account.h"

#include <stddef.h>

/* Firebase RTDB REST lives on a background pthread; game_tick and touch never
 * block on HTTPS. The Android transport below uses HttpURLConnection via JNI.
 * Host tests replace it with an in-memory fake. Only /rooms and the public
 * /levels catalog are touched. */
int on_http_request(const char *path, const char *method, const char *body,
                    const char *if_match, char *response, size_t response_cap);

enum { ON_NET_CLOSED, ON_NET_ROOMS, ON_NET_LOBBY };
enum { ON_SLOT_NONE, ON_SLOT_HOST, ON_SLOT_GUEST };
typedef struct {
    int mode, slot, busy, connected, pending;
    char room_id[ON_ROOM_ID_SIZE];
    char guest_id[ON_PLAYER_ID_SIZE];
    char notice[144];
    int map, host_role, guest_role;
    OnRoomSummary rooms[ON_ROOM_LIST_CAP]; int room_count;
    int has_state, has_command;
    OnMatch state;
    OnCommand command;
    OnPublishedLevelSummary levels[ON_LEVEL_LIST_CAP]; int level_count;
    int levels_busy, level_loaded;
    char levels_notice[144];
    int level_publish_busy;
    char level_publish_id[ON_LEVEL_ID_SIZE];
    char level_publish_notice[144];
    /* Lightweight immutable snapshot metadata; object storage is copied only
     * when the main thread consumes a freshly loaded level. */
    char loaded_level_id[ON_LEVEL_ID_SIZE];
    char loaded_level_title[ON_LEVEL_TITLE_SIZE];
    /* Accounts, comments and moderation. account.login[0] == 0 means «guest»:
     * the catalog keeps working exactly as it did before accounts existed. */
    OnAccount account;
    OnComment comments[ON_COMMENTS_CAP];
    int comment_count;
    char comment_level[ON_LEVEL_ID_SIZE];
    int account_busy, comments_busy;
    char account_notice[145];
} OnNetView;

void on_net_open(void);
void on_net_close(void); /* return to menu without waiting for in-flight HTTPS */
void on_net_shutdown(void); /* app exit: join thread, release response buffer */
void on_net_view(OnNetView *out);
void on_net_refresh(void);
void on_net_levels_refresh(void);
void on_net_level_fetch(const char *id);
/* A catalog like is toggleable; a unique install records one download. */
void on_net_level_like(const char *id);
void on_net_level_download(const char *id);
/* Explicit native-workshop action: creates a new public level and index entry. */
int on_net_level_publish(const OnPublishedLevel *level);
void on_net_level_cancel(void);
/* Accounts: the password is stretched on the worker thread and never stored,
 * so the UI stays responsive while PBKDF2 runs. */
void on_net_account_restore(void); /* loads the saved nick/token without opening rooms */
void on_net_account_sign_in(const char *login, const char *password, int create);
void on_net_account_sign_out(void);
/* Comments under the catalog level the player is looking at. */
void on_net_comments_load(const char *level_id);
void on_net_comment_post(const char *level_id, const char *text);
void on_net_comment_hide(const char *level_id, const char *comment_id);
/* Moderator tools; both are refused unless the signed-in nick is in /admins. */
void on_net_account_ban(const char *login, const char *reason, int banned);
void on_net_level_set_official(const char *level_id, int official);
/* 32 random bytes as lowercase hex, for session tokens and comment ids. */
void on_net_random_hex(char out[ON_TOKEN_SIZE]);
/* Copies the pending record to caller-owned storage without exposing worker
 * memory; caller storage should be static/heap-backed for the 20k-object cap. */
int on_net_take_loaded_level(OnPublishedLevel *out);
void on_net_level_consumed(void);
void on_net_create(int map);
void on_net_join(const char *id);
void on_net_choose(int role);
void on_net_leave(void);
void on_net_publish(const OnMatch *match); /* host: newest authoritative frame */
int on_net_send(OnCommand command);       /* guest: one command until ACK */
/* Only host tests call this directly; Android calls it from the worker. */
void on_net_pump_once(void);

#endif
