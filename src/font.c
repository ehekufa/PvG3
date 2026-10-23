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
    {"Поиск по каталогу", "Search the catalog"},
    {"Все уровни", "All levels"},
    {"Все", "All"},
    {"Ничего не найдено", "No results"},
    {"Измени запрос или фильтры и попробуй снова.",
     "Change the query or filters and try again."},
    {"Любая сложность", "Any difficulty"},
    {"Лёгкая", "Easy"},
    {"Обычная", "Normal"},
    {"Сложная", "Hard"},
    {"Экспертная", "Expert"},
    {"Новые", "Newest"},
    {"Лайки", "Likes"},
    {"Дизлайки", "Dislikes"},
    {"Вход", "Enter"},
    {"Выход", "Exit"},
    {"Удержание · 0,25с", "Stay · 0.25s"},
    {"Режим касания", "Touch mode"},
    {"Счётчик", "Count"},
    {"Переключатель", "Toggle"},
    {"Появление", "Spawn"},
    {"Прозрачность", "Opacity"},
    {"Пульсация · ВКЛ.", "Pulse · ON"},
    {"Пульсация · ВЫКЛ.", "Pulse · OFF"},
    {"Тряска · ВКЛ.", "Shake · ON"},
    {"Тряска · ВЫКЛ.", "Shake · OFF"},
    {"Нравится", "Like"},
    {"Убрать лайк", "Unlike"},
    {"Мастерская · мои уровни", "Workshop · My Levels"},
    {"ЛОКАЛЬНЫЕ ЧЕРНОВИКИ", "LOCAL DRAFTS"},
    {"Платформенный уровень · локальный черновик",
     "Platformer level · local draft"},
    {"Открыть", "Open"},
    {"Черновиков пока нет", "No drafts yet"},
    {"Отдельный платформенный редактор",
     "Separate platformer editor"},
    {"+  Новый уровень", "+  New Level"},
    {"Опубликованный каталог", "Published catalog"},
    {"Параметры уровня", "Level settings"},
    {"НАЗВАНИЕ", "TITLE"},
    {"ОПИСАНИЕ", "DESCRIPTION"},
    {"Открыть редактор", "Open editor"},
    {"Публиковать", "Publish"},
    {"Гость", "Guest"},
    {"Войди или создай аккаунт — уровень опубликуется сразу.",
     "Sign in or create an account — your level will publish right away."},
    {"Не удалось начать публикацию.", "Couldn't start publishing."},
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
    {"РАСТЕНИЯ", "PLANTS"},
    {"ГУСИ", "GEESE"},
    {"ГАЗОН", "LAWN"},
    {"В МЕНЮ", "TO MENU"},
    {"УБРАТЬ", "CLEAR"},
    {"ВРАГИ", "ENEMIES"},
    {"ПРОПУСТИТЬ", "SKIP"},
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
    {"ОЖИДАЕМ ПОДТВЕРЖДЕНИЯ ХОДА...",
     "WAITING FOR TURN CONFIRMATION..."},
    {"РАСТЕНИЯ ПОБЕДИЛИ", "PLANTS WIN"},
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
    {"%d + РОБОТ", "%d + ROBOT"},
    {"%d ВРАГОВ", "%d ENEMIES"},
    {"БЕЗ НАЗВАНИЯ", "UNTITLED"},
    {"В ВОДЕ СНАЧАЛА ПОСАДИ КУВШИНКУ!", "PLANT A LILY PAD FIRST!"},
    {"ВВЕДИ ВСЕ ШЕСТЬ СИМВОЛОВ КОДА",
     "ENTER ALL SIX CODE CHARACTERS"},
    {"ВЕДРО", "BUCKET"},
    {"ВЕДРО ЗАЩИЩАЕТ УТКУ ОТ УДАРОВ.",
     "THE BUCKET PROTECTS THE DUCK."},
    {"ВОДА", "WATER"},
    {"ВОДЯНОЙ УРОВЕНЬ", "WATER LEVEL"},
    {"ГОРОХОСТРЕЛ", "PEASHOOTER"},
    {"ГРУППА / СЛОЙ", "GROUP / LAYER"},
    {"ГУСЬ", "GOOSE"},
    {"ДВИЖЕНИЕ", "MOVEMENT"},
    {"ДЕРЖИ ОБОРОНУ", "HOLD THE LINE"},
    {"ДЖАМПЕР", "JUMPER"},
    {"ДЖАМПЕР-БОЕЦ", "JUMPER FIGHTER"},
    {"ДОЖДИСЬ ПОДТВЕРЖДЕНИЯ ПРЕДЫДУЩЕГО ХОДА",
     "WAIT FOR THE PREVIOUS TURN TO CONFIRM"},
    {"ДРУГИЕ РАСТЕНИЯ СТАВЯТСЯ ПОВЕРХ НЕЁ.",
     "OTHER PLANTS GO ON TOP OF IT."},
    {"ЖДЁМ ВТОРОГО ИГРОКА...", "WAITING FOR THE SECOND PLAYER..."},
    {"ИГРАЙ ЗА РАСТЕНИЯ ИЛИ УТОК", "PLAY AS PLANTS OR DUCKS"},
    {"ИЗМЕНИТЬ", "EDIT"},
    {"ИСТОРИЯ ХЛЕБУШКА", "BREAD'S STORY"},
    {"КОМНАТА %s    УТОК ОСТАЛОСЬ: %d",
     "ROOM %s    DUCKS LEFT: %d"},
    {"КОНУС", "CONE"},
    {"КОНУС ЗАЩИЩАЕТ ТУ ЖЕ УТКУ.",
     "THE CONE PROTECTS THE SAME DUCK."},
    {"КОРОЛЕВА БЛИЗКО", "THE QUEEN IS CLOSE"},
    {"КОРОЛЕВА В РОБОТЕ", "QUEEN IN THE ROBOT"},
    {"КОРОЛЕВА В РОБОТЕ!", "QUEEN IN THE ROBOT!"},
    {"КОРОЛЕВА УПРАВЛЯЕТ БОЛЬШИМ РОБОТОМ.",
     "THE QUEEN PILOTS A BIG ROBOT."},
    {"КУВШИНКА", "LILY PAD"},
    {"ЛОКАЛЬНЫЙ ЧЕРНОВИК ПЛАТФОРМЕННОГО УРОВНЯ",
     "LOCAL PLATFORMER DRAFT"},
    {"ЛОКАЛЬНЫХ ЧЕРНОВИКОВ ПОКА НЕТ", "NO LOCAL DRAFTS YET"},
    {"МАСТЕРСКАЯ", "WORKSHOP"},
    {"МОЖЕТ, КТО-ТО ПОМОГАТЬ МНЕ БУДЕТ?",
     "MAYBE SOMEONE WILL HELP ME?"},
    {"МОИ УРОВНИ", "MY LEVELS"},
    {"МОНЕТ ИЛИ ПЕРЕЗАРЯДКИ НЕ ХВАТАЕТ",
     "NOT ENOUGH COINS OR RECHARGE"},
    {"МОНЕТ ИЛИ УТОК НЕ ХВАТАЕТ", "NOT ENOUGH COINS OR DUCKS"},
    {"МОНЕТЫ %d", "COINS %d"},
    {"НА ПОДСТУПАХ", "ON THE APPROACH"},
    {"НАЙДЕНО: %d", "FOUND: %d"},
    {"НАРИСОВАННАЯ АВТОРОМ УТКА-ЗОМБИ.",
     "THE AUTHOR'S HAND-DRAWN ZOMBIE DUCK."},
    {"НЕ ПЛАЧЬ, МЫ НОВОГО СДЕЛАЕМ.",
     "DON'T CRY, WE'LL MAKE A NEW ONE."},
    {"НЕ СТРЕЛЯЕТ И НЕ ЗАМОРАЖИВАЕТ.",
     "IT DOESN'T SHOOT OR FREEZE."},
    {"НЕВОЗМОЖНЫЙ ХОД: ПРОВЕРЬ КЛЕТКУ И МОНЕТЫ",
     "IMPOSSIBLE MOVE: CHECK THE CELL AND COINS"},
    {"НОВАЯ ВОЛНА", "NEW WAVE"},
    {"НОВЫЙ УРОВЕНЬ", "NEW LEVEL"},
    {"ОБЪЕКТЫ И ТРИГГЕРЫ", "OBJECTS AND TRIGGERS"},
    {"ОПУБЛИКОВАННЫЕ", "PUBLISHED"},
    {"ОРЕХ", "WALNUT"},
    {"ОТБРАСЫВАЕТ УТКУ И РОБОТА НА 3 КЛЕТКИ.",
     "KNOCKS DUCKS AND THE ROBOT BACK 3 TILES."},
    {"ОТКРЫТЬ РЕДАКТОР", "OPEN EDITOR"},
    {"ПЕРВАЯ ЗАЩИТА", "FIRST DEFENSE"},
    {"ПЕРЕД БУРЕЙ", "BEFORE THE STORM"},
    {"ПОДСОЛНУХ", "SUNFLOWER"},
    {"ПОДСОЛНУХ-НАРКОМАН", "STONER SUNFLOWER"},
    {"ПОДСОЛНУХ-НАРКОМАН ДАЁТ МОНЕТЫ",
     "THE STONER SUNFLOWER GIVES COINS"},
    {"ПОДХОДЯЩИХ КОМНАТ: %d", "MATCHING ROOMS: %d"},
    {"ПОМОГАЕТ ДРУГИМ РАСТЕНИЯМ.", "IT HELPS OTHER PLANTS."},
    {"ПОСЛЕДНИЙ РУБЕЖ", "LAST STAND"},
    {"ПОЯВЛЯЕТСЯ В КОНЦЕ УРОВНЯ 10. ДВИЖЕТСЯ МЕДЛЕННО.",
     "APPEARS AT THE END OF LEVEL 10. MOVES SLOWLY."},
    {"ПРИ УДАРЕ ИСЧЕЗАЕТ.", "DISAPPEARS ON HIT."},
    {"ПРОСМОТР", "VIEW"},
    {"ПРОЧНАЯ ПРЕГРАДА НА ПУТИ УТОК.",
     "A SOLID BARRIER IN THE DUCKS' PATH."},
    {"РАСТЁТ В ВОДЕ: ОСНОВА ДЛЯ ПОСАДКИ.",
     "GROWS IN WATER: A BASE FOR PLANTS."},
    {"РАСТЕНИЯ ПРОТИВ ГУСЕЙ", "PLANTS VS. GEESE"},
    {"РЕДАКТОР УРОВНЯ", "LEVEL EDITOR"},
    {"РОБОТ КОРОЛЕВЫ", "QUEEN'S ROBOT"},
    {"РОБОТ УНИЧТОЖИЛ ВСЕХ!", "THE ROBOT DESTROYED EVERYONE!"},
    {"САД ДЗЕН", "ZEN GARDEN"},
    {"СВОБОДНО СТАВЬ РАСТЕНИЯ И ГУСЕЙ. ВСЁ БЕСПЛАТНО.",
     "PLACE PLANTS AND GEESE FREELY. EVERYTHING IS FREE."},
    {"СЛОЖНЕЕ И СЛОЖНЕЕ", "HARDER AND HARDER"},
    {"СОЗДАЁТ МОНЕТЫ ДЛЯ ПОКУПКИ РАСТЕНИЙ.",
     "MAKES COINS TO BUY PLANTS."},
    {"СОЗДАТЬ УРОВЕНЬ", "CREATE LEVEL"},
    {"СОХРАНИТЬ", "SAVE"},
    {"СТРЕЛЯЕТ ПО УТКАМ В СВОЁМ РЯДУ.",
     "SHOOTS DUCKS IN ITS OWN LANE."},
    {"СТРОИТЬ", "BUILD"},
    {"У ЗАБОРА", "AT THE FENCE"},
    {"УДАЛИТЬ", "DELETE"},
    {"УМНАЯ КНИГА КИРИЛЛА", "KIRILL'S FIELD GUIDE"},
    {"УРОВЕНЬ %d", "LEVEL %d"},
    {"УРОВНИ ИГРОКОВ", "PLAYER LEVELS"},
    {"УРОН: 20 ЗА ПОПАДАНИЕ.", "DAMAGE: 20 PER HIT."},
    {"УТКА С ВЕДРОМ", "BUCKET DUCK"},
    {"УТКА С КОНУСОМ", "CONE DUCK"},
    {"УТКА-ЗОМБИ", "ZOMBIE DUCK"},
    {"ХЛЕБУШЕК ПЛАЧЕТ...", "BREAD IS CRYING..."},
    {"ХОЗЯИН ЗАПУСКАЕТ БОЙ...",
     "THE HOST IS STARTING THE MATCH..."},
    {"ЦВЕТ", "COLOR"},
    {"ЧЕМ ДАЛЬШЕ УРОВЕНЬ, ТЕМ БОЛЬШЕ ЗДОРОВЬЯ.",
     "THE FURTHER THE LEVEL, THE MORE HEALTH."},
    {"%.1f с", "%.1f s"},
    {"%d px/с", "%d px/s"},
    {"%d px/с²", "%d px/s²"},
    {"%d с", "%d s"},
    {"+  Добавить группу", "+  Add group"},
    {"+ Создать", "+ Create"},
    {"+ Создать комнату", "+ Create room"},
    {"ID группы", "Group ID"},
    {"ID целевой группы", "Target group ID"},
    {"Z-порядок", "Z-order"},
    {"Активировать навсегда", "Activate forever"},
    {"БЛОКИРОВКА X", "LOCK X"},
    {"БЛОКИРОВКА Y", "LOCK Y"},
    {"БЫСТРЫЙ ВЫБОР СЛОЯ", "QUICK LAYER PICK"},
    {"Базовая", "Basic"},
    {"Без звука", "Silent"},
    {"Без столкн.", "No coll."},
    {"Блоки", "Blocks"},
    {"В меню", "To menu"},
    {"ВАРИАНТЫ БЛОКОВ", "BLOCK VARIANTS"},
    {"ВАРИАНТЫ ОРБА", "ORB VARIANTS"},
    {"ВАРИАНТЫ ПОРТАЛА", "PORTAL VARIANTS"},
    {"ВАРИАНТЫ ТРИГГЕРА", "TRIGGER VARIANTS"},
    {"ВЫБРАННЫЙ ОБЪЕКТ", "SELECTED OBJECT"},
    {"Введи все шесть символов кода.",
     "Type all six code characters."},
    {"Ввод значения", "Value input"},
    {"Вернуться к комнатам", "Back to rooms"},
    {"Вечно", "Forever"},
    {"Вид триггера", "Trigger kind"},
    {"Вкл", "On"},
    {"Войти  >", "Join  >"},
    {"Войти в комнату", "Join room"},
    {"Время вращения · 1–9999 секунд",
     "Rotation time · 1–9999 seconds"},
    {"Время жизни", "Lifetime"},
    {"Время перемещения", "Move time"},
    {"Время, секунды", "Time, seconds"},
    {"Всплеск", "Burst"},
    {"Вставить", "Paste"},
    {"Вставлено объектов: %d.", "Pasted objects: %d."},
    {"Вход", "In"},
    {"Выбрано объектов: %d", "Selected objects: %d"},
    {"Выбрать", "Select"},
    {"Выйти", "Exit"},
    {"Выкл", "Off"},
    {"Высота", "Height"},
    {"Высота объекта · 0,1–40", "Object height · 0.1–40"},
    {"Выход", "Out"},
    {"Газон", "Lawn"},
    {"Готово", "Done"},
    {"Гравитац.", "Gravity"},
    {"Группа", "Group"},
    {"Группа триггера · 0–9999", "Trigger group · 0–9999"},
    {"Группы, слои и порядок", "Groups, layers and order"},
    {"Гуси", "Geese"},
    {"ДВИГАТЬ ОБЪЕКТЫ", "MOVE OBJECTS"},
    {"Дальше", "Next"},
    {"Дальше ›", "Next ›"},
    {"Деактивировать навсегда", "Deactivate forever"},
    {"Динамический режим", "Dynamic mode"},
    {"Достигнут общий лимит %d объектов.",
     "Total limit of %d objects reached."},
    {"Друг вошёл", "Friend joined"},
    {"Ждём друга", "Waiting for a friend"},
    {"Жёлтый", "Yellow"},
    {"Жёлтый орб", "Yellow orb"},
    {"ЗЕРКАЛО", "MIRROR"},
    {"Закончить", "Finish"},
    {"Защита прорвана", "Defense breached"},
    {"Здесь пока тихо", "Quiet here for now"},
    {"Зомби", "Zombie"},
    {"Играем вместе", "Playing together"},
    {"Игрок · ID 2", "Player · ID 2"},
    {"Ищем комнаты...", "Searching for rooms..."},
    {"Камера", "Camera"},
    {"Карта X %d  Y %d", "Map X %d  Y %d"},
    {"Касание", "Touch"},
    {"Книга", "Book"},
    {"Комната %s", "Room %s"},
    {"Комната %s  ·  осталось: %d", "Room %s  ·  left: %d"},
    {"Копировать", "Copy"},
    {"Мало монет", "Not enough coins"},
    {"Малый шаг", "Small step"},
    {"Может, кто-то помогать мне будет?",
     "Maybe someone will help me?"},
    {"Мульти", "Multi"},
    {"Название уровня", "Level title"},
    {"Найти комнату по коду", "Find a room by code"},
    {"Направление", "Direction"},
    {"Настроить", "Configure"},
    {"Настроить P", "Configure P"},
    {"Настроить команду перемещения", "Configure the move command"},
    {"Настройка триггера · %s", "Trigger setup · %s"},
    {"Настройка эмиттера частиц · P", "Particle emitter setup · P"},
    {"Не плачь, мы нового сделаем.",
     "Don't cry, we'll make a new one."},
    {"Невидим.", "Invis."},
    {"Невидимость", "Invisibility"},
    {"Нет", "None"},
    {"Нет места для вставки: достигнут лимит объектов.",
     "No room to paste: object limit reached."},
    {"Нет связи", "No connection"},
    {"Нет столкновения", "No collision"},
    {"Объектов: %d / %d", "Objects: %d / %d"},
    {"Обычный", "Normal"},
    {"Описание уровня", "Level description"},
    {"Оранжевый", "Orange"},
    {"Оранжевый орб", "Orange orb"},
    {"Орбы", "Orbs"},
    {"ПОВОРОТ", "ROTATE"},
    {"ПРЕДПРОСМОТР", "PREVIEW"},
    {"Палитра %d", "Palette %d"},
    {"Пастель", "Pastel"},
    {"Перекрас.", "Recolor"},
    {"Перекраска", "Recoloring"},
    {"Плавно", "Smooth"},
    {"Плавность", "Easing"},
    {"Платформа · ID 1", "Platform · ID 1"},
    {"По ум.", "Def."},
    {"Победа!", "Victory!"},
    {"Победил соперник", "Opponent wins"},
    {"Повторить", "Retry"},
    {"Повторить поиск", "Search again"},
    {"Подбор монеты", "Coin pickup"},
    {"Подождите", "Please wait"},
    {"Поиск", "Search"},
    {"Порталы", "Portals"},
    {"Поток", "Stream"},
    {"Появление", "Spawn"},
    {"Пропустить", "Skip"},
    {"Противники", "Enemies"},
    {"Публикуем уровень в общий каталог…",
     "Publishing the level to the shared catalog…"},
    {"РЕЖИМ РЕДАКТОРА", "EDITOR MODE"},
    {"Разброс", "Spread"},
    {"Разворот", "Flip"},
    {"Размер", "Size"},
    {"Размер и свободный поворот объекта",
     "Object size and free rotation"},
    {"Размер/угол", "Size/angle"},
    {"Растения", "Plants"},
    {"Растения спасли сад", "Plants saved the garden"},
    {"Режим", "Mode"},
    {"Режим цели", "Target mode"},
    {"Робот остановлен!", "Robot stopped!"},
    {"Сбросить настройки", "Reset settings"},
    {"Свечение", "Glow"},
    {"Свободные комнаты", "Open rooms"},
    {"Свободный угол · 0–360 градусов",
     "Free angle · 0–360 degrees"},
    {"Сила гравитации", "Gravity strength"},
    {"Склон", "Slope"},
    {"Скопировано объектов: %d.", "Copied objects: %d."},
    {"Скорость", "Speed"},
    {"Следующий уровень", "Next level"},
    {"Слой L", "Layer L"},
    {"Слой L2", "Layer L2"},
    {"Смещение X", "Offset X"},
    {"Смещение X · −9999…9999", "Offset X · −9999…9999"},
    {"Смещение Y", "Offset Y"},
    {"Смещение Y · −9999…9999", "Offset Y · −9999…9999"},
    {"Событие", "Event"},
    {"Стереть", "Erase"},
    {"Сторона занята", "Side taken"},
    {"Строить", "Build"},
    {"Триггер · %s · ID %d", "Trigger · %s · ID %d"},
    {"%s · ID %d", "%s · ID %d"},
    {"Убрать", "Clear"},
    {"Угол, градусы", "Angle, degrees"},
    {"Удалены выбранные объекты: %d.",
     "Deleted selected objects: %d."},
    {"Удалить", "Delete"},
    {"Уникальные элементы уровня обязательны.",
     "Unique level elements are required."},
    {"Уровень", "Level"},
    {"Уровень %d  ·  Осталось: %d", "Level %d  ·  Left: %d"},
    {"Уровень %d  ·  Робот: %d%%", "Level %d  ·  Robot: %d%%"},
    {"Уровень опубликован.", "Level published."},
    {"Уровень пройден!", "Level complete!"},
    {"Утки закончились", "Ducks are gone"},
    {"Утки захватили сад", "Ducks took the garden"},
    {"Финиш · ID 3", "Finish · ID 3"},
    {"Хлебушек плачет...", "Bread is crying..."},
    {"Холодная", "Cold"},
    {"ЦЕЛИ", "GOALS"},
    {"Цвет объекта", "Object color"},
    {"Цвет объектов", "Object color"},
    {"Цвет фона", "Background color"},
    {"Целевая группа", "Target group"},
    {"Цели", "Goals"},
    {"Частиц/всплеск", "Particles/burst"},
    {"Частиц/с", "Particles/s"},
    {"Шипы", "Spikes"},
    {"Ширина", "Width"},
    {"Ширина объекта · 0,1–64", "Object width · 0.1–64"},
    {"Эмиссия", "Emission"},
    {"Эмиттер частиц", "Particle emitter"},
    {"Яркая", "Bright"},
    {"‹ Назад", "‹ Back"},
    {"FIREBASE ЗАПРЕТИЛ ДОСТУП К /ROOMS",
     "FIREBASE DENIED ACCESS TO /ROOMS"},
    {"FIREBASE ЗАПРЕТИЛ ЗАПИСЬ В /LEVELS. ПРОВЕРЬ ПРАВИЛА.",
     "FIREBASE DENIED WRITES TO /LEVELS. CHECK THE RULES."},
    {"Firebase запретил чтение /levels-index. Проверь правила.",
     "Firebase denied reading /levels-index. Check the rules."},
    {"Firebase запретил чтение уровня. Проверь правила /levels.",
     "Firebase denied reading the level. Check the /levels rules."},
    {"КОМНАТА ЗАКРЫЛАСЬ. ВЕРНУЛИСЬ К СПИСКУ.",
     "THE ROOM CLOSED. BACK TO THE LIST."},
    {"КОМНАТА ЗАКРЫТА ИЛИ УЖЕ ЗАНЯТА",
     "THE ROOM IS CLOSED OR ALREADY FULL"},
    {"КОМНАТА ЗАНЯТА. ОБНОВИ СПИСОК.",
     "THE ROOM IS FULL. REFRESH THE LIST."},
    {"Каталог уровней повреждён или слишком велик.",
     "The level catalog is damaged or too large."},
    {"МЕСТО УЖЕ ЗАНЯЛ ДРУГОЙ ИГРОК",
     "ANOTHER PLAYER TOOK THE SLOT"},
    {"НЕ УДАЛОСЬ ЗАПУСТИТЬ ПУБЛИКАЦИЮ",
     "COULD NOT START PUBLISHING"},
    {"НЕ УДАЛОСЬ ЗАПУСТИТЬ СЕТЬ", "COULD NOT START THE NETWORK"},
    {"НЕ УДАЛОСЬ НАЙТИ СВОБОДНЫЙ ID УРОВНЯ",
     "COULD NOT FIND A FREE LEVEL ID"},
    {"НЕ УДАЛОСЬ ОПУБЛИКОВАТЬ УРОВЕНЬ. ПРОВЕРЬ ИНТЕРНЕТ.",
     "COULD NOT PUBLISH THE LEVEL. CHECK THE INTERNET."},
    {"НЕ УДАЛОСЬ ПРОЧИТАТЬ СПИСОК КОМНАТ",
     "COULD NOT READ THE ROOM LIST"},
    {"НЕ УДАЛОСЬ СОБРАТЬ ХОД", "COULD NOT BUILD THE TURN"},
    {"НЕ ХВАТИЛО ПАМЯТИ ДЛЯ БОЯ", "OUT OF MEMORY FOR THE MATCH"},
    {"НЕ ХВАТИЛО ПАМЯТИ ДЛЯ ПУБЛИКАЦИИ",
     "OUT OF MEMORY FOR PUBLISHING"},
    {"НЕ ХВАТИЛО ПАМЯТИ ДЛЯ СЕТИ", "OUT OF MEMORY FOR THE NETWORK"},
    {"НЕВЕРНОЕ СОСТОЯНИЕ КОМНАТЫ", "INVALID ROOM STATE"},
    {"НЕТ СВЯЗИ С FIREBASE. ПРОВЕРЬ ИНТЕРНЕТ.",
     "NO CONNECTION TO FIREBASE. CHECK THE INTERNET."},
    {"Не удалось загрузить уровень. Проверь интернет.",
     "Could not load the level. Check the internet."},
    {"Не удалось получить каталог уровней. Проверь интернет.",
     "Could not fetch the level catalog. Check the internet."},
    {"ОШИБКА FIREBASE. ПОПРОБУЙ ЕЩЁ РАЗ.",
     "FIREBASE ERROR. TRY AGAIN."},
    {"ОШИБКА СОСТОЯНИЯ БОЯ", "MATCH STATE ERROR"},
    {"СЛИШКОМ МНОГО КОМНАТ. ОТВЕТ БОЛЬШОЙ.",
     "TOO MANY ROOMS. THE ANSWER IS TOO BIG."},
    {"УРОВЕНЬ ЗАПИСАН, НО FIREBASE ЗАПРЕТИЛ /LEVELS-INDEX. ПРОВЕРЬ ПРАВИЛА.",
     "LEVEL SAVED, BUT FIREBASE DENIED /LEVELS-INDEX. CHECK THE RULES."},
    {"УРОВЕНЬ ЗАПИСАН, НО НЕ УДАЛОСЬ ДОБАВИТЬ ЕГО В КАТАЛОГ.",
     "LEVEL SAVED, BUT IT COULD NOT BE ADDED TO THE CATALOG."},
    {"УРОВЕНЬ ЗАПИСАН, НО НЕ УДАЛОСЬ ПОДГОТОВИТЬ КАТАЛОГ.",
     "LEVEL SAVED, BUT THE CATALOG COULD NOT BE PREPARED."},
    {"УРОВЕНЬ ОПУБЛИКОВАН · ID %s", "LEVEL PUBLISHED · ID %s"},
    {"Уровень не найден или его формат не поддерживается.",
     "Level not found or its format is not supported."},
    {"ЧЕРНОВИК ПОВРЕЖДЁН ИЛИ НЕ ПОДДЕРЖИВАЕТСЯ",
     "THE DRAFT IS DAMAGED OR UNSUPPORTED"},
    {"Гусь", "Goose"},
    {"Не удалось открыть окно PvG3.",
     "Could not open the PvG3 window."},
    {"Не удалось создать окно PvG3.",
     "Could not create the PvG3 window."},
    {"Действие", "Action"},
    {"Сохранить", "Save"},
    {"Публиковать", "Publish"},
    {"Предпросмотр", "Preview"},
    {"Обучение: ВКЛ.", "Tutorial: ON"},
    {"Обучение: ВЫКЛ.", "Tutorial: OFF"},
    {"Аккаунт", "Account"},
    {"Ник", "Nickname"},
    {"Пароль", "Password"},
    {"Ник игрока", "Player nickname"},
    {"ник", "nickname"},
    {"a-z, 0-9 и _", "a-z, 0-9 and _"},
    {"от 6 знаков", "6+ characters"},
    {"Войти", "Sign in"},
    {"Создать аккаунт", "Create account"},
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
    {"Автор", "Author"},
    {"Забанить автора", "Ban the author"},
    {"Уровень непроходимый", "Unplayable level"},
    {"Ник: только a-z, 0-9 и _, от 3 до 24 знаков.",
     "Nickname: a-z, 0-9 and _ only, 3 to 24 characters."},
    {"Пароль: от 6 до 72 знаков без пробелов.", "Password: 6 to 72 characters, no spaces."},
    {"Такой аккаунт уже есть.", "That account already exists."},
    {"Аккаунт занят или Firebase отказал. Войди либо проверь правила.",
     "The nickname is taken or Firebase refused registration. Sign in or check the rules."},
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
    {"Сообщение не найдено. Обнови список.", "Message not found. Refresh the list."},
    {"База не приняла аккаунт. Проверь правила Firebase.",
     "The database refused the account. Check the Firebase rules."},
    {"Не удалось обновить статистику уровня.", "Couldn't update the level statistics."},
    {"База не приняла сообщение. Возможно, ник забанен.",
     "The database refused the message. The nickname may be banned."},
    {"Скрыть сообщение может только его автор или модератор.",
     "Only the author or a moderator can hide a message."},
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

/* Formatted strings are assembled before drawing, so a finished label such as
 * "УРОВЕНЬ 5" is matched against table entries with printf conversions
 * ("УРОВЕНЬ %d"). Captured words (%s) are translated recursively so composite
 * labels switch language completely. */
#define PATTERN_MAX_CAPTURES 4
#define PATTERN_CAPTURE_CAP  160

static const char *exact_lookup(const char *text) {
    for (size_t i = 0; i < sizeof translations / sizeof translations[0]; ++i)
        if (!strcmp(text, translations[i].ru)) return translations[i].en;
    return NULL;
}

/* Points at '%'; returns just past the conversion character. */
static const char *pattern_scan_spec(const char *pattern, char *conversion) {
    ++pattern;
    if (*pattern == '%') { *conversion = '%'; return pattern + 1; }
    while (*pattern && !strchr("diuxXfsge", *pattern)) ++pattern;
    if (!*pattern) { *conversion = 0; return pattern; }
    *conversion = *pattern;
    return pattern + 1;
}

static int pattern_match(const char *pattern, const char *input,
                         char captures[][PATTERN_CAPTURE_CAP], int *count) {
    if (!*pattern) return !*input;
    if (*pattern == '%') {
        char conversion;
        const char *next = pattern_scan_spec(pattern, &conversion);
        if (!conversion) return 0;
        if (conversion == '%')
            return *input == '%' &&
                   pattern_match(next, input + 1, captures, count);
        size_t take = 0;
        if (conversion == 's') take = strlen(input);
        else {
            if (input[take] == '+' || input[take] == '-') ++take;
            size_t digits = take;
            while (input[take] >= '0' && input[take] <= '9') ++take;
            if (conversion == 'f' && input[take] == '.') {
                size_t fraction = take + 1;
                while (input[fraction] >= '0' && input[fraction] <= '9')
                    ++fraction;
                if (fraction > take + 1) take = fraction;
            }
            if (take == digits) return 0; /* at least one digit */
        }
        for (;;) {
            if (*count < PATTERN_MAX_CAPTURES && take < PATTERN_CAPTURE_CAP) {
                memcpy(captures[*count], input, take);
                captures[*count][take] = 0;
                int saved = *count;
                ++*count;
                if (pattern_match(next, input + take, captures, count))
                    return 1;
                *count = saved;
            }
            if (!take) return 0;
            --take;
            if (conversion != 's') return 0; /* numbers match in one piece */
        }
    }
    if (*pattern != *input) return 0;
    return pattern_match(pattern + 1, input + 1, captures, count);
}

static const char *pattern_render(const char *pattern,
                                  char captures[][PATTERN_CAPTURE_CAP],
                                  int count) {
    size_t used = 0;
    int capture = 0;
    while (*pattern && used + 1 < sizeof translated_text) {
        if (*pattern == '%') {
            char conversion;
            const char *next = pattern_scan_spec(pattern, &conversion);
            if (conversion == '%') translated_text[used++] = '%';
            else if (capture < count) {
                const char *value = captures[capture++];
                if (conversion == 's') {
                    const char *word = exact_lookup(value);
                    if (word) value = word;
                }
                size_t length = strlen(value);
                if (used + length >= sizeof translated_text)
                    length = sizeof translated_text - used - 1;
                memcpy(translated_text + used, value, length);
                used += length;
            }
            pattern = next;
            continue;
        }
        translated_text[used++] = *pattern++;
    }
    translated_text[used] = 0;
    return used ? translated_text : NULL;
}

const char *font_translate(const char *text) {
    if (!text || selected_language != FONT_LANG_EN) return text;
    const char *exact = exact_lookup(text);
    if (exact) return exact;
    /* Prefer the most specific (longest) pattern that consumes the string. */
    static char captures[PATTERN_MAX_CAPTURES][PATTERN_CAPTURE_CAP];
    const FontTranslation *best = NULL;
    size_t best_length = 0;
    for (size_t i = 0; i < sizeof translations / sizeof translations[0]; ++i) {
        if (!strchr(translations[i].ru, '%')) continue;
        size_t length = strlen(translations[i].ru);
        if (best && length <= best_length) continue;
        int count = 0;
        if (pattern_match(translations[i].ru, text, captures, &count)) {
            best = &translations[i];
            best_length = length;
        }
    }
    if (best) {
        int count = 0;
        if (pattern_match(best->ru, text, captures, &count)) {
            const char *rendered = pattern_render(best->en, captures, count);
            if (rendered) return rendered;
        }
    }
    static const struct {const char *ru, *en;} prefixes[] = {
        {"МОНЕТЫ ", "COINS "},
        {"Объектов: ", "Objects: "},
        {"Выбрано объектов: ", "Selected objects: "},
        {"Палитра ", "Palette "},
        {"Комната ", "Room "},
        {"Поиск: ", "Search: "},
        {"Сложность: ", "Difficulty: "},
        {"Тег: ", "Tag: "},
        {"Сортировка: ", "Sort: "},
        {"Найдено: ", "Results: "},
        {"Режим · ", "Touch: "},
        {"Прогресс ", "Progress "},
        {"Попытка ", "Attempt "},
        {"Время ", "Time "},
        {"Монеты ", "Coins "},
        {"Нравится · ", "Like · "},
        {"Убрать лайк · ", "Unlike · "}
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
