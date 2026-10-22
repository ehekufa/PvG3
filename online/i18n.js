/* PvG3 web interface language. Every visible string is authored in Russian
 * (the same canonical keys the native game keeps in src/font.c) and t()
 * returns the English line when the interface language is English. Data
 * labels stay Russian canonically; translation happens only for display.
 * {0}, {1} placeholders are filled from the extra arguments. */
const EN = {
  'PvG3 Онлайн — растения против уток': 'PvG3 Online — plants vs. ducks',
  'PvG3 Онлайн: играй за растения Кирилла или отправляй уток-зомби другу. Комнаты через Firebase.': 'PvG3 Online: play as Kirill\'s plants or send zombie ducks to a friend. Rooms via Firebase.',
  'онлайн-арена Кирилла': 'Kirill\'s online arena',
  'Подключение…': 'Connecting…',
  'Firebase на связи': 'Firebase connected',
  'Нет связи': 'No connection',
  'Музыка': 'Music',
  'Выключить музыку': 'Turn music off',
  'Включить музыку': 'Turn music on',
  'Книга': 'Book',
  'ИГРА ПО СЕТИ · 1 НА 1': 'ONLINE PLAY · 1 V 1',
  'Собери растения.': 'Gather plants.',
  'Или отправь уток.': 'Or send ducks.',
  'Создай комнату для друга или зайди в свободную. После подключения каждый сам выберет сторону: растения Кирилла или утки-зомби.': 'Create a room for a friend or join an open one. Once connected, each player picks a side: Kirill\'s plants or zombie ducks.',
  'Горохострел': 'Peashooter',
  'Нарисованная утка-зомби': 'Hand-drawn zombie duck',
  'ПРИГЛАСИ ДРУГА': 'INVITE A FRIEND',
  'Новая комната': 'New room',
  'Хозяин комнаты запускает бой на своём устройстве. Второй игрок увидит его поле и сможет отправлять команды.': 'The room host runs the match on their device. The second player sees the board and sends commands.',
  'Выбор карты': 'Map choice',
  'Газон': 'Lawn',
  'Водная карта': 'Water map',
  'Вода + кувшинки': 'Water + lily pads',
  'Создать комнату': 'Create room',
  'КОМНАТЫ': 'ROOMS',
  'Свободные комнаты': 'Open rooms',
  'Обновить': 'Refresh',
  'Обновить список': 'Refresh the list',
  'Ищем открытые комнаты…': 'Searching for open rooms…',
  'Пока нет свободных комнат. Создай свою кнопкой «+»!': 'No open rooms yet. Create your own with the "+" button!',
  'Друг может поделиться ссылкой с кодом комнаты. Закрытые и занятые комнаты в список не попадают.': 'A friend can share a link with the room code. Closed and full rooms never appear in the list.',
  'Мастерская платформера': 'Platformer workshop',
  'СОЗДАЙ СВОЮ ИГРУ': 'BUILD YOUR OWN GAME',
  'Собери уровень из блоков, монет и препятствий, проверь его и опубликуй в каталоге «Уровни игроков».': 'Build a level from blocks, coins and obstacles, test it and publish it to the Player Levels catalog.',
  'Открыть мастерскую': 'Open the workshop',
  'Без входа и чата · Только комнаты и игровые ходы · Кампания в APK остаётся отдельно': 'No login or chat · Just rooms and game turns · The APK campaign stays separate',
  'Назад · В онлайн': 'Back · Online',
  'ПЛАТФОРМЕННЫЙ РЕДАКТОР': 'PLATFORMER EDITOR',
  'Мастерская': 'Workshop',
  'ТВОЙ ПРОЕКТ': 'YOUR PROJECT',
  'Новый уровень': 'New level',
  'Продолжить редактор': 'Continue editing',
  'ИГРАЙ И ПУБЛИКУЙ': 'PLAY AND PUBLISH',
  'Уровни игроков': 'Player Levels',
  'Открыть каталог': 'Open the catalog',
  'Назад · Мастерская': 'Back · Workshop',
  'Проверить': 'Test',
  'Опубликовать': 'Publish',
  'Название уровня': 'Level title',
  'Краткое описание': 'Short description',
  'Сцена · окно карты 16 × 10': 'Scene · 16 × 10 map window',
  '0 / 20000 объектов': '0 / 20000 objects',
  'Окно бесконечной прокручиваемой карты платформера': 'Window into the infinite scrolling platformer map',
  'Инструменты редактора': 'Editor tools',
  'РЕЖИМ': 'MODE',
  'Строить': 'Build',
  'Выбрать': 'Select',
  'Мульти': 'Multi',
  'Удалить': 'Delete',
  'Карта': 'Map',
  'ВЫДЕЛЕНИЕ И ПРЕОБРАЗОВАНИЕ': 'SELECTION AND TRANSFORM',
  '0 выбрано': '0 selected',
  'Копировать': 'Copy',
  'Вставить': 'Paste',
  'Шаг перемещения': 'Move step',
  'Переместить выбранные объекты': 'Move the selected objects',
  'Сдвинуть объекты вверх на полблока': 'Nudge the objects up half a block',
  'Сдвинуть объекты влево на полблока': 'Nudge the objects left half a block',
  'Сдвинуть объекты вправо на полблока': 'Nudge the objects right half a block',
  'Сдвинуть объекты вниз на полблока': 'Nudge the objects down half a block',
  'Шаг размера': 'Size step',
  'Изменить ширину и высоту выбранных объектов': 'Change width and height of the selected objects',
  'Шаг поворота, °': 'Rotation step, °',
  'Повернуть выделение': 'Rotate the selection',
  'Повернуть против часовой стрелки': 'Rotate counterclockwise',
  'Повернуть по часовой стрелке': 'Rotate clockwise',
  'ЗЕРКАЛЬНОЕ ОТРАЖЕНИЕ': 'MIRROR FLIP',
  'Отразить выделение': 'Flip the selection',
  'Отразить слева направо': 'Flip left to right',
  'Отразить сверху вниз': 'Flip top to bottom',
  'Слева ↔ справа': 'Left ↔ right',
  'Сверху ↕ снизу': 'Top ↕ bottom',
  'ВЫБРАННОЕ': 'SELECTED',
  'Удалить выбранное': 'Delete selection',
  'КАТЕГОРИИ ОБЪЕКТОВ': 'OBJECT CATEGORIES',
  'Блоки': 'Blocks',
  'Шипы': 'Spikes',
  'Монета': 'Coin',
  'Утка': 'Duck',
  'Игрок': 'Player',
  'Цели': 'Goals',
  'Триггер': 'Trigger',
  'Орбы': 'Orbs',
  'Порталы': 'Portals',
  'Частицы': 'Particles',
  'Вид триггера': 'Trigger kind',
  'Движение': 'Movement',
  'Поворот': 'Rotation',
  'Разворот': 'Flip',
  'Вечно': 'Forever',
  'Невидимость': 'Invisibility',
  'Нет столкновения': 'No collision',
  'Гравитация': 'Gravity',
  'Перекраска': 'Recoloring',
  'Событие': 'Event',
  'Касание': 'Touch',
  'Подбор монеты': 'Coin pickup',
  'Кнопка действия': 'Action button',
  'Старт уровня': 'Level start',
  'Действие': 'Action',
  'Настроить эмиттер': 'Configure emitter',
  'Цвет объектов': 'Object color',
  'Цвет объекта': 'Object color',
  'Цвет фона': 'Background color',
  'Цвет': 'Color',
  'Фон': 'Background',
  'Размер': 'Size',
  'Ширина': 'Width',
  'Высота': 'Height',
  'Смещение X': 'Offset X',
  'Смещение Y': 'Offset Y',
  'Группа': 'Group',
  'Целевая группа': 'Target group',
  'Число': 'Number',
  'Клавиатура': 'Keyboard',
  'Управление': 'Controls',
  'Кнопки': 'Buttons',
  'Экранные кнопки': 'On-screen buttons',
  'ПЕРЕМЕЩЕНИЕ КАРТЫ': 'MAP PANNING',
  'Шаг карты': 'Map step',
  'Сдвинуть окно карты': 'Move the map window',
  'Сдвинуть карту вверх': 'Move the map up',
  'Сдвинуть карту вниз': 'Move the map down',
  'Сдвинуть карту влево': 'Move the map left',
  'Сдвинуть карту вправо': 'Move the map right',
  'Предпросмотр платформерного уровня': 'Platformer level preview',
  'Предпросмотр уровня': 'Level preview',
  'Редактор уровня': 'Level editor',
  'НАЗАД': 'BACK',
  'ВПЕРЁД': 'FORWARD',
  'ПРЫЖОК': 'JUMP',
  'ДЕЙСТВИЕ': 'ACTION',
  '↑ ВВЕРХ': '↑ UP',
  '↓ ВНИЗ': '↓ DOWN',
  'Вперёд': 'Forward',
  'Назад': 'Back',
  'Подъём': 'Up',
  'Спуск': 'Down',
  'С начала': 'From the start',
  'Время вращения, с': 'Rotation time, s',
  'Время жизни': 'Lifetime',
  'Скорость': 'Speed',
  'Направление': 'Direction',
  'Разброс': 'Spread',
  'Сила гравитации': 'Gravity strength',
  'Сила гравитации ·': 'Gravity strength ·',
  'Эмиссия включена': 'Emission enabled',
  'Непрерывно': 'Continuous',
  'Свечение': 'Glow',
  'ЖИВОЙ ПРЕДПРОСМОТР': 'LIVE PREVIEW',
  'Анимированный предпросмотр эмиттера P': 'Animated preview of emitter P',
  'Настройки эмиттера': 'Emitter settings',
  'ЭФФЕКТ ОБЪЕКТА P': 'OBJECT P EFFECT',
  'Закрыть': 'Close',
  'Закрыть настройки': 'Close settings',
  'По умолчанию': 'Defaults',
  'Готово': 'Done',
  'Авто': 'Auto',
  'Загружаем каталог…': 'Loading the catalog…',
  'Назад · В редактор': 'Back · To the editor',
  'Выйти из комнаты': 'Leave the room',
  'ВЫ В КОМНАТЕ': 'YOU ARE IN THE ROOM',
  'Выбор стороны': 'Choose a side',
  'КОД КОМНАТЫ': 'ROOM CODE',
  'Скопировать ссылку': 'Copy the link',
  'ЗАЩИТА': 'DEFENSE',
  'Растения': 'Plants',
  'НАСТУПЛЕНИЕ': 'OFFENSE',
  'Зомби-утки': 'Zombie ducks',
  'Онлайн-бой': 'Online match',
  'КОМНАТА': 'ROOM',
  'Выйти из боя ×': 'Leave the match ×',
  'Игровое поле': 'Game board',
  'Закончить волну': 'Finish the wave',
  'УМНАЯ КНИГА КИРИЛЛА': 'KIRILL\'S FIELD GUIDE',
  'Растения и противники': 'Plants and enemies',
  'Растения Кирилла': 'Kirill\'s plants',
  'Утки-зомби': 'Zombie ducks',
  'Обычная утка, утка с конусом и утка с ведром показаны на отдельных авторских рисунках.': 'The plain duck, the cone duck and the bucket duck each come as their own hand-drawn picture.',
  'Подсолнух': 'Sunflower',
  'Хлебушек': 'Bread',
  'Утка с конусом': 'Cone duck',
  'Утка с ведром': 'Bucket duck',
  'Утка-зомби': 'Zombie duck',
  'Главная мастерской': 'Workshop home',
  'Каталог уровней игроков': 'Player level catalog',
  '1.2 с': '1.2 s',
  '90 px/с': '90 px/s',
  '90 px/с²': '90 px/s²',
  'Платформа': 'Platform',
  'Склон': 'Slope',
  'Жёлтый орб': 'Yellow orb',
  'Оранжевый орб': 'Orange orb',
  'Обычный портал': 'Normal portal',
  'Портал Jetpack': 'Jetpack portal',
  'Чекпоинт': 'Checkpoint',
  'Эмиттер частиц': 'Particle emitter',
  'Активировать': 'Activate',
  'Деактивировать': 'Deactivate',
  'Подключаемся…': 'Connecting…',
  'Бой онлайн': 'Online match',
  'Бой без связи': 'Offline match',
  'В комнате': 'In the room',
  'Комната создана': 'Room created',
  'Создаём комнату…': 'Creating the room…',
  'Обновляем комнаты…': 'Refreshing rooms…',
  'Выходим…': 'Leaving…',
  'Войти': 'Join',
  'Играть': 'Play',
  'Код комнаты: {0}': 'Room code: {0}',
  'Неверный код комнаты.': 'Invalid room code.',
  'Комната закрыта': 'The room closed',
  'Комната закрылась или другой игрок занял твоё место.': 'The room closed or another player took your slot.',
  'Комната закрыта. Вернулись к списку комнат.': 'The room closed. Back to the room list.',
  'Не удалось закрыть комнату': 'Could not close the room',
  'Ошибка комнаты': 'Room error',
  'Ссылка на комнату скопирована.': 'Room link copied.',
  'Эту сторону уже выбрал соперник.': 'The opponent already picked this side.',
  'Подожди, предыдущий ход ещё передаётся.': 'Hold on, the previous turn is still being sent.',
  'Пока нельзя: проверь монеты, клетку и перезарядку.': 'Not yet: check coins, the cell and the cooldown.',
  'Защити Хлебушка': 'Protect Bread',
  'Прорви защиту Кирилла': 'Break through Kirill\'s defense',
  'Обычный газон': 'Plain lawn',
  'Водное поле · кувшинки': 'Water field · lily pads',
  'ОФИЦИАЛЬНЫЙ': 'OFFICIAL',
  'Официальный уровень': 'Official level',
  'Уровень ': 'Level ',
  'Уровень пройден!': 'Level complete!',
  'Уровень пройден! Собрано монет: {0}.': 'Level complete! Coins collected: {0}.',
  'Орб активирован!': 'Orb activated!',
  'Уровень опубликован · ID {0}': 'Level published · ID {0}',
  'Уровень опубликован под ID {0}. Он доступен в каталоге и нативной игре.': 'The level is published as ID {0}. It is available in the catalog and the native game.',
  'Некорректный ID публикации.': 'Invalid publication ID.',
  'Последняя публикация: ID {0}': 'Last publication: ID {0}',
  'Не удалось загрузить каталог: {0}': 'Could not load the catalog: {0}',
  'В каталоге пока нет опубликованных уровней.': 'No published levels in the catalog yet.',
  'Авторский платформерный уровень.': 'A hand-made platformer level.',
  'Назад · Каталог': 'Back · Catalog',
  'Не удалось прочитать черновик': 'Could not read the draft',
  'Не удалось сохранить черновик': 'Could not save the draft',
  'Сохранение черновика отменено': 'Draft saving cancelled',
  'Хранилище черновика занято другой вкладкой': 'The draft store is busy in another tab',
  'Не удалось открыть хранилище': 'Could not open the store',
  'IndexedDB недоступна': 'IndexedDB is unavailable',
  'Создать новый уровень вместо текущего черновика? Опубликованные уровни не затрагиваются.': 'Create a new level instead of the current draft? Published levels are untouched.',
  'Сначала скопируй выбранные объекты.': 'Copy the selected objects first.',
  'Вставлено объектов: {0}.': 'Pasted objects: {0}.',
  'Скопировано объектов: {0}.': 'Copied objects: {0}.',
  'Нет места для вставки: достигнут лимит объектов.': 'No room to paste: the object limit is reached.',
  'Достигнут общий лимит {0} объектов. Удали лишние объекты перед добавлением новых.': 'The total limit of {0} objects is reached. Remove some objects before adding new ones.',
  'Игрок и финиш обязательны и не удаляются.': 'The player and the finish are required and cannot be deleted.',
  'Выбрано: {0}': 'Selected: {0}',
  '{0} выбрано': '{0} selected',
  '{0} объектов': '{0} objects',
  '{0} / {1} объектов': '{0} / {1} objects',
  'Монеты: {0}': 'Coins: {0}',
  'монет': 'coins',
  'с': 's',
  'Уровень без игрока.': 'The level has no player.',
  'Уровень имеет неподдерживаемый формат.': 'The level has an unsupported format.',
  'Укажи название длиной от 1 до 80 символов.': 'Use a title between 1 and 80 characters.',
  'Описание должно быть не длиннее 160 символов.': 'The description must be at most 160 characters.',
  'На уровне обязательны игрок и финиш.': 'The level needs a player and a finish.',
  'Нужен уровень с окном просмотра 16×10 и 1–{0} объектами.': 'The level needs a 16×10 view window and 1–{0} objects.',
  'У объектов должны быть уникальные числовые ID.': 'Objects need unique numeric IDs.',
  'Настрой триггер: событие, группу, действие и параметры.': 'Set up the trigger: event, group, action and parameters.',
  'Проверь параметры эмиттера частиц.': 'Check the particle emitter settings.',
  'Проверь размеры, цвет и положение объекта «{0}».': 'Check the size, color and position of the object "{0}".',
  'Черновик не найден.': 'Draft not found.',
  'Браузер не разрешил воспроизвести музыку.': 'The browser did not allow music playback.',
  'Firebase запретил чтение /levels. Проверь правила базы.': 'Firebase denied reading /levels. Check the database rules.',
  'Firebase запретил чтение /levels-index. Для каталога в правилах базы нужно разрешить чтение этой ветки.': 'Firebase denied reading /levels-index. The catalog needs read access to that branch in the database rules.',
  'Firebase отклонил запись. Для публикации правила должны разрешать создание записей в /levels и /levels-index. Правила базы автоматически не менялись.': 'Firebase rejected the write. Publishing needs rules that allow creating records in /levels and /levels-index. The database rules were not changed automatically.',
  'Ведро защищает утку: 750 здоровья.': 'The bucket protects the duck: 750 health.',
  'Конус защищает утку: 420 здоровья.': 'The cone protects the duck: 420 health.',
  'Крепкая преграда для уток.': 'A sturdy barrier against ducks.',
  'Кувшинка': 'Lily pad',
  'Джампер-боец': 'Jumper fighter',
  'Исчезает при ударе, отбрасывая утку на три клетки.': 'Disappears on hit, knocking the duck back three tiles.',
  'Обычная нарисованная утка-противник.': 'A plain hand-drawn enemy duck.',
  'Орех': 'Walnut',
  'Подсолнух-наркоман': 'Stoner sunflower',
  'Производит монеты (+25 каждые 8 секунд).': 'Produces coins (+25 every 8 seconds).',
  'Стреляет по уткам в своём ряду.': 'Shoots ducks in its own lane.',
  'Только на водной карте: опора для посадки растений.': 'Water maps only: a base for planting.',
  'МОНЕТЫ РАСТЕНИЙ': 'PLANT COINS',
  'МОНЕТЫ ЗОМБИ': 'ZOMBIE COINS',
  'НАЖМИ НА РЯД ПОСЛЕ ВЫБОРА УТКИ': 'TAP A LANE AFTER PICKING A DUCK',
  'ОНЛАЙН • {0}': 'ONLINE • {0}',
  'ТЫ: РАСТЕНИЯ': 'YOU: PLANTS',
  'ТЫ: ЗОМБИ-УТКИ': 'YOU: ZOMBIE DUCKS',
  'УТОК ОСТАЛОСЬ: {0} / {1}': 'DUCKS LEFT: {0} / {1}',
  'КНИГА': 'BOOK',
  'ВЫЙТИ': 'EXIT',
  'ПОБЕДА!': 'VICTORY!',
  'ПОРАЖЕНИЕ': 'DEFEAT',
  'ВСЕ УТКИ ОСТАНОВЛЕНЫ': 'ALL DUCKS STOPPED',
  'УТКИ ПРОРВАЛИ ЗАЩИТУ': 'THE DUCKS BROKE THROUGH',
  '{0} с': '{0} s',
  'Сменить язык интерфейса': 'Switch interface language',
  'Firebase запретил запись в /levels-index. Проверь правила Firebase.':
    'Firebase denied writing to /levels-index. Check the database rules.',
  'Firebase запретил чтение или запись. Проверь правила Firebase.':
    'Firebase denied the read or write. Check the Firebase rules.',
  'Firebase не отвечает (таймаут). Проверь интернет.':
    'Firebase is not responding (timeout). Check the internet.',
  '{0} px/с': '{0} px/s',
  '{0} px/с²': '{0} px/s²',
  '{0} монет · {1} HP. {2}': '{0} coins · {1} HP. {2}',
  'Комната закрыта, уже началась или занята.':
    'The room is closed, already running or full.',
  'Комната уже занята. Обнови список и попробуй снова.':
    'The room is already taken. Refresh the list and try again.',
  'Не удалось подобрать свободный ID для уровня.':
    'Could not find a free level ID.',
  'Не удалось создать уникальный код комнаты.':
    'Could not create a unique room code.',
  'Неверная сторона.': 'Invalid side.',
  'Неверный ID уровня.': 'Invalid level ID.',
  'Нет соединения с Firebase. Проверь сеть или настройки доступа.':
    'No connection to Firebase. Check the network or access settings.',
  'Создатель закрыл комнату.': 'The host closed the room.',
  'Уровень {0} сохранён, но не появился в каталоге: {1} Повтори публикацию позже или проверь права базы.':
    'Level {0} is saved but did not appear in the catalog: {1} Try publishing later or check the database permissions.',
  'Уровень не найден в каталоге.': 'Level not found in the catalog.',
  'Уровень повреждён или его формат не поддерживается.':
    'The level is damaged or its format is not supported.',
  'ПРЕДПРОСМОТР': 'PREVIEW',
  'Уровень': 'Level',
  'Частиц в секунду': 'Particles per second',
  'Частиц во всплеске': 'Particles per burst',
};

let lang = 'ru';

export function getLang() { return lang; }

export function setLang(next) {
  lang = next === 'en' ? 'en' : 'ru';
  try { localStorage.setItem('pvg3-lang', lang); } catch { /* private mode */ }
  return lang;
}

export function initLang() {
  let saved = null;
  try { saved = localStorage.getItem('pvg3-lang'); } catch { /* private mode */ }
  if (saved === 'en' || saved === 'ru') lang = saved;
  else if (typeof navigator !== 'undefined' &&
           /^en\b/i.test(navigator.language || '')) lang = 'en';
  return lang;
}

export function t(text, ...values) {
  if (typeof text !== 'string') return text;
  let out = lang === 'en' && Object.hasOwn(EN, text) ? EN[text] : text;
  for (let i = 0; i < values.length; i++)
    out = out.replaceAll(`{${i}}`, String(values[i]));
  return out;
}

/* The few labels whose markup is pinned by tests keep their exact HTML and
 * are translated through selectors instead of data-i18n attributes. */
const PINNED = [
  ['#lobby-title', 'Выбор стороны'],
  ['button[data-side="plants"] h2', 'Растения'],
  ['button[data-side="zombies"] h2', 'Зомби-утки'],
];

const originals = new WeakMap();

function remember(node, key, value) {
  let entry = originals.get(node);
  if (!entry) { entry = {}; originals.set(node, entry); }
  if (!(key in entry)) entry[key] = value;
  return entry[key];
}

function originalFor(node, key, source) {
  const entry = originals.get(node);
  if (entry && key in entry) return entry[key];
  if (source && Object.hasOwn(EN, source)) return remember(node, key, source);
  return null;
}

function translateTextNode(node) {
  const value = node.nodeValue ?? '';
  const source = value.trim();
  if (!source) return;
  const original = originalFor(node, 'text', source);
  if (!original) return;
  node.nodeValue = value.replace(source, t(original));
}

function translateAttributes(element) {
  for (const name of ['aria-label', 'title', 'alt', 'placeholder', 'content']) {
    const value = element.getAttribute(name);
    if (value === null) continue;
    const source = value.trim();
    if (!source) continue;
    const original = originalFor(element, name, source);
    if (!original) continue;
    element.setAttribute(name, t(original));
  }
}

/* Walk the static document text once per call; user-generated content that
 * appears later is never touched because its nodes are new and, when the app
 * builds them, they already pass through t(). */
export function applyTranslations(root = document) {
  const walker = root.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  const texts = [];
  for (let node = walker.nextNode(); node; node = walker.nextNode())
    if (node.parentElement?.closest('[data-no-i18n]') === null)
      texts.push(node);
  for (const node of texts) translateTextNode(node);
  const elements = root.querySelectorAll('*');
  for (const element of elements) {
    if (element.closest('[data-no-i18n]')) continue;
    translateAttributes(element);
  }
  for (const [selector, source] of PINNED) {
    const element = root.querySelector(selector);
    if (!element) continue;
    const original = remember(element, 'pinned', source);
    element.textContent = t(original);
  }
  const toggle = root.querySelector('#lang-toggle');
  if (toggle) toggle.textContent = lang === 'en' ? 'РУ' : 'EN';
  if (root.documentElement) root.documentElement.lang = lang;
}
