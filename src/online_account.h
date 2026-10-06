#ifndef PVG3_ONLINE_ACCOUNT_H
#define PVG3_ONLINE_ACCOUNT_H

/* Player accounts for the native game: nick + password, moderation and the
 * comments under a published level. There is no server of our own and no
 * embedded credential: the password is stretched with PBKDF2-SHA256 on the
 * device (exactly like the browser workshop) and exchanged for a random session
 * token that travels with privileged writes, so a shared database sees nothing
 * but what firebase/database.rules.json allows.
 *
 * Everything here is pure logic — no threads, no sockets — so the host tests
 * can check the crypto and the JSON against the same vectors as the website. */

#include <stddef.h>
#include <stdint.h>

#define ON_LOGIN_SIZE 25          /* 24 characters plus NUL */
#define ON_PASSWORD_SIZE 73       /* 72 characters plus NUL */
#define ON_TOKEN_SIZE 65          /* 64 hex characters plus NUL */
#define ON_HASH_SIZE 65
#define ON_COMMENT_ID_SIZE 25
#define ON_COMMENT_TEXT_SIZE 301  /* the rules allow 300 characters */
#define ON_REASON_SIZE 141
#define ON_COMMENTS_CAP 24
#define ON_BANS_CAP 64

typedef struct {
    char id[ON_COMMENT_ID_SIZE];
    char login[ON_LOGIN_SIZE];
    char text[ON_COMMENT_TEXT_SIZE];
    int64_t at;
    int hidden;
} OnComment;

typedef struct {
    char login[ON_LOGIN_SIZE];
    char reason[ON_REASON_SIZE];
    int64_t at;
} OnBan;

typedef struct {
    int signed_in, admin;
    char login[ON_LOGIN_SIZE];
    char token[ON_TOKEN_SIZE];
    char hash[ON_HASH_SIZE];
} OnAccount;

/* Nick rules shared with the website: only a–z, 0–9 and _, 3 to 24 characters,
 * trimmed and lower-cased first, so two nicks can never differ by spaces or by
 * capital letters alone. */
int on_account_normalize(char out[ON_LOGIN_SIZE], const char *value);
int on_account_valid_login(const char *value);
int on_account_valid_password(const char *password);

/* The salt is derived from the nick, so a client never has to read /accounts
 * to compute a hash. Both outputs are 64 lowercase hex characters plus NUL. */
void on_account_salt(const char *login, char salt_hex[ON_HASH_SIZE]);
void on_account_hash(const char *login, const char *password,
                     char hash_hex[ON_HASH_SIZE]);

/* Builders for the small records the rules accept. Each returns the number of
 * bytes it would write (out == NULL measures it) or 0 when cap is too small. */
size_t on_account_record_json(const char *salt, const char *hash,
                              int64_t created_at, char *out, size_t cap);
size_t on_account_token_json(const char *token, const char *hash,
                             char *out, size_t cap);
size_t on_account_author_json(const char *login, const char *token,
                              char *out, size_t cap);
size_t on_account_comment_json(const char *login, const char *text,
                               const char *token, int64_t at,
                               char *out, size_t cap);
/* Hiding keeps the original text and author: only `hidden`, `by` and the token
 * are added, exactly as firebase/database.rules.json requires. */
size_t on_account_comment_hide_json(const OnComment *comment, const char *by,
                                    const char *token, char *out, size_t cap);
size_t on_account_ban_json(int banned, const char *reason, const char *by,
                           const char *token, int64_t at, char *out, size_t cap);
size_t on_account_flag_json(int official, char *out, size_t cap);
/* A bare JSON string, for single-value writes such as the author of a card in
 * /levels-index. */
size_t on_account_text_json(const char *text, char *out, size_t cap);

/* Parsers for the branches the game reads. Every one of them treats public
 * data as hostile: unknown fields are skipped, oversized strings are dropped. */
/* /admins/{nick}: 1 for true, 0 for false, null or anything else. */
int on_account_parse_admin(const char *json);
/* /bans: fills out with every entry whose `banned` is true. Returns the count. */
int on_account_parse_bans(const char *json, OnBan *out, int cap);
/* /comments/{level}: oldest first. Returns the count, or -1 for broken JSON. */
int on_account_parse_comments(const char *json, OnComment *out, int cap);
int on_account_is_banned(const OnBan *bans, int count, const char *login);

#endif
