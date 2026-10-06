#include "preferences.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define PREFERENCES_PATH_CAP 4096
#define PREFERENCES_VERSION 3
#define PREFERENCES_LOGIN_CAP 25
#define PREFERENCES_SECRET_CAP 65

/* The options file is written from the UI thread and, since accounts arrived,
 * from the network worker too; one small lock keeps the two apart. */
static pthread_mutex_t preferences_lock = PTHREAD_MUTEX_INITIALIZER;

/* Keep authored artwork enabled by default; plain blue-gray is optional. */
static int music_enabled = 1;
static int neutral_background_enabled;
static int tutorial_hints_enabled;
static char preferences_path[PREFERENCES_PATH_CAP];
static char account_login[PREFERENCES_LOGIN_CAP];
static char account_token[PREFERENCES_SECRET_CAP];
static char account_hash[PREFERENCES_SECRET_CAP];
static int account_admin;

static int is_hex64(const char *value) {
    if (!value || strlen(value) != 64) return 0;
    for (int i = 0; i < 64; i++) {
        char c = value[i];
        int hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) return 0;
    }
    return 1;
}
static int is_login(const char *value) {
    size_t length = value ? strlen(value) : 0;
    if (length < 3 || length >= PREFERENCES_LOGIN_CAP) return 0;
    for (size_t i = 0; i < length; i++) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return 1;
}

static void save_preferences(void) {
    char temporary[PREFERENCES_PATH_CAP + 8];
    int length;
    FILE *file;
    if (!preferences_path[0]) return;
    length = snprintf(temporary, sizeof temporary, "%s.tmp",
                      preferences_path);
    if (length <= 0 || (size_t)length >= sizeof temporary) return;

    pthread_mutex_lock(&preferences_lock);
    file = fopen(temporary, "wb");
    if (!file) {
        pthread_mutex_unlock(&preferences_lock);
        return;
    }
    int ok = fprintf(file,
        "PVG3-PREFERENCES %d\nmusic=%d\nneutral_background=%d\ntutorial_hints=%d\n"
        "account_login=%s\naccount_token=%s\naccount_hash=%s\naccount_admin=%d\n",
        PREFERENCES_VERSION, music_enabled, neutral_background_enabled,
        tutorial_hints_enabled, account_login, account_token, account_hash,
        account_admin ? 1 : 0) > 0;
    if (fclose(file) != 0) ok = 0;
    if (ok) {
        remove(preferences_path);
        if (rename(temporary, preferences_path) != 0) remove(temporary);
    } else {
        remove(temporary);
    }
    pthread_mutex_unlock(&preferences_lock);
}

/* Reads `key=value` into out; returns 1 when the key was present. */
static int read_value(const char *line, const char *key, char *out, size_t cap) {
    size_t key_length = strlen(key);
    size_t at = 0;
    if (strncmp(line, key, key_length)) return 0;
    while (line[key_length + at] && line[key_length + at] != '\n' &&
           line[key_length + at] != '\r') {
        if (at + 1 >= cap) break;
        out[at] = line[key_length + at];
        at++;
    }
    out[at] = 0;
    return 1;
}

void preferences_set_path(const char *path) {
    if (!path) {
        preferences_path[0] = 0;
        return;
    }
    size_t length = strlen(path);
    if (length >= sizeof preferences_path) return;
    memcpy(preferences_path, path, length + 1);

    pthread_mutex_lock(&preferences_lock);
    music_enabled = 1;
    neutral_background_enabled = 0;
    tutorial_hints_enabled = 0;
    account_login[0] = account_token[0] = account_hash[0] = 0;
    account_admin = 0;
    pthread_mutex_unlock(&preferences_lock);
    FILE *file = fopen(preferences_path, "rb");
    if (!file) return;

    char line[256];
    int version = 1;
    if (fgets(line, sizeof line, file)) {
        int parsed_version = 0;
        if (sscanf(line, "PVG3-PREFERENCES %d", &parsed_version) == 1)
            version = parsed_version;
    }
    char login[PREFERENCES_LOGIN_CAP] = {0}, token[PREFERENCES_SECRET_CAP] = {0};
    char hash[PREFERENCES_SECRET_CAP] = {0};
    int admin = 0;
    while (fgets(line, sizeof line, file)) {
        if (!strncmp(line, "music=", 6) &&
            (line[6] == '0' || line[6] == '1'))
            music_enabled = line[6] == '1';
        else if (version >= 2 && !strncmp(line, "neutral_background=", 19) &&
                 (line[19] == '0' || line[19] == '1'))
            neutral_background_enabled = line[19] == '1';
        else if (!strncmp(line, "tutorial_hints=", 15) &&
                 (line[15] == '0' || line[15] == '1'))
            tutorial_hints_enabled = line[15] == '1';
        else if (version >= 3) {
            if (read_value(line, "account_login=", login, sizeof login)) continue;
            if (read_value(line, "account_token=", token, sizeof token)) continue;
            if (read_value(line, "account_hash=", hash, sizeof hash)) continue;
            if (!strncmp(line, "account_admin=", 14) &&
                (line[14] == '0' || line[14] == '1'))
                admin = line[14] == '1';
        }
    }
    fclose(file);
    /* A damaged or hand-edited record simply means «not signed in». */
    pthread_mutex_lock(&preferences_lock);
    if (is_login(login) && is_hex64(token) && is_hex64(hash)) {
        memcpy(account_login, login, strlen(login) + 1);
        memcpy(account_token, token, strlen(token) + 1);
        memcpy(account_hash, hash, strlen(hash) + 1);
        account_admin = admin ? 1 : 0;
    } else {
        account_login[0] = account_token[0] = account_hash[0] = 0;
        account_admin = 0;
    }
    pthread_mutex_unlock(&preferences_lock);

    /* Version 1 defaulted to the plain background. Switch that old default to
     * authored artwork; a user can reselect the plain background in Settings. */
    if (version < PREFERENCES_VERSION) save_preferences();
}

int preferences_music_enabled(void) { return music_enabled; }

void preferences_set_music_enabled(int enabled) {
    enabled = !!enabled;
    if (music_enabled == enabled) return;
    music_enabled = enabled;
    save_preferences();
}

int preferences_neutral_background_enabled(void) {
    return neutral_background_enabled;
}

void preferences_set_neutral_background_enabled(int enabled) {
    enabled = !!enabled;
    if (neutral_background_enabled == enabled) return;
    neutral_background_enabled = enabled;
    save_preferences();
}

int preferences_tutorial_hints_enabled(void) {
    return tutorial_hints_enabled;
}

void preferences_set_tutorial_hints_enabled(int enabled) {
    enabled = !!enabled;
    if (tutorial_hints_enabled == enabled) return;
    tutorial_hints_enabled = enabled;
    save_preferences();
}

static int copy_out(char *out, size_t cap, const char *value) {
    size_t length;
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (!value || !value[0]) return 0;
    length = strlen(value);
    if (length >= cap) return 0;
    memcpy(out, value, length + 1);
    return 1;
}

int preferences_account_login(char *out, size_t cap) {
    int result;
    pthread_mutex_lock(&preferences_lock);
    result = copy_out(out, cap, account_login);
    pthread_mutex_unlock(&preferences_lock);
    return result;
}
int preferences_account_token(char *out, size_t cap) {
    int result;
    pthread_mutex_lock(&preferences_lock);
    result = copy_out(out, cap, account_token);
    pthread_mutex_unlock(&preferences_lock);
    return result;
}
int preferences_account_hash(char *out, size_t cap) {
    int result;
    pthread_mutex_lock(&preferences_lock);
    result = copy_out(out, cap, account_hash);
    pthread_mutex_unlock(&preferences_lock);
    return result;
}
int preferences_account_admin(void) {
    int result;
    pthread_mutex_lock(&preferences_lock);
    result = account_admin;
    pthread_mutex_unlock(&preferences_lock);
    return result;
}
void preferences_set_account(const char *login, const char *token,
                             const char *hash, int admin) {
    pthread_mutex_lock(&preferences_lock);
    if (!login || !login[0] || !is_login(login) || !is_hex64(token) ||
        !is_hex64(hash)) {
        account_login[0] = account_token[0] = account_hash[0] = 0;
        account_admin = 0;
    } else {
        memcpy(account_login, login, strlen(login) + 1);
        memcpy(account_token, token, strlen(token) + 1);
        memcpy(account_hash, hash, strlen(hash) + 1);
        account_admin = admin ? 1 : 0;
    }
    pthread_mutex_unlock(&preferences_lock);
    save_preferences();
}
