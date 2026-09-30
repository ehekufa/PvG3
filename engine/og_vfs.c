/* ОГОРОД: виртуальная файловая система (встроенные файлы + диск). */
#define _POSIX_C_SOURCE 200809L
#include "og_vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    unsigned char *data;
    size_t len;
} Entry;

static Entry *entries;
static int count, capacity;

void og_vfs_add(const char *name, const unsigned char *data, size_t len) {
    if (!name || (!data && len)) return;
    if (count == capacity) {
        capacity = capacity ? capacity * 2 : 16;
        entries = (Entry *)realloc(entries, (size_t)capacity * sizeof(Entry));
    }
    entries[count].name = strdup(name);
    entries[count].data = (unsigned char *)malloc(len + 1);
    if (entries[count].data) {
        if (len) memcpy(entries[count].data, data, len);
        entries[count].data[len] = 0;
    }
    entries[count].len = len;
    count++;
}

/* Данные таблицы статические, но копируются вместе с именем — так все
 * встроенные файлы живут одинаково. */
void og_vfs_add_table(const OgVfsFile *files, int n) {
    for (int i = 0; i < n; i++) og_vfs_add(files[i].name, files[i].data, files[i].len);
}

int og_vfs_count(void) { return count; }

const unsigned char *og_vfs_find(const char *name, size_t *len) {
    if (!name) return NULL;
    for (int i = 0; i < count; i++)
        if (strcmp(entries[i].name, name) == 0) {
            if (len) *len = entries[i].len;
            return entries[i].data;
        }
    return NULL;
}

char *og_read_file(const char *name) {
    size_t len = 0;
    const unsigned char *p = og_vfs_find(name, &len);
    if (p) {
        char *buf = (char *)malloc(len + 1);
        if (!buf) return NULL;
        if (len) memcpy(buf, p, len);
        buf[len] = 0;
        return buf;
    }
    FILE *f = fopen(name, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

void og_vfs_free(void *p) { free(p); }
