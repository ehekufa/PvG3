/* Native half of the PvG3 player accounts: the same PBKDF2-SHA256 stretch and
 * the same record shapes as online/accounts.js, so an account created in the
 * browser works in the APK and the other way round. Public data is parsed
 * defensively: nothing here trusts the database. */
#include "online_account.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ SHA-256 */

typedef struct {
    uint32_t state[8];
    unsigned char block[64];
    size_t used;
    uint64_t total;
} OnSha256;

static const uint32_t ON_SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

#define ON_ROR(value, bits) (((value) >> (bits)) | ((value) << (32 - (bits))))

static void on_sha256_compress(OnSha256 *c, const unsigned char block[64]) {
    uint32_t w[64], a, b, cc, d, e, f, g, h;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ON_ROR(w[i - 15], 7) ^ ON_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ON_ROR(w[i - 2], 17) ^ ON_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->state[0];b = c->state[1];cc = c->state[2];d = c->state[3];
    e = c->state[4];f = c->state[5];g = c->state[6];h = c->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ON_ROR(e, 6) ^ ON_ROR(e, 11) ^ ON_ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + ON_SHA256_K[i] + w[i];
        uint32_t S0 = ON_ROR(a, 2) ^ ON_ROR(a, 13) ^ ON_ROR(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t temp2 = S0 + maj;
        h = g;g = f;f = e;e = d + temp1;
        d = cc;cc = b;b = a;a = temp1 + temp2;
    }
    c->state[0] += a;c->state[1] += b;c->state[2] += cc;c->state[3] += d;
    c->state[4] += e;c->state[5] += f;c->state[6] += g;c->state[7] += h;
}

static void on_sha256_init(OnSha256 *c) {
    c->state[0] = 0x6a09e667u;c->state[1] = 0xbb67ae85u;c->state[2] = 0x3c6ef372u;
    c->state[3] = 0xa54ff53au;c->state[4] = 0x510e527fu;c->state[5] = 0x9b05688cu;
    c->state[6] = 0x1f83d9abu;c->state[7] = 0x5be0cd19u;
    c->used = 0;c->total = 0;
}
static void on_sha256_update(OnSha256 *c, const void *data, size_t length) {
    const unsigned char *bytes = (const unsigned char *)data;
    c->total += length;
    while (length) {
        size_t taken = 64 - c->used;
        if (taken > length) taken = length;
        memcpy(c->block + c->used, bytes, taken);
        c->used += taken;bytes += taken;length -= taken;
        if (c->used == 64) {on_sha256_compress(c, c->block);c->used = 0;}
    }
}
static void on_sha256_final(OnSha256 *c, unsigned char digest[32]) {
    uint64_t bits = c->total * 8;
    unsigned char tail[72];
    size_t pad = c->used < 56 ? 56 - c->used : 120 - c->used;
    memset(tail, 0, sizeof tail);
    tail[0] = 0x80;
    /* SHA-256 wants the bit length big-endian in the last eight bytes. */
    for (int i = 0; i < 8; i++)
        tail[pad + (size_t)i] = (unsigned char)(bits >> (56 - 8 * i));
    on_sha256_update(c, tail, pad + 8);
    for (int i = 0; i < 8; i++) {
        digest[i * 4] = (unsigned char)(c->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(c->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(c->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)c->state[i];
    }
}
static void on_sha256(const void *data, size_t length, unsigned char digest[32]) {
    OnSha256 context;
    on_sha256_init(&context);
    on_sha256_update(&context, data, length);
    on_sha256_final(&context, digest);
}

static void on_hmac_sha256(const unsigned char *key, size_t key_length,
                           const unsigned char *data, size_t data_length,
                           unsigned char out[32]) {
    unsigned char block[64], inner[32];
    OnSha256 context;
    memset(block, 0, sizeof block);
    if (key_length > 64) {
        unsigned char hashed[32];
        on_sha256(key, key_length, hashed);
        memcpy(block, hashed, 32);
    } else memcpy(block, key, key_length);
    for (int i = 0; i < 64; i++) block[i] ^= 0x36;
    on_sha256_init(&context);
    on_sha256_update(&context, block, 64);
    on_sha256_update(&context, data, data_length);
    on_sha256_final(&context, inner);
    for (int i = 0; i < 64; i++) block[i] ^= 0x36 ^ 0x5c;
    on_sha256_init(&context);
    on_sha256_update(&context, block, 64);
    on_sha256_update(&context, inner, 32);
    on_sha256_final(&context, out);
}

/* PBKDF2 with one block: the game only ever derives 32 bytes, and HMAC-SHA256
 * outputs exactly 32, so the counter runs once. */
static void on_pbkdf2_hmac_sha256(const char *password, size_t password_length,
                                  const unsigned char *salt, size_t salt_length,
                                  unsigned long rounds, unsigned char out[32]) {
    unsigned char salt_block[68], block[32], previous[32], swapped[32];
    memcpy(salt_block, salt, salt_length);
    for (int i = 0; i < 4; i++)
        salt_block[salt_length + (size_t)i] = (unsigned char)(1u >> (24 - i * 8));
    on_hmac_sha256((const unsigned char *)password, password_length, salt_block,
                   salt_length + 4, previous);
    memcpy(block, previous, 32);
    for (unsigned long round = 1; round < rounds; round++) {
        on_hmac_sha256((const unsigned char *)password, password_length, previous,
                       32, swapped);
        memcpy(previous, swapped, 32);
        for (int i = 0; i < 32; i++) block[i] ^= previous[i];
    }
    memcpy(out, block, 32);
}

static void on_hex(const unsigned char *bytes, size_t count, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < count; i++) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[count * 2] = 0;
}

/* ------------------------------------------------------------------- nicks */

int on_account_normalize(char out[ON_LOGIN_SIZE], const char *value) {
    const char *text = value ? value : "";
    size_t length = strlen(text), first = 0, last = length;
    while (first < last && (unsigned char)text[first] <= ' ') first++;
    while (last > first && (unsigned char)text[last - 1] <= ' ') last--;
    if (!out) return 0;
    if (last - first >= ON_LOGIN_SIZE) { out[0] = 0; return 0; }
    for (size_t i = first; i < last; i++) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[i - first] = c;
    }
    out[last - first] = 0;
    return 1;
}

int on_account_valid_login(const char *value) {
    char login[ON_LOGIN_SIZE];
    size_t length;
    if (!on_account_normalize(login, value)) return 0;
    length = strlen(login);
    if (length < 3 || length > 24) return 0;
    for (size_t i = 0; i < length; i++) {
        char c = login[i];
        int letter = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!letter) return 0;
    }
    return 1;
}

int on_account_valid_token(const char *value) {
    if (!value || strlen(value) != 64) return 0;
    for (size_t i = 0; i < 64; i++)
        if ((value[i] < '0' || value[i] > '9') &&
            (value[i] < 'a' || value[i] > 'f')) return 0;
    return 1;
}

int on_account_valid_password(const char *password) {
    size_t length;
    if (!password) return 0;
    length = strlen(password);
    if (length < 6 || length > 72) return 0;
    for (size_t i = 0; i < length; i++)
        if ((unsigned char)password[i] <= ' ') return 0;
    return 1;
}

/* The web client salts with sha256("pvg3-account:" + login), written out as hex,
 * and stretches the password with that hex text. Both halves must match, or
 * accounts would not be portable between the APK and the browser. */
void on_account_salt(const char *login, char salt_hex[ON_HASH_SIZE]) {
    char name[ON_LOGIN_SIZE], prefixed[ON_LOGIN_SIZE + 16];
    unsigned char digest[32];
    if (!salt_hex) return;
    if (!on_account_normalize(name, login)) name[0] = 0;
    snprintf(prefixed, sizeof prefixed, "pvg3-account:%s", name);
    on_sha256(prefixed, strlen(prefixed), digest);
    on_hex(digest, 32, salt_hex);
}

void on_account_hash(const char *login, const char *password,
                     char hash_hex[ON_HASH_SIZE]) {
    char salt[ON_HASH_SIZE];
    unsigned char digest[32];
    if (!hash_hex) return;
    hash_hex[0] = 0;
    on_account_salt(login, salt);
    on_pbkdf2_hmac_sha256(password ? password : "",
                          password ? strlen(password) : 0,
                          (const unsigned char *)salt, strlen(salt),
                          150000ul, digest);
    on_hex(digest, 32, hash_hex);
}

/* -------------------------------------------------------------------- JSON */

/* Every builder below measures first and then writes exactly that many bytes,
 * so a caller that passes out == NULL learns the size, and a caller with a
 * buffer that is too small never gets silently truncated JSON. */

/* Escapes a string into out; returns the number of bytes it wrote (or would
 * write when out == NULL), and 0 when the buffer is too small. */
static size_t on_json_string(const char *text, char *out, size_t cap) {
    size_t needed = 2;
    for (size_t i = 0; text && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') needed += 2;
        else if (c < 0x20) needed += 6;
        else needed += 1;
    }
    if (!out || cap < needed + 1) return needed;
    size_t at = 0;
    out[at++] = '"';
    for (size_t i = 0; text && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') { out[at++] = '\\'; out[at++] = (char)c; }
        else if (c == '\n') { out[at++] = '\\'; out[at++] = 'n'; }
        else if (c == '\r') { out[at++] = '\\'; out[at++] = 'r'; }
        else if (c == '\t') { out[at++] = '\\'; out[at++] = 't'; }
        else if (c < 0x20) {
            snprintf(out + at, cap - at, "\\u%04x", (unsigned)c);
            at += 6;
        } else out[at++] = (char)c;
    }
    out[at++] = '"';
    out[at] = 0;
    return at;
}

/* Appends literal text; 0 means the buffer was too small. */
static int on_json_puts(char *out, size_t cap, size_t *at, const char *text) {
    size_t length = strlen(text);
    if (*at + length >= cap) return 0;
    memcpy(out + *at, text, length + 1);
    *at += length;
    return 1;
}

size_t on_account_record_json(const char *salt, const char *hash,
                              int64_t created_at, char *out, size_t cap) {
    char number[32];
    size_t salt_size = on_json_string(salt, NULL, 0);
    size_t hash_size = on_json_string(hash, NULL, 0);
    int written = snprintf(number, sizeof number, "%lld", (long long)created_at);
    size_t need, at = 0;
    if (written <= 0 || (size_t)written >= sizeof number) return 0;
    if (!salt_size || !hash_size) return 0;
    need = strlen("{\"salt\":") + salt_size + strlen(",\"hash\":") + hash_size +
           strlen(",\"createdAt\":") + strlen(number) + 1;
    if (!out) return need;
    if (cap < need + 1) return 0;
    if (!on_json_puts(out, cap, &at, "{\"salt\":")) return 0;
    if (!on_json_string(salt, out + at, cap - at)) return 0;
    at += salt_size;
    if (!on_json_puts(out, cap, &at, ",\"hash\":")) return 0;
    if (!on_json_string(hash, out + at, cap - at)) return 0;
    at += hash_size;
    if (!on_json_puts(out, cap, &at, ",\"createdAt\":")) return 0;
    if (!on_json_puts(out, cap, &at, number)) return 0;
    if (!on_json_puts(out, cap, &at, "}")) return 0;
    return need;
}

size_t on_account_token_json(const char *token, const char *hash,
                             char *out, size_t cap) {
    size_t token_size = on_json_string(token, NULL, 0);
    size_t hash_size = on_json_string(hash, NULL, 0);
    size_t need, at = 0;
    if (!token_size || !hash_size) return 0;
    need = strlen("{\"token\":") + token_size + strlen(",\"hash\":") + hash_size + 1;
    if (!out) return need;
    if (cap < need + 1) return 0;
    if (!on_json_puts(out, cap, &at, "{\"token\":")) return 0;
    if (!on_json_string(token, out + at, cap - at)) return 0;
    at += token_size;
    if (!on_json_puts(out, cap, &at, ",\"hash\":")) return 0;
    if (!on_json_string(hash, out + at, cap - at)) return 0;
    at += hash_size;
    if (!on_json_puts(out, cap, &at, "}")) return 0;
    return need;
}

size_t on_account_author_json(const char *login, const char *token,
                              char *out, size_t cap) {
    size_t login_size = on_json_string(login, NULL, 0);
    size_t token_size = on_json_string(token, NULL, 0);
    size_t need, at = 0;
    if (!login_size || !token_size) return 0;
    need = strlen("{\"login\":") + login_size + strlen(",\"tok\":") + token_size + 1;
    if (!out) return need;
    if (cap < need + 1) return 0;
    if (!on_json_puts(out, cap, &at, "{\"login\":")) return 0;
    if (!on_json_string(login, out + at, cap - at)) return 0;
    at += login_size;
    if (!on_json_puts(out, cap, &at, ",\"tok\":")) return 0;
    if (!on_json_string(token, out + at, cap - at)) return 0;
    at += token_size;
    if (!on_json_puts(out, cap, &at, "}")) return 0;
    return need;
}

size_t on_account_comment_json(const char *login, const char *text,
                               const char *token, int64_t at_value,
                               char *out, size_t cap) {
    char number[32];
    size_t login_size = on_json_string(login, NULL, 0);
    size_t text_size = on_json_string(text, NULL, 0);
    size_t token_size = on_json_string(token, NULL, 0);
    int written = snprintf(number, sizeof number, "%lld", (long long)at_value);
    size_t need, at = 0;
    if (written <= 0 || (size_t)written >= sizeof number) return 0;
    if (!login_size || !text_size || !token_size) return 0;
    need = strlen("{\"login\":") + login_size + strlen(",\"text\":") + text_size +
           strlen(",\"at\":") + strlen(number) + strlen(",\"tok\":") +
           token_size + 1;
    if (!out) return need;
    if (cap < need + 1) return 0;
    if (!on_json_puts(out, cap, &at, "{\"login\":")) return 0;
    if (!on_json_string(login, out + at, cap - at)) return 0;
    at += login_size;
    if (!on_json_puts(out, cap, &at, ",\"text\":")) return 0;
    if (!on_json_string(text, out + at, cap - at)) return 0;
    at += text_size;
    if (!on_json_puts(out, cap, &at, ",\"at\":") ||
        !on_json_puts(out, cap, &at, number) ||
        !on_json_puts(out, cap, &at, ",\"tok\":")) return 0;
    if (!on_json_string(token, out + at, cap - at)) return 0;
    at += token_size;
    if (!on_json_puts(out, cap, &at, "}")) return 0;
    return need;
}

size_t on_account_comment_hide_json(const OnComment *comment, const char *by,
                                    const char *token, char *out, size_t cap) {
    char number[32];
    size_t login_size, text_size, by_size, token_size, need, at = 0;
    int written;
    if (!comment) return 0;
    login_size = on_json_string(comment->login, NULL, 0);
    text_size = on_json_string(comment->text, NULL, 0);
    by_size = on_json_string(by, NULL, 0);
    token_size = on_json_string(token, NULL, 0);
    written = snprintf(number, sizeof number, "%lld", (long long)comment->at);
    if (written <= 0 || (size_t)written >= sizeof number) return 0;
    if (!login_size || !text_size || !by_size || !token_size) return 0;
    need = strlen("{\"login\":") + login_size + strlen(",\"text\":") + text_size +
           strlen(",\"at\":") + strlen(number) + strlen(",\"hidden\":true,\"by\":") +
           by_size + strlen(",\"tok\":") + token_size + 1;
    if (!out) return need;
    if (cap < need + 1) return 0;
    if (!on_json_puts(out, cap, &at, "{\"login\":")) return 0;
    if (!on_json_string(comment->login, out + at, cap - at)) return 0;
    at += login_size;
    if (!on_json_puts(out, cap, &at, ",\"text\":")) return 0;
    if (!on_json_string(comment->text, out + at, cap - at)) return 0;
    at += text_size;
    if (!on_json_puts(out, cap, &at, ",\"at\":") ||
        !on_json_puts(out, cap, &at, number) ||
        !on_json_puts(out, cap, &at, ",\"hidden\":true,\"by\":")) return 0;
    if (!on_json_string(by, out + at, cap - at)) return 0;
    at += by_size;
    if (!on_json_puts(out, cap, &at, ",\"tok\":")) return 0;
    if (!on_json_string(token, out + at, cap - at)) return 0;
    at += token_size;
    if (!on_json_puts(out, cap, &at, "}")) return 0;
    return need;
}

size_t on_account_ban_json(int banned, const char *reason, const char *by,
                           const char *token, int64_t at_value, char *out,
                           size_t cap) {
    char number[32];
    const char *flag = banned ? "{\"banned\":true,\"reason\":" :
                                "{\"banned\":false,\"reason\":";
    size_t reason_size = on_json_string(reason, NULL, 0);
    size_t by_size = on_json_string(by, NULL, 0);
    size_t token_size = on_json_string(token, NULL, 0);
    int written = snprintf(number, sizeof number, "%lld", (long long)at_value);
    size_t need, at = 0;
    if (written <= 0 || (size_t)written >= sizeof number) return 0;
    if (!reason_size || !by_size || !token_size) return 0;
    need = strlen(flag) + reason_size + strlen(",\"at\":") + strlen(number) +
           strlen(",\"by\":") + by_size + strlen(",\"tok\":") + token_size + 1;
    if (!out) return need;
    if (cap < need + 1) return 0;
    if (!on_json_puts(out, cap, &at, flag)) return 0;
    if (!on_json_string(reason, out + at, cap - at)) return 0;
    at += reason_size;
    if (!on_json_puts(out, cap, &at, ",\"at\":") ||
        !on_json_puts(out, cap, &at, number) ||
        !on_json_puts(out, cap, &at, ",\"by\":")) return 0;
    if (!on_json_string(by, out + at, cap - at)) return 0;
    at += by_size;
    if (!on_json_puts(out, cap, &at, ",\"tok\":")) return 0;
    if (!on_json_string(token, out + at, cap - at)) return 0;
    at += token_size;
    if (!on_json_puts(out, cap, &at, "}")) return 0;
    return need;
}

size_t on_account_text_json(const char *text, char *out, size_t cap) {
    size_t need = on_json_string(text, NULL, 0);
    if (!out) return need;
    if (cap < need + 1) return 0;
    if (on_json_string(text, out, cap) != need) return 0;
    return need;
}

size_t on_account_flag_json(int official, char *out, size_t cap) {
    const char *value = official ? "true" : "false";
    size_t need = strlen(value);
    if (!out) return need;
    if (cap < need + 1) return 0;
    memcpy(out, value, need + 1);
    return need;
}

/* ----------------------------------------------------------------- parsing */

typedef struct {
    const char *s;
    size_t n, at;
    int bad, depth;
} OnJson;

static void jws(OnJson *j) {
    while (j->at < j->n && (j->s[j->at] == ' ' || j->s[j->at] == '\t' ||
                            j->s[j->at] == '\n' || j->s[j->at] == '\r')) j->at++;
}
static int jchar(OnJson *j, char wanted) {
    jws(j);
    if (j->at >= j->n || j->s[j->at] != wanted) return 0;
    j->at++;
    return 1;
}
static int jliteral(OnJson *j, const char *word) {
    size_t length = strlen(word);
    jws(j);
    if (j->at + length > j->n || memcmp(j->s + j->at, word, length)) return 0;
    j->at += length;
    return 1;
}
static int jhex4(OnJson *j, unsigned *out) {
    unsigned value = 0;
    if (j->at + 4 > j->n) return 0;
    for (int i = 0; i < 4; i++) {
        char c = j->s[j->at + i];
        int digit = (c >= '0' && c <= '9') ? c - '0' :
                    (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                    (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (digit < 0) return 0;
        value = value * 16 + (unsigned)digit;
    }
    j->at += 4;
    *out = value;
    return 1;
}
static void jutf8(char *out, size_t cap, size_t *at, unsigned code) {
    if (code < 0x80) {
        if (*at + 1 < cap) out[(*at)++] = (char)code;
    } else if (code < 0x800) {
        if (*at + 2 < cap) {
            out[(*at)++] = (char)(0xc0 | (code >> 6));
            out[(*at)++] = (char)(0x80 | (code & 0x3f));
        }
    } else if (code < 0x10000) {
        if (*at + 3 < cap && !(code >= 0xd800 && code < 0xe000)) {
            out[(*at)++] = (char)(0xe0 | (code >> 12));
            out[(*at)++] = (char)(0x80 | ((code >> 6) & 0x3f));
            out[(*at)++] = (char)(0x80 | (code & 0x3f));
        }
    } else if (*at + 4 < cap) {
        out[(*at)++] = (char)(0xf0 | (code >> 18));
        out[(*at)++] = (char)(0x80 | ((code >> 12) & 0x3f));
        out[(*at)++] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[(*at)++] = (char)(0x80 | (code & 0x3f));
    }
}
/* Reads a JSON string into out. Returns 1 on success; a string longer than cap
 * is truncated, which is what the bounded UI needs. */
static int jstring(OnJson *j, char *out, size_t cap) {
    size_t at = 0;
    jws(j);
    if (j->at >= j->n || j->s[j->at] != '"') return 0;
    j->at++;
    while (j->at < j->n) {
        unsigned char c = (unsigned char)j->s[j->at++];
        if (c == '"') {
            if (out && cap) out[at] = 0;
            return 1;
        }
        if (c == '\\') {
            if (j->at >= j->n) break;
            char esc = j->s[j->at++];
            if (esc == 'u') {
                unsigned code = 0;
                if (!jhex4(j, &code)) break;
                if (code >= 0xd800 && code < 0xdc00 && j->at + 6 <= j->n &&
                    j->s[j->at] == '\\' && j->s[j->at + 1] == 'u') {
                    size_t save = j->at;
                    unsigned low = 0;
                    j->at += 2;
                    if (jhex4(j, &low) && low >= 0xdc00 && low < 0xe000)
                        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    else j->at = save;
                }
                jutf8(out, cap, &at, code);
                continue;
            }
            char plain = esc == 'n' ? '\n' : esc == 't' ? '\t' :
                         esc == 'r' ? '\r' : esc == 'b' ? '\b' :
                         esc == 'f' ? '\f' : esc;
            if (at + 1 < cap && out) out[at++] = plain;
            continue;
        }
        if (c < 0x20) break;
        if (at + 1 < cap && out) out[at++] = (char)c;
    }
    if (out && cap) out[at] = 0;
    return 0;
}
static int jnumber(OnJson *j, int64_t *out) {
    size_t start = j->at;
    int negative = 0;
    int64_t value = 0;
    int digits = 0;
    jws(j);
    if (j->at < j->n && (j->s[j->at] == '-' || j->s[j->at] == '+')) {
        negative = j->s[j->at] == '-';
        j->at++;
    }
    while (j->at < j->n && (unsigned char)(j->s[j->at] - '0') <= 9) {
        if (digits < 18) value = value * 10 + (j->s[j->at] - '0');
        digits++;
        j->at++;
    }
    if (j->at < j->n && (j->s[j->at] == '.' || j->s[j->at] == 'e' ||
                         j->s[j->at] == 'E')) {
        int exponent_negative = 0, exponent = 0;
        if (j->s[j->at] == '.')
            while (j->at < j->n && (unsigned char)(j->s[j->at] - '0') <= 9) j->at++;
        if (j->at < j->n && (j->s[j->at] == 'e' || j->s[j->at] == 'E')) {
            j->at++;
            if (j->at < j->n && (j->s[j->at] == '-' || j->s[j->at] == '+')) {
                exponent_negative = j->s[j->at] == '-';
                j->at++;
            }
            while (j->at < j->n && (unsigned char)(j->s[j->at] - '0') <= 9) {
                if (exponent < 1000) exponent = exponent * 10 + (j->s[j->at] - '0');
                j->at++;
            }
        }
        (void)exponent;(void)exponent_negative;
    }
    if (!digits) { j->at = start; return 0; }
    *out = negative ? -value : value;
    return 1;
}
static void jskip(OnJson *j);
static void jskip_object(OnJson *j) {
    if (!jchar(j, '{')) { j->bad = 1; return; }
    if (jchar(j, '}')) return;
    for (;;) {
        char key[ON_LOGIN_SIZE];
        jws(j);
        if (!jstring(j, key, sizeof key)) { j->bad = 1; return; }
        if (!jchar(j, ':')) { j->bad = 1; return; }
        jskip(j);
        if (j->bad) return;
        if (jchar(j, ',')) continue;
        if (jchar(j, '}')) return;
        j->bad = 1;
        return;
    }
}
static void jskip(OnJson *j) {
    jws(j);
    if (j->at >= j->n) { j->bad = 1; return; }
    if (++j->depth > 32) { j->bad = 1; return; }
    char c = j->s[j->at];
    if (c == '{') jskip_object(j);
    else if (c == '[') {
        j->at++;
        if (jchar(j, ']')) { j->depth--; return; }
        for (;;) {
            jskip(j);
            if (j->bad) return;
            if (jchar(j, ',')) continue;
            if (jchar(j, ']')) break;
            j->bad = 1;
            return;
        }
    } else if (c == '"') {
        if (!jstring(j, NULL, 0)) j->bad = 1;
    } else if (c == 't') {
        if (!jliteral(j, "true")) j->bad = 1;
    } else if (c == 'f') {
        if (!jliteral(j, "false")) j->bad = 1;
    } else if (c == 'n') {
        if (!jliteral(j, "null")) j->bad = 1;
    } else {
        int64_t ignored = 0;
        if (!jnumber(j, &ignored)) j->bad = 1;
    }
    j->depth--;
}

int on_account_parse_admin(const char *json) {
    OnJson scanner = {json ? json : "", json ? strlen(json) : 0, 0, 0, 0};
    jws(&scanner);
    return scanner.at < scanner.n && !strncmp(scanner.s + scanner.at, "true", 4);
}

static int on_account_login_key(const char *key) {
    size_t length = strlen(key);
    if (length < 3 || length > 24) return 0;
    for (size_t i = 0; i < length; i++) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return 1;
}

int on_account_parse_bans(const char *json, OnBan *out, int cap) {
    OnJson scanner = {json ? json : "", json ? strlen(json) : 0, 0, 0, 0};
    int count = 0;
    if (!out || cap <= 0) return 0;
    jws(&scanner);
    if (scanner.at >= scanner.n || scanner.s[scanner.at] != '{') return 0;
    scanner.at++;
    if (jchar(&scanner, '}')) return 0;
    for (;;) {
        char login[ON_LOGIN_SIZE] = {0};
        int banned = 0;
        int64_t at = 0;
        char reason[ON_REASON_SIZE] = {0};
        jws(&scanner);
        if (!jstring(&scanner, login, sizeof login)) return count;
        if (!jchar(&scanner, ':')) return count;
        jws(&scanner);
        if (scanner.at >= scanner.n || scanner.s[scanner.at] != '{') {
            jskip(&scanner);
        } else {
            scanner.at++;
            scanner.depth = 1;
            if (!jchar(&scanner, '}')) {
                for (;;) {
                    char key[24] = {0};
                    jws(&scanner);
                    if (!jstring(&scanner, key, sizeof key)) return count;
                    if (!jchar(&scanner, ':')) return count;
                    jws(&scanner);
                    if (!strcmp(key, "banned")) {
                        if (jliteral(&scanner, "true")) banned = 1;
                        else if (jliteral(&scanner, "false")) banned = 0;
                        else jskip(&scanner);
                    } else if (!strcmp(key, "reason")) {
                        if (!jstring(&scanner, reason, sizeof reason)) jskip(&scanner);
                    } else if (!strcmp(key, "at")) {
                        if (!jnumber(&scanner, &at)) jskip(&scanner);
                    } else jskip(&scanner);
                    if (scanner.bad) return count;
                    if (jchar(&scanner, ',')) continue;
                    if (jchar(&scanner, '}')) break;
                    return count;
                }
            }
            scanner.depth = 0;
        }
        if (scanner.bad) return count;
        if (banned && on_account_login_key(login) && count < cap) {
            snprintf(out[count].login, sizeof out[count].login, "%s", login);
            snprintf(out[count].reason, sizeof out[count].reason, "%s", reason);
            out[count].at = at;
            count++;
        }
        if (jchar(&scanner, ',')) continue;
        if (jchar(&scanner, '}')) break;
        return count;
    }
    return count;
}

int on_account_parse_comments(const char *json, OnComment *out, int cap) {
    OnJson scanner = {json ? json : "", json ? strlen(json) : 0, 0, 0, 0};
    int count = 0;
    if (!out || cap <= 0) return -1;
    jws(&scanner);
    if (scanner.at >= scanner.n) return -1;
    if (scanner.s[scanner.at] != '{') return -1;
    scanner.at++;
    if (jchar(&scanner, '}')) return 0;
    for (;;) {
        char id[ON_COMMENT_ID_SIZE] = {0};
        OnComment entry = {{0}, {0}, {0}, 0, 0};
        int have_login = 0, have_text = 0;
        jws(&scanner);
        if (!jstring(&scanner, id, sizeof id)) return count;
        if (!jchar(&scanner, ':')) return count;
        jws(&scanner);
        if (scanner.at >= scanner.n || scanner.s[scanner.at] != '{') {
            jskip(&scanner);
            if (scanner.bad) return count;
        } else {
            scanner.at++;
            scanner.depth = 1;
            if (!jchar(&scanner, '}')) {
                for (;;) {
                    char key[24] = {0};
                    jws(&scanner);
                    if (!jstring(&scanner, key, sizeof key)) return count;
                    if (!jchar(&scanner, ':')) return count;
                    if (!strcmp(key, "login")) {
                        if (jstring(&scanner, entry.login, sizeof entry.login)) {
                            have_login = on_account_login_key(entry.login);
                            if (!have_login) entry.login[0] = 0;
                        } else jskip(&scanner);
                    } else if (!strcmp(key, "text")) {
                        if (jstring(&scanner, entry.text, sizeof entry.text))
                            have_text = 1;
                        else jskip(&scanner);
                    } else if (!strcmp(key, "at")) {
                        if (!jnumber(&scanner, &entry.at)) jskip(&scanner);
                    } else if (!strcmp(key, "hidden")) {
                        if (jliteral(&scanner, "true")) entry.hidden = 1;
                        else if (!jliteral(&scanner, "false")) jskip(&scanner);
                    } else jskip(&scanner);
                    if (scanner.bad) return count;
                    if (jchar(&scanner, ',')) continue;
                    if (jchar(&scanner, '}')) break;
                    return count;
                }
            }
            scanner.depth = 0;
        }
        if (have_login && have_text && on_account_login_key(id) && count < cap) {
            snprintf(entry.id, sizeof entry.id, "%s", id);
            out[count] = entry;
            count++;
            /* Keep the list oldest first, the same order the web client shows. */
            for (int i = count - 1; i > 0 && out[i - 1].at > out[i].at; i--) {
                OnComment swap = out[i - 1];
                out[i - 1] = out[i];
                out[i] = swap;
            }
        }
        if (jchar(&scanner, ',')) continue;
        if (jchar(&scanner, '}')) break;
        return count;
    }
    return count;
}

int on_account_is_banned(const OnBan *bans, int count, const char *login) {
    char name[ON_LOGIN_SIZE];
    if (!bans || count <= 0 || !on_account_normalize(name, login)) return 0;
    for (int i = 0; i < count; i++)
        if (!strcmp(bans[i].login, name)) return 1;
    return 0;
}
