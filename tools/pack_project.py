#!/usr/bin/env python3
"""Упаковывает каталоги projects/ в C-заголовок для Android.

Файлы игры (project.cfg, сцены, сценарии .og) кладутся прямо в .so, чтобы
APK был самодостаточным: внутри APK нечего распаковывать, движок читает их
через свою виртуальную файловую систему (engine/og_vfs.c).

    python3 tools/pack_project.py            # перегенерировать
    python3 tools/pack_project.py --check    # проверить, что файл свежий

Зависимостей нет, формат вывода детерминированный — как у pack_sprites.py.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECTS = os.path.join(ROOT, "projects")
OUT = os.path.join(ROOT, "engine", "android", "og_project_data.h")

# Расширения, которые уезжают в APK. Промежуточные файлы и скриншоты — нет.
WANTED = (".og", ".scene", ".cfg", ".txt")
SKIP_DIRS = {"shots", "__pycache__", "build"}


def collect():
    files = []
    for base, dirs, names in os.walk(PROJECTS):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for name in sorted(names):
            if not name.endswith(WANTED):
                continue
            full = os.path.join(base, name)
            rel = os.path.relpath(full, ROOT).replace(os.sep, "/")
            with open(full, "rb") as fh:
                files.append((rel, fh.read()))
    return files


def render(files):
    out = []
    out.append("/* Создано tools/pack_project.py из projects/ — не править руками.\n")
    out.append(" * Файлы игр ОгРога встроены в библиотеку, чтобы APK не нуждался\n")
    out.append(" * в распаковке assets. Формат тот же, что у pack_sprites.py. */\n")
    out.append("#ifndef OG_PROJECT_DATA_H\n#define OG_PROJECT_DATA_H\n\n")
    out.append('#include "og_vfs.h"\n\n')
    out.append("/* Всего файлов: %d */\n" % len(files))
    for i, (name, data) in enumerate(files):
        out.append("/* %s (%d байт) */\n" % (name, len(data)))
        out.append("static const unsigned char og_project_file_%d[] = {" % i)
        for off in range(0, len(data), 12):
            chunk = data[off:off + 12]
            out.append("\n    " + " ".join("0x%02x," % b for b in chunk))
        out.append("\n};\n\n")
    out.append("static const OgVfsFile OG_PROJECT_FILES[] = {\n")
    for i, (name, data) in enumerate(files):
        out.append('    { "%s", og_project_file_%d, %d },\n' % (name, i, len(data)))
    out.append("};\n\n")
    out.append("#define OG_PROJECT_FILE_COUNT %d\n\n" % len(files))
    out.append("/* Подключить все игры к виртуальной файловой системе. Повторный вызов\n")
    out.append(" * ничего не делает: хост можно перезапускать в одном процессе. */\n")
    out.append("static void og_project_install(void) {\n")
    out.append("    static int installed;\n")
    out.append("    if (installed) return;\n")
    out.append("    installed = 1;\n")
    out.append("    og_vfs_add_table(OG_PROJECT_FILES, OG_PROJECT_FILE_COUNT);\n")
    out.append("}\n\n")
    out.append("#endif /* OG_PROJECT_DATA_H */\n")
    return "".join(out)


def main(argv):
    check = "--check" in argv
    files = collect()
    if not files:
        sys.exit("pack_project: в projects/ не найдено файлов игр")
    text = render(files)
    if check:
        current = ""
        if os.path.exists(OUT):
            with open(OUT, "r", encoding="utf-8") as fh:
                current = fh.read()
        if current != text:
            sys.exit("pack_project: %s устарел, запусти python3 tools/pack_project.py" % OUT)
        print("pack_project: %d файлов, заголовок актуален" % len(files))
        return 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(text)
    total = sum(len(d) for _, d in files)
    print("Wrote %s (%d файлов, %d байт)" % (OUT, len(files), total))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
