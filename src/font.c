/* Smooth UTF-8 text for both Android and desktop screenshots. PT Sans Regular
 * is OFL-1.1 (assets/fonts/OFL.txt); stb_truetype is public domain / MIT.
 * This file rasterizes each codepoint/size only once and alpha-blends glyphs
 * into the game's software framebuffer. No installed system font is needed.
 */
#include "font.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"
#include "font_data.h"


typedef struct {const char *ru, *en;} FontTranslation;

static const FontTranslation translations[] = {
    {"Растения против гусей 3", "Plants vs. Geese 3"},
    {"Кампания", "Campaign"},
    {"Сад Дзен", "Zen Garden"},
    {"Уровни игроков", "Player Levels"},
    {"Хлебушек", "Bread"},
    {"Дима в маске", "Masked Dima"},
    {"Кирилл", "Kirill"},
    {"Умная книга", "Field Guide"},
    {"Начать игру", "Start Game"},
    {"Играть вдвоём", "Play Online"},
    {"Уровни", "Levels"},
    {"Назад", "Back"},
    {"Вода", "Water"},
    {"Финал", "Finale"},
    {"Уровень 0 · история", "Level 0 · Story"},
    {"Уровень 0 · история Кирилла", "Level 0 · Kirill's Story"},
    {"Каталог уровней", "Level Catalog"},
    {"Настройки", "Settings"},
    {"Язык интерфейса", "Interface language"},
    {"Русский", "Russian"},
    {"English", "English"},
    {"Закрыть", "Close"},
    {"Музыка", "Music"},
    {"Музыка: ВКЛ.", "Music: ON"},
    {"Музыка: ВЫКЛ.", "Music: OFF"},
    {"Фон уровня", "Level background"},
    {"По умолчанию", "Default"},
    {"Обычная картинка", "Original artwork"},
    {"Однотонный", "Plain"},
    {"Авторский", "Artwork"},
    {"Новый уровень", "New level"},
    {"К уровням", "Back to levels"},
    {"НАЗАД", "BACK"},
    {"ВПЕРЁД", "FORWARD"},
    {"ДЕЙСТВИЕ", "ACTION"},
    {"ПРЫЖОК", "JUMP"},
    {"ВВЕРХ", "UP"},
    {"ВНИЗ", "DOWN"},
    {"МОНЕТЫ", "COINS"},
    {"УРОВЕНЬ ПРОЙДЕН!", "LEVEL COMPLETE!"},
    {"ПУБЛИЧНЫЙ КАТАЛОГ УРОВНЕЙ", "PLAYER LEVEL CATALOG"},
    {"ОФИЦИАЛЬНЫЙ", "OFFICIAL"},
    {"Уровней пока нет", "No levels yet"},
    {"Опубликованные уровни появятся здесь.",
     "Published levels will appear here."},
    {"Мастерская", "Workshop"},
    {"Обновить", "Refresh"},
    {"Мастерская · мои уровни", "Workshop · My Levels"},
    {"ЛОКАЛЬНЫЕ ЧЕРНОВИКИ", "LOCAL DRAFTS"},
    {"Платформенный уровень · локальный черновик",
     "Platformer level · local draft"},
    {"Открыть", "Open"},
    {"Черновиков пока нет", "No drafts yet"},
    {"Отдельный платформенный редактор", "Separate platformer editor"},
    {"+  Новый уровень", "+  New Level"},
    {"Опубликованный каталог", "Published catalog"},
    {"Параметры уровня", "Level settings"},
    {"НАЗВАНИЕ", "TITLE"},
    {"ОПИСАНИЕ", "DESCRIPTION"},
    {"Открыть редактор", "Open editor"},
    {"Публиковать", "Publish"},
    {"Сохранить", "Save"},
    {"Предпросмотр", "Preview"},
    {"УРОВЕНЬ", "LEVEL"},
    {"РОБОТ", "ROBOT"},
    {"ВОЛНА", "WAVE"},
    {"КНИГА", "BOOK"},
    {"МЕНЮ", "MENU"},
    {"ДИМА", "DIMA"},
    {"ХЛЕБУШЕК", "BREAD"},
    {"КИРИЛЛ", "KIRILL"},
    {"УРОВНИ 1-10", "LEVELS 1-10"},
    {"СТАРТ", "START"},
    {"ОНЛАЙН", "ONLINE"},
    {"ВЫБОР УРОВНЯ", "LEVEL SELECT"},
    {"УРОВЕНЬ 0: КАТ-СЦЕНА", "LEVEL 0: STORY"},
    {"КАТАЛОГ УРОВНЕЙ", "LEVEL CATALOG"},
    {"НАЗАД", "BACK"},
    {"РАСТЕНИЯ", "PLANTS"},
    {"ГУСИ", "GEESE"},
    {"ГАЗОН", "LAWN"},
    {"В МЕНЮ", "TO MENU"},
    {"УБРАТЬ", "CLEAR"},
    {"ВРАГИ", "ENEMIES"},
    {"ПРОПУСТИТЬ", "SKIP"},
    {"УРОВЕНЬ ПРОЙДЕН!", "LEVEL COMPLETE!"},
    {"ВСЯ ВОЛНА ПОБЕЖДЕНА!", "WAVE CLEARED!"},
    {"СЛЕДУЮЩИЙ УРОВЕНЬ:", "NEXT LEVEL:"},
    {"ДАЛЬШЕ", "CONTINUE"},
    {"РОБОТ ОСТАНОВЛЕН!", "ROBOT STOPPED!"},
    {"ЗАЩИТА ПРОРВАНА!", "DEFENSE BREACHED!"},
    {"ПОПРОБУЙ ЕЩЁ РАЗ", "TRY AGAIN"},
    {"ПОВТОРИТЬ", "RETRY"},
    {"ПОБЕДА!", "VICTORY!"},
    {"ПОБЕДИЛ СОПЕРНИК", "OPPONENT WINS"},
    {"ПОБЕДИЛИ РАСТЕНИЯ", "PLANTS WIN"},
    {"УТКИ ПОБЕДИЛИ", "GEESE WIN"},
    {"В КОМНАТЫ", "BACK TO ROOMS"},
    {"МАЛО МОНЕТ", "NOT ENOUGH COINS"},
    {"КОМНАТЫ", "ROOMS"},
    {"ПОИСК", "SEARCH"},
    {"+ СОЗДАТЬ", "+ CREATE"},
    {"ОБНОВИТЬ", "REFRESH"},
    {"ПОДКЛЮЧАЕМСЯ...", "CONNECTING..."},
    {"ЗАГРУЖАЕМ КОМНАТЫ...", "LOADING ROOMS..."},
    {"ПОКА НЕТ СВОБОДНЫХ КОМНАТ", "NO OPEN ROOMS YET"},
    {"ВОЙТИ >", "JOIN >"},
    {"ПОИСК КОМНАТЫ", "FIND A ROOM"},
    {"КОД ИЗ 6 СИМВОЛОВ", "6-CHARACTER CODE"},
    {"СТЕРЕТЬ", "DELETE"},
    {"ВОЙТИ ПО КОДУ", "JOIN BY CODE"},
    {"КОМНАТА %s", "ROOM %s"},
    {"ВЫЙТИ", "EXIT"},
    {"КАРТА: ВОДА", "MAP: WATER"},
    {"КАРТА: ГАЗОН", "MAP: LAWN"},
    {"ЗОМБИ", "ZOMBIES"},
    {"ТВОЯ СТОРОНА", "YOUR SIDE"},
    {"СТОРОНА СОПЕРНИКА", "OPPONENT'S SIDE"},
    {"ВЫБРАТЬ СТОРОНУ", "CHOOSE A SIDE"},
    {"ЗАКОНЧИТЬ", "FINISH"},
    {"ОЖИДАЕМ ПОДТВЕРЖДЕНИЯ ХОДА...", "WAITING FOR TURN CONFIRMATION..."},
    {"ПОБЕДА!", "VICTORY!"},
    {"ПОБЕДИЛ СОПЕРНИК", "OPPONENT WINS"},
    {"РАСТЕНИЯ ПОБЕДИЛИ", "PLANTS WIN"},
    {"УТКИ ПОБЕДИЛИ", "GEESE WIN"},
    {"Кнопка действия", "Action button"},
    {"Старт уровня", "Level start"},
    {"Действие", "Action"},
    {"Игрок", "Player"},
    {"Финиш", "Finish"},
    {"Чекпоинт", "Checkpoint"},
    {"Монета", "Coin"},
    {"Блок", "Block"},
    {"Платформа", "Platform"},
    {"Утка", "Duck"},
    {"Триггер", "Trigger"},
    {"Цвет", "Color"},
    {"Фон", "Background"},
    {"Движение", "Movement"},
    {"Гравитация", "Gravity"},
    {"Портал Jetpack", "Jetpack Portal"},
    {"Обычный портал", "Normal Portal"},
    {"Прыжок", "Jump"},
    {"Действие", "Action"},
    {"Сохранить", "Save"},
    {"Публиковать", "Publish"},
    {"Предпросмотр", "Preview"},
    {"Обучение: ВКЛ.", "Tutorial: ON"},
    {"Обучение: ВЫКЛ.", "Tutorial: OFF"},
    /* Accounts, comments and moderation. */
    {"Аккаунт", "Account"},
    {"Ник", "Nickname"},
    {"Пароль", "Password"},
    {"Ник игрока", "Player nickname"},
    {"ник", "nickname"},
    {"a-z, 0-9 и _", "a-z, 0-9 and _"},
    {"от 6 знаков", "6+ characters"},
    {"Войти", "Sign in"},
    {"Создать аккаунт", "Create account"},
    {"Выйти", "Sign out"},
    {"Вы не в аккаунте.", "Not signed in."},
    {"Модератор", "Moderator"},
    {"Забанить", "Ban"},
    {"Разбанить", "Unban"},
    {"Нарушение правил", "Rule violation"},
    {"Плохое сообщение", "Abusive message"},
    {"Закрыть", "Close"},
    {"Играть", "Play"},
    {"Сообщения", "Comments"},
    {"Сообщения · ID", "Comments · ID"},
    {"Сообщений пока нет.", "No comments yet."},
    {"Загружаю сообщения…", "Loading comments…"},
    {"Написать сообщение…", "Write a comment…"},
    {"Отправить", "Send"},
    {"Скрыть", "Hide"},
    {"Сообщение скрыто.", "Comment hidden."},
    {"Официальный", "Official"},
    {"Снять метку", "Remove badge"},
    {"Отмена", "Cancel"},
    {"Ник · a-z, 0-9 и _", "Nickname · a-z, 0-9 and _"},
    {"Пароль · 6-72 знака", "Password · 6-72 characters"},
    {"Сообщение · до 300 знаков", "Comment · up to 300 characters"},
    {"Пароль не хранится и не уходит в базу: игра считает хэш PBKDF2 и "
     "обменивает его на токен сессии.",
     "The password is never stored or sent: the game derives a PBKDF2 hash and "
     "exchanges it for a session token."},
    /* Answers the database and the client give. */
    {"Ник: только a-z, 0-9 и _, от 3 до 24 знаков.",
     "Nickname: a-z, 0-9 and _ only, 3 to 24 characters."},
    {"Пароль: от 6 до 72 знаков без пробелов.",
     "Password: 6 to 72 characters, no spaces."},
    {"Такой аккаунт уже есть.", "That account already exists."},
    {"Неверный ник или пароль.", "Wrong nickname or password."},
    {"Вход выполнен.", "Signed in."},
    {"Вход выполнен. Вы модератор.", "Signed in. You are a moderator."},
    {"Вы вышли из аккаунта.", "Signed out."},
    {"Сначала войди в аккаунт.", "Sign in first."},
    {"Банить может только модератор.", "Only a moderator can ban."},
    {"Метку ставит только модератор.", "Only a moderator sets the badge."},
    {"Игрок забанен.", "Player banned."},
    {"Игрок разбанен.", "Player unbanned."},
    {"Уровень стал официальным.", "The level is official now."},
    {"Метка снята.", "Badge removed."},
    {"Пустое сообщение.", "Empty message."},
    {"Сообщение не найдено. Обнови список.",
     "Message not found. Refresh the list."},
    {"База не приняла аккаунт. Проверь правила Firebase.",
     "The database refused the account. Check the Firebase rules."},
    {"База не приняла сообщение. Возможно, ник забанен.",
     "The database refused the message. The nickname may be banned."},
    {"Скрыть сообщение может только его автор или модератор.",
     "Only the author or a moderator can hide a message."}
};

#define FONT_LANGUAGE_PATH_CAP 4096
static int selected_language = FONT_LANG_RU;
static char language_path[FONT_LANGUAGE_PATH_CAP];
static char translated_text[512];

int font_language(void) { return selected_language; }

void font_set_language_path(const char *path) {
    if (!path) {
        language_path[0] = 0;
        return;
    }
    size_t length = strlen(path);
    if (length >= sizeof language_path) return;
    memcpy(language_path, path, length + 1);
    FILE *file = fopen(language_path, "rb");
    if (!file) return;
    char saved[8] = {0};
    size_t count = fread(saved, 1, sizeof saved - 1, file);
    fclose(file);
    if (count >= 2 && saved[0] == 'e' && saved[1] == 'n')
        selected_language = FONT_LANG_EN;
    else if (count >= 2 && saved[0] == 'r' && saved[1] == 'u')
        selected_language = FONT_LANG_RU;
}

void font_set_language(int language) {
    if (language != FONT_LANG_RU && language != FONT_LANG_EN) return;
    selected_language = language;
    if (!language_path[0]) return;
    char temporary[FONT_LANGUAGE_PATH_CAP + 8];
    int length = snprintf(temporary, sizeof temporary, "%s.tmp", language_path);
    if (length <= 0 || (size_t)length >= sizeof temporary) return;
    FILE *file = fopen(temporary, "wb");
    if (!file) return;
    const char *value = language == FONT_LANG_EN ? "en\n" : "ru\n";
    size_t size = strlen(value);
    int ok = fwrite(value, 1, size, file) == size;
    if (fclose(file) != 0) ok = 0;
    if (ok) {
        remove(language_path);
        if (rename(temporary, language_path) != 0) remove(temporary);
    } else {
        remove(temporary);
    }
}

const char *font_translate(const char *text) {
    if (!text || selected_language != FONT_LANG_EN) return text;
    for (size_t i = 0; i < sizeof translations / sizeof translations[0]; ++i)
        if (!strcmp(text, translations[i].ru)) return translations[i].en;
    static const struct {const char *ru, *en;} prefixes[] = {
        {"МОНЕТЫ ", "COINS "},
        {"Объектов: ", "Objects: "},
        {"Выбрано объектов: ", "Selected objects: "},
        {"Палитра ", "Palette "},
        {"Комната ", "Room "}
    };
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; ++i) {
        size_t prefix = strlen(prefixes[i].ru);
        if (strncmp(text, prefixes[i].ru, prefix)) continue;
        int count = snprintf(translated_text, sizeof translated_text, "%s%s",
                             prefixes[i].en, text + prefix);
        if (count > 0 && (size_t)count < sizeof translated_text)
            return translated_text;
    }
    return text;
}

const unsigned char *font_ttf_data(size_t *length) {
    if (length) *length = PT_SANS_TTF_SIZE;
    return PT_SANS_TTF;
}

#define MAX_TEXT_SIZE 8
#define GLYPH_SLOTS 224          /* ASCII + Cyrillic U+0400..U+045F */

typedef struct {
    unsigned char *alpha;
    int w, h, xoff, yoff;
    int loaded;
} Glyph;

static stbtt_fontinfo face;
static float scales[MAX_TEXT_SIZE + 1];
static int baselines[MAX_TEXT_SIZE + 1];
static Glyph glyphs[MAX_TEXT_SIZE + 1][GLYPH_SLOTS];
static int initialized, valid;

int font_init(void) {
    if (initialized) return valid;
    initialized = 1;
    if (stbtt_GetFontOffsetForIndex(PT_SANS_TTF, 0) != 0 ||
        !stbtt_InitFont(&face, PT_SANS_TTF, 0)) return 0;
    for (int size = 1; size <= MAX_TEXT_SIZE; size++) {
        /* The cap height of PT Sans at 10*size is close to the old 7*size
         * layout, but with naturally proportioned, anti-aliased glyphs. */
        scales[size] = stbtt_ScaleForPixelHeight(&face, 10.0f * size);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&face, 'H', scales[size], scales[size],
                                    &x0, &y0, &x1, &y1);
        baselines[size] = -y0;
    }
    valid = 1;
    return 1;
}

int font_has_glyph(uint32_t codepoint) {
    return font_init() && stbtt_FindGlyphIndex(&face, (int)codepoint) != 0;
}

static uint32_t next_codepoint(const unsigned char **text) {
    unsigned char first = *(*text)++;
    if (first < 0x80) return first;
    int count = 0;
    uint32_t cp = 0;
    if ((first & 0xe0) == 0xc0) { count = 1; cp = first & 0x1f; }
    else if ((first & 0xf0) == 0xe0) { count = 2; cp = first & 0x0f; }
    else if ((first & 0xf8) == 0xf0) { count = 3; cp = first & 0x07; }
    else return '?';
    for (int i = 0; i < count; i++) {
        unsigned char b = **text;
        if ((b & 0xc0) != 0x80) return '?';
        (*text)++;
        cp = (cp << 6) | (b & 0x3f);
    }
    if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) ||
        (count == 3 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff)) return '?';
    return cp;
}

static uint32_t supported_codepoint(uint32_t cp) {
    if (cp >= 32 && cp <= 126) return cp;
    if (cp >= 0x400 && cp <= 0x45f) return cp;
    return '?';
}

static int glyph_slot(uint32_t cp) {
    if (cp <= 127) return (int)cp;
    return 128 + (int)(cp - 0x400);
}

static int advance_px(int size, uint32_t cp) {
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&face, (int)cp, &advance, &lsb);
    int px = (int)(advance * scales[size] + 0.5f);
    return px > 0 ? px : 1;
}

int font_width(int size, const char *utf8) {
    if (!font_init() || !utf8 || size < 1 || size > MAX_TEXT_SIZE) return 0;
    utf8 = font_translate(utf8);
    const unsigned char *p = (const unsigned char *)utf8;
    int width = 0;
    while (*p) width += advance_px(size, supported_codepoint(next_codepoint(&p)));
    return width;
}

static Glyph *rasterize(int size, uint32_t cp) {
    Glyph *g = &glyphs[size][glyph_slot(cp)];
    if (!g->loaded) {
        g->alpha = stbtt_GetCodepointBitmap(&face, scales[size], scales[size],
                                             (int)cp, &g->w, &g->h,
                                             &g->xoff, &g->yoff);
        g->loaded = 1; /* spaces have no bitmap, but still have an advance */
    }
    return g;
}

static uint32_t alpha_blend(uint32_t dst, uint32_t color, int alpha) {
    if (alpha == 255) return color | 0xff000000u;
    int inv = 255 - alpha;
    int r = ((int)(dst & 255) * inv + (int)(color & 255) * alpha) / 255;
    int g = ((int)((dst >> 8) & 255) * inv + (int)((color >> 8) & 255) * alpha) / 255;
    int b = ((int)((dst >> 16) & 255) * inv + (int)((color >> 16) & 255) * alpha) / 255;
    return 0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

void font_draw(uint32_t *rgba, int width, int height,
               int x, int y, int size, uint32_t color, const char *utf8) {
    if (!font_init() || !rgba || !utf8 || size < 1 || size > MAX_TEXT_SIZE) return;
    utf8 = font_translate(utf8);
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p) {
        uint32_t cp = supported_codepoint(next_codepoint(&p));
        Glyph *g = rasterize(size, cp);
        int gy = y + baselines[size] + g->yoff;
        int gx = x + g->xoff;
        if (g->alpha) {
            for (int row = 0; row < g->h; row++) {
                int dy = gy + row;
                if ((unsigned)dy >= (unsigned)height) continue;
                for (int col = 0; col < g->w; col++) {
                    int dx = gx + col;
                    if ((unsigned)dx >= (unsigned)width) continue;
                    int a = g->alpha[row * g->w + col];
                    if (a) {
                        uint32_t *pixel = &rgba[dy * width + dx];
                        *pixel = alpha_blend(*pixel, color, a);
                    }
                }
            }
        }
        x += advance_px(size, cp);
    }
}
