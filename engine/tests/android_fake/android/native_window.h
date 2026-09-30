#ifndef OG_FAKE_ANDROID_NATIVE_WINDOW_H
#define OG_FAKE_ANDROID_NATIVE_WINDOW_H

int ANativeWindow_getWidth(void *win);
int ANativeWindow_getHeight(void *win);
void og_fake_window_set_size(int w, int h);

#endif
