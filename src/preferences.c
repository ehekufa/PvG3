#include "preferences.h"

#include <stdio.h>
#include <string.h>

#define PREFERENCES_PATH_CAP 4096

/* Music preserves the original launch behavior. The new neutral backdrop is
 * the default so the saturated green map/sky artwork is not shown unless the
 * player explicitly opts back into the author's backgrounds. */
static int music_enabled = 1;
static int neutral_background_enabled = 1;
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
        "PVG3-PREFERENCES 1\nmusic=%d\nneutral_background=%d\n",
        music_enabled, neutral_background_enabled) > 0;
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
    neutral_background_enabled = 1;
    FILE *file = fopen(preferences_path, "rb");
    if (!file) return;
    char line[96];
    while (fgets(line, sizeof line, file)) {
        if (!strncmp(line, "music=", 6) &&
            (line[6] == '0' || line[6] == '1'))
            music_enabled = line[6] == '1';
        else if (!strncmp(line, "neutral_background=", 19) &&
                 (line[19] == '0' || line[19] == '1'))
            neutral_background_enabled = line[19] == '1';
    }
    fclose(file);
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
