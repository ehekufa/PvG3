/* Заглушки Android API для тестов движка без устройства.
 * Нужны, чтобы обычным gcc собрать и запустить engine/android/og_android.c
 * и проверить его логику (выбор игры, загрузку проекта из встроенных
 * файлов, ввод) до настоящей сборки APK. */
#ifndef OG_FAKE_ANDROID_LOG_H
#define OG_FAKE_ANDROID_LOG_H

enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_ERROR = 6 };

int __android_log_print(int prio, const char *tag, const char *fmt, ...);
void og_fake_log_reset(void);
int og_fake_log_errors(void);

#endif
