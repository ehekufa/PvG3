/* Local, account-free project format for the PvG3 mini game maker. */
export const PROJECT_FORMAT = 'PVG3-MAKER';
export const PROJECT_VERSION = 1;
export const GRID_WIDTH = 16;
export const GRID_HEIGHT = 10;
export const MAX_OBJECT_ID = 1000000;
export const MAX_OBJECTS = 500;

export const TEMPLATES = [
  {
    id: 'classic',
    title: 'Классический платформер',
    description: 'Прыжки по платформам, монеты и финиш.',
    projectName: 'Первый платформер',
  },
  {
    id: 'puzzle',
    title: 'Платформер-головоломка',
    description: 'Кнопка-триггер двигает мост и открывает путь.',
    projectName: 'Платформер-головоломка',
  },
  {
    id: 'adventure',
    title: 'Приключенческий платформер',
    description: 'Противник, опасности и несколько высот.',
    projectName: 'Приключение на газоне',
  },
];

export const OBJECT_TYPES = {
  block:   {name: 'Блок',       color: '#ad7449', w: 1,   h: 1,    group: 'solid'},
  ground:  {name: 'Платформа',  color: '#64844c', w: 2,   h: 0.4,  group: 'solid'},
  hazard:  {name: 'Шипы',       color: '#d65d4d', w: 1,   h: 0.8,  group: 'hazard'},
  coin:    {name: 'Монета',     color: '#f2c24c', w: 0.55,h: 0.55, group: 'pickup'},
  enemy:   {name: 'Противник',  color: '#ba6d78', w: 0.8, h: 0.8,  group: 'actor'},
  player:  {name: 'Старт',      color: '#7db9dd', w: 0.65,h: 0.85, group: 'actor'},
  goal:    {name: 'Финиш',      color: '#e8cf77', w: 0.8, h: 1.2,  group: 'actor'},
  trigger: {name: 'Триггер',    color: '#59c7c4', w: 1,   h: 1,    group: 'logic'},
};

const TRIGGER_EVENTS = new Set(['touch', 'coin', 'manual']);
const TRIGGER_ACTIONS = new Set(['toggle', 'move', 'recolor', 'number']);
const HEX_COLOR = /^#[0-9a-f]{6}$/i;
const clamp = (n, min, max) => Math.max(min, Math.min(max, n));
const copy = value => JSON.parse(JSON.stringify(value));

function object(id, type, x, y, extra = {}) {
  const def = OBJECT_TYPES[type];
  if (!def) throw new TypeError(`Неизвестный объект: ${type}`);
  return {
    id, type, name: def.name, x, y, w: def.w, h: def.h,
    angle: 0, color: def.color, number: 0, visible: true,
    ...(type === 'trigger' ? {
      trigger: {event: 'touch', action: 'toggle', targetId: 0, value: 1, color: '#f4bd4e'},
    } : {}),
    ...extra,
  };
}

function templateObjects(templateId) {
  if (templateId === 'classic') {
    return [
      object(1, 'ground', 0, 9, {w: 16, h: 1, name: 'Земля'}),
      object(2, 'player', 1, 8.12, {name: 'Старт игрока'}),
      object(3, 'goal', 14.7, 7.8, {name: 'Финиш'}),
      object(4, 'ground', 3.4, 7.15, {w: 2.5, name: 'Платформа 1'}),
      object(5, 'ground', 7.6, 6.05, {w: 2.3, name: 'Платформа 2'}),
      object(6, 'ground', 11.2, 7.15, {w: 1.6, name: 'Платформа 3'}),
      object(7, 'coin', 4.2, 6.45),
      object(8, 'coin', 8.3, 5.35),
      object(9, 'hazard', 6.3, 8.2),
    ];
  }
  if (templateId === 'puzzle') {
    return [
      object(1, 'ground', 0, 9, {w: 16, h: 1, name: 'Земля'}),
      object(2, 'player', 1, 8.12, {name: 'Старт игрока'}),
      object(3, 'goal', 14.7, 7.8, {name: 'Финиш'}),
      object(4, 'ground', 3, 7.1, {w: 2.2, name: 'Верхняя площадка'}),
      object(5, 'ground', 8, 7.1, {w: 3, name: 'Скрытый мост', visible: false}),
      object(6, 'trigger', 5.5, 8.05, {
        name: 'Кнопка моста',
        trigger: {event: 'touch', action: 'toggle', targetId: 5, value: 1, color: '#f4bd4e'},
      }),
      object(7, 'coin', 4.1, 6.4),
      object(8, 'hazard', 7, 8.2),
    ];
  }
  if (templateId === 'adventure') {
    return [
      object(1, 'ground', 0, 9, {w: 16, h: 1, name: 'Земля'}),
      object(2, 'player', 1, 8.12, {name: 'Старт игрока'}),
      object(3, 'goal', 14.7, 7.8, {name: 'Финиш'}),
      object(4, 'ground', 2.8, 7.2, {w: 2.2, name: 'Площадка у входа'}),
      object(5, 'ground', 6.4, 6.2, {w: 2, name: 'Высокая площадка'}),
      object(6, 'ground', 10, 7.15, {w: 2.5, name: 'Площадка у финиша'}),
      object(7, 'enemy', 8.4, 8.2, {name: 'Патрульный'}),
      object(8, 'coin', 3.5, 6.5),
      object(9, 'coin', 7.1, 5.5),
      object(10, 'coin', 11, 6.45),
      object(11, 'hazard', 5, 8.2),
    ];
  }
  throw new TypeError('Неизвестный шаблон проекта.');
}

export function createProject(templateId = 'classic') {
  const template = TEMPLATES.find(item => item.id === templateId);
  if (!template) throw new TypeError('Неизвестный шаблон проекта.');
  return {
    format: PROJECT_FORMAT,
    version: PROJECT_VERSION,
    title: template.projectName,
    templateId,
    width: GRID_WIDTH,
    height: GRID_HEIGHT,
    objects: templateObjects(templateId),
  };
}

export function makeObject(type, id, x = 0, y = 0) {
  if (!Number.isSafeInteger(id) || id < 1 || id > MAX_OBJECT_ID)
    throw new TypeError(`ID должен быть целым числом от 1 до ${MAX_OBJECT_ID}.`);
  if (!Number.isFinite(x) || !Number.isFinite(y)) throw new TypeError('Координаты должны быть числами.');
  return object(id, type, x, y);
}

export function nextObjectId(project) {
  const used = new Set(project.objects.map(item => item.id));
  for (let id = 1; id <= MAX_OBJECT_ID; id++) if (!used.has(id)) return id;
  throw new RangeError('Свободных ID не осталось.');
}

export function normalizeProject(input) {
  if (!input || typeof input !== 'object' || Array.isArray(input))
    throw new TypeError('Файл проекта должен содержать объект JSON.');
  if (input.format !== PROJECT_FORMAT || input.version !== PROJECT_VERSION)
    throw new TypeError('Это не файл проекта PvG3 Maker или его версия не поддерживается.');
  const template = TEMPLATES.find(item => item.id === input.templateId);
  if (!template) throw new TypeError('Неизвестный шаблон уровня.');
  if (!Array.isArray(input.objects) || input.objects.length > MAX_OBJECTS)
    throw new TypeError(`В проекте должно быть не больше ${MAX_OBJECTS} объектов.`);
  const width = Number(input.width), height = Number(input.height);
  if (width !== GRID_WIDTH || height !== GRID_HEIGHT)
    throw new TypeError(`Размер уровня должен быть ${GRID_WIDTH} × ${GRID_HEIGHT}.`);

  const seen = new Set();
  const objects = input.objects.map((raw, index) => {
    if (!raw || typeof raw !== 'object' || Array.isArray(raw))
      throw new TypeError(`Объект ${index + 1} повреждён.`);
    const type = raw.type;
    const def = OBJECT_TYPES[type];
    if (!def) throw new TypeError(`У объекта ${index + 1} неизвестный тип.`);
    const id = Number(raw.id);
    if (!Number.isSafeInteger(id) || id < 1 || id > MAX_OBJECT_ID || seen.has(id))
      throw new TypeError(`У каждого объекта должен быть свой целый ID от 1 до ${MAX_OBJECT_ID}.`);
    seen.add(id);
    const x = Number(raw.x), y = Number(raw.y);
    const w = Number(raw.w), h = Number(raw.h);
    const angle = Number(raw.angle ?? 0);
    if (![x, y, w, h, angle].every(Number.isFinite))
      throw new TypeError(`Координаты или размер объекта ${id} некорректны.`);
    const color = typeof raw.color === 'string' && HEX_COLOR.test(raw.color) ? raw.color : def.color;
    const safeW = clamp(w, 0.25, GRID_WIDTH);
    const safeH = clamp(h, 0.25, GRID_HEIGHT);
    const item = {
      id, type,
      name: (typeof raw.name === 'string' && raw.name.trim() ? raw.name.trim() : def.name).slice(0, 48),
      x: clamp(x, 0, GRID_WIDTH - safeW),
      y: clamp(y, 0, GRID_HEIGHT - safeH),
      w: safeW,
      h: safeH,
      angle: ((angle % 360) + 360) % 360,
      color: color.toLowerCase(),
      number: Number.isFinite(Number(raw.number)) ? Math.trunc(clamp(Number(raw.number), 0, 9999)) : 0,
      visible: raw.visible !== false,
    };
    if (type === 'trigger') {
      const config = raw.trigger && typeof raw.trigger === 'object' ? raw.trigger : {};
      const event = TRIGGER_EVENTS.has(config.event) ? config.event : 'touch';
      const action = TRIGGER_ACTIONS.has(config.action) ? config.action : 'toggle';
      const targetId = Number(config.targetId ?? 0);
      const value = Number(config.value ?? 1);
      item.trigger = {
        event,
        action,
        targetId: Number.isSafeInteger(targetId) && targetId >= 0 && targetId <= 1000000 ? targetId : 0,
        value: Number.isFinite(value) ? clamp(value, -100, 100) : 1,
        color: typeof config.color === 'string' && HEX_COLOR.test(config.color) ? config.color.toLowerCase() : '#f4bd4e',
      };
    }
    return item;
  });
  const title = typeof input.title === 'string' && input.title.trim() ? input.title.trim().slice(0, 80) : template.projectName;
  return {
    format: PROJECT_FORMAT,
    version: PROJECT_VERSION,
    title,
    templateId: template.id,
    width: GRID_WIDTH,
    height: GRID_HEIGHT,
    objects,
  };
}

export function serializeProject(project) {
  return `${JSON.stringify(normalizeProject(project), null, 2)}\n`;
}

export function parseProject(text) {
  if (typeof text !== 'string' || text.length > 1_000_000)
    throw new TypeError('Файл пустой или слишком большой (максимум 1 МБ).');
  let parsed;
  try { parsed = JSON.parse(text); }
  catch { throw new TypeError('Не удалось прочитать TXT: ожидается JSON-файл проекта PvG3.'); }
  return normalizeProject(parsed);
}

export function duplicateProject(project) {
  return copy(normalizeProject(project));
}
