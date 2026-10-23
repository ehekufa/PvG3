#ifndef PVG3_ONLINE_CONFIG_H_INCLUDED
#define PVG3_ONLINE_CONFIG_H_INCLUDED

/* Where the game talks to Firebase. Nothing here has to be edited: drop a
 * src/online_config_secret.h next to this header (see
 * online_config_secret.h.example, the file itself is gitignored) or let CI
 * generate one from the PVG3_DATABASE_HOST / PVG3_DATABASE_AUTH secrets, and
 * both values are replaced at build time.
 *
 * The address that ships with the sources is stored ENCODED (every byte XOR a
 * key, written as hex), so a plain-text read of the repository, of a fork or of
 * a search index no longer shows it. Be honest about what that buys: the key
 * sits next to the value, so this is hiding, not cryptography — anyone who
 * takes the APK apart can decode it. Real secrecy comes from the generated
 * online_config_secret.h, which never lands in git. */
#if defined(__has_include)
#if __has_include("online_config_secret.h")
#include "online_config_secret.h"
#endif
#endif

#ifndef PVG3_DATABASE_HOST
/* Same value as the public default, encoded with the key below. */
#define PVG3_DATABASE_HOST_ENC \
    "2a0e53625a56374c006d5d5d280857230e3f47412e1c567f115e20115038035c240118350d26"
#define PVG3_DATABASE_HOST_KEY "Zx4Qw7Rt2Yp9Mn6VbKj3"
#endif

/* Optional ?auth= token appended to every request. Leave empty for the public
 * catalog: accounts authenticate with their own per-account token instead. */
#ifndef PVG3_DATABASE_AUTH
#define PVG3_DATABASE_AUTH ""
#endif

#include <stddef.h>
#include <stdio.h>

/* Writes the configured database host into out (always NUL-terminated, never
 * longer than cap - 1 characters). A build-time override is already plain text
 * in a gitignored file; otherwise the encoded default is decoded. Both call
 * sites build their base URL once, so this runs a single time per process. */
static inline char *pvg3_database_host(char *out, size_t cap) {
    if (!out || cap == 0) return out;
    out[0] = 0;
#ifdef PVG3_DATABASE_HOST
    snprintf(out, cap, "%s", PVG3_DATABASE_HOST);
    return out;
#else
    {
    size_t i = 0, key_len = 0;
    const char *hex = PVG3_DATABASE_HOST_ENC;
    const char *key = PVG3_DATABASE_HOST_KEY;
    while (key[key_len]) key_len++;
    if (!key_len) return out;
    for (i = 0; hex[0] && hex[1] && i + 1 < cap; i++) {
        int nibble, value = 0;
        for (nibble = 0; nibble < 2; nibble++) {
            char c = hex[nibble];
            int digit = (c >= '0' && c <= '9') ? c - '0' :
                (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (digit < 0) { out[i] = 0; return out; }
            value = value * 16 + digit;
        }
        out[i] = (char)(unsigned char)(value ^ (unsigned char)key[i % key_len]);
        hex += 2;
    }
    out[i] = 0;
    return out;
    }
#endif
}

#endif /* PVG3_ONLINE_CONFIG_H_INCLUDED */
