#ifndef PVG3_PREFERENCES_H_INCLUDED
#define PVG3_PREFERENCES_H_INCLUDED

#include <stddef.h>

/* Per-install options shared by the renderer, native UI and platform audio. */
void preferences_set_path(const char *path);
int preferences_music_enabled(void);
void preferences_set_music_enabled(int enabled);
int preferences_neutral_background_enabled(void);
void preferences_set_neutral_background_enabled(int enabled);
/* Workshop hints such as «ЗЕРКАЛО» are hidden until the player turns the
 * tutorial mode on; the choice is remembered between launches. */
int preferences_tutorial_hints_enabled(void);
void preferences_set_tutorial_hints_enabled(int enabled);
/* The signed-in player account, remembered between launches. The password is
 * never stored: only the nick, the stretched hash and the session token the
 * database handed out. Every getter returns 0 when nothing is stored. */
int preferences_account_login(char *out, size_t cap);
int preferences_account_token(char *out, size_t cap);
int preferences_account_hash(char *out, size_t cap);
int preferences_account_admin(void);
/* Passing login == NULL or an empty login signs the player out. */
void preferences_set_account(const char *login, const char *token,
                             const char *hash, int admin);

#endif /* PVG3_PREFERENCES_H_INCLUDED */
