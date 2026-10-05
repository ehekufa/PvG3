#ifndef PVG3_PREFERENCES_H_INCLUDED
#define PVG3_PREFERENCES_H_INCLUDED

/* Per-install options shared by the renderer, native UI and platform audio. */
void preferences_set_path(const char *path);
int preferences_music_enabled(void);
void preferences_set_music_enabled(int enabled);
int preferences_neutral_background_enabled(void);
void preferences_set_neutral_background_enabled(int enabled);

#endif /* PVG3_PREFERENCES_H_INCLUDED */
