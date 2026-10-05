#include "preferences.h"

#include <stdio.h>
#include <string.h>

#define PREFERENCES_PATH_CAP 4096
#define PREFERENCES_VERSION 2

/* Keep authored artwork enabled by default; plain blue-gray is optional. */
static int music_enabled = 1;
static int neutral_background_enabled;
static char preferences_path[PREFERENCES_PATH_CAP];

static void save_preferences(void) {
    if (!preferences_path[0]) return;
    char temporary[PREFERENCES_PATH_CAP + 8];
    int length = snprintf(temporary, sizeof temporary, "%s.tmp",
                          preferences_path);
    if (length <= 0 || (size_t)length >= sizeof temporary) return;

    FILE *file = fopen(temporary, "wb");
    if (!file) return;
    int ok = fprintf(file,
        "PVG3-PREFERENCES %d\nmusic=%d\nneutral_background=%d\n",
        PREFERENCES_VERSION, music_enabled, neutral_background_enabled) > 0;
    if (fclose(file) != 0) ok = 0;
    if (ok) {
        remove(preferences_path);
        if (rename(temporary, preferences_path) != 0) remove(temporary);
    } else {
        remove(temporary);
    }
}

void preferences_set_path(const char *path) {
    if (!path) {
        preferences_path[0] = 0;
        return;
    }
    size_t length = strlen(path);
    if (length >= sizeof preferences_path) return;
    memcpy(preferences_path, path, length + 1);

    music_enabled = 1;
    neutral_background_enabled = 0;
    FILE *file = fopen(preferences_path, "rb");
    if (!file) return;

    char line[96];
    int version = 1;
    if (fgets(line, sizeof line, file)) {
        int parsed_version = 0;
        if (sscanf(line, "PVG3-PREFERENCES %d", &parsed_version) == 1)
            version = parsed_version;
    }
    while (fgets(line, sizeof line, file)) {
        if (!strncmp(line, "music=", 6) &&
            (line[6] == '0' || line[6] == '1'))
            music_enabled = line[6] == '1';
        else if (version >= PREFERENCES_VERSION &&
                 !strncmp(line, "neutral_background=", 19) &&
                 (line[19] == '0' || line[19] == '1'))
            neutral_background_enabled = line[19] == '1';
    }
    fclose(file);

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
