/* ОГОРОД: виртуальная файловая система.
 *
 * Файлы проекта (project.cfg, сцены, сценарии) обычно лежат на диске, но
 * внутри APK их нужно взять из самого бинарника. Поэтому движок читает
 * файлы через og_read_file(): сначала встроенные, потом настоящие.
 * Встроенные файлы добавляет tools/pack_project.py — он генерирует
 * engine/android/og_project_data.h из каталога projects/. */
#ifndef OG_VFS_H
#define OG_VFS_H

#include <stddef.h>

/* Добавить встроенный файл. name — путь с '/' (например
 * "projects/oborona/scripts/game.og"). Данные копируются. */
void og_vfs_add(const char *name, const unsigned char *data, size_t len);

/* Подключить готовую таблицу, созданную pack_project.py. */
typedef struct {
    const char *name;
    const unsigned char *data;
    unsigned int len;
} OgVfsFile;
void og_vfs_add_table(const OgVfsFile *files, int count);

/* Содержимое файла проекта (не NULL-терминированный) или NULL.
 * Результат освобождается через og_vfs_free. */
const unsigned char *og_vfs_find(const char *name, size_t *len);

/* Чтение файла: сначала встроенные, затем диск. Возвращает malloc-буфер
 * с завершающим нулём либо NULL. */
char *og_read_file(const char *name);
void og_vfs_free(void *p);

int og_vfs_count(void);

#endif /* OG_VFS_H */
