/* Shared web workshop model, PVG3-MAKER codec and small platformer preview.
 * This format is also read by src/online_protocol.c in the native C client. */
// The canvas shows a 16×10 viewport onto a sparse, effectively infinite world.
// Coordinates are bounded only to keep JSON, float math and camera precision safe.
export const LEVEL_WIDTH = 16;
export const LEVEL_HEIGHT = 10;
export const WORLD_LIMIT = 100_000;
export const TILE_W = 80;
export const TILE_H = 72;
// Shared ceiling for all object types combined; matches ON_LEVEL_OBJECT_CAP.
export const MAX_LEVEL_OBJECTS = 20_000;
export const MIN_OBJECT_SIZE = .1;
export const MAX_OBJECT_WIDTH = LEVEL_WIDTH * 4;
export const MAX_OBJECT_HEIGHT = LEVEL_HEIGHT * 4;
export const TRIGGER_KINDS = Object.freeze([
  'move', 'rotate', 'forever', 'invisibility', 'no-collision', 'gravity',
]);
export const TRIGGER_LABELS = Object.freeze({
  move: 'Движение', rotate: 'Разворот', forever: 'Вечно',
  invisibility: 'Невидимость', 'no-collision': 'Нет столкновения',
  gravity: 'Гравитация',
});
const TRIGGER_ACTIONS = Object.freeze({
  move: 'move', rotate: 'rotate', forever: 'activate',
  invisibility: 'invisible', 'no-collision': 'no-collision', gravity: 'set-gravity',
});
export const LEVEL_TYPES = Object.freeze([
  'block', 'ground', 'hazard', 'coin', 'enemy', 'player', 'goal', 'trigger', 'slope',
  'orb-yellow', 'orb-orange', 'particle',
]);
export const TYPE_LABELS = Object.freeze({
  block: 'Блок', ground: 'Платформа', hazard: 'Шипы', coin: 'Монета',
  enemy: 'Гусь', player: 'Игрок', goal: 'Финиш', trigger: 'Триггер', slope: 'Склон',
  'orb-yellow': 'Жёлтый орб', 'orb-orange': 'Оранжевый орб',
  particle: 'Эмиттер частиц',
});
const TYPE_SIZES = Object.freeze({
  block: [1, 1], ground: [2, 1], hazard: [1, 1], coin: [.55, .55],
  enemy: [.8, .8], player: [.65, .85], goal: [1, 2], trigger: [1, 1], slope: [1, 1],
  'orb-yellow': [.7, .7], 'orb-orange': [.7, .7], particle: [1, 1],
});
const DEFAULT_COLORS = Object.freeze({
  block: '#55c8ea', ground: '#65a845', hazard: '#e56c5b', coin: '#ffc54e',
  enemy: '#9560bd', player: '#5ab7e8', goal: '#69d16c', trigger: '#f27652',
  slope: '#e56c5b', 'orb-yellow': '#fff400', 'orb-orange': '#ff8a16',
  particle: '#68f0d8',
});
const TYPE_TO_ID = Object.freeze({
  block: 0, ground: 1, hazard: 2, coin: 3, enemy: 4,
  player: 5, goal: 6, trigger: 7, slope: 8, 'orb-yellow': 9, 'orb-orange': 10,
  particle: 11,
});
const TYPE_NAMES = Object.freeze([
  'block', 'ground', 'hazard', 'coin', 'enemy', 'player', 'goal', 'trigger', 'slope',
  'orb-yellow', 'orb-orange', 'particle',
]);
// Normalized opaque artwork bounds, measured from alpha in the supplied PNGs.
const ART_ALPHA_BOUNDS = Object.freeze({
  player: Object.freeze([.21, .02, .80, .96]),
  enemy: Object.freeze([.20, .21, .99, .99]),
  coin: Object.freeze([.05, .03, .96, .97]),
  goal: Object.freeze([.04, .03, 1, 1]),
});
const FULL_ART_BOUNDS = Object.freeze([0, 0, 1, 1]);
// Silhouettes of Склон.png and Шип.png; transparent canvas corners are not solid.
const SLOPE_VERTICES = Object.freeze([[0, 1], [1, 0], [1, 1]]);
const SPIKE_VERTICES = Object.freeze([[.5, .03], [1, 1], [0, 1]]);
const EVENTS = new Set(['touch', 'coin', 'manual', 'start']);
const ACTIONS = new Set([
  'toggle', 'move', 'recolor', 'number', 'rotate', 'invisible', 'no-collision',
  'set-gravity',
]);
const FOREVER_ACTIONS = new Set(['activate', 'unactivate']);
const GROUP_ID_MAX = 9999;
const ROTATION_DURATION_MAX = 9999;
const ROTATION_DEGREES_PER_SECOND = 360;
export const DEFAULT_PARTICLE_EMITTER = Object.freeze({
  enabled: true, continuous: true, gravityEnabled: false, glow: true,
  rate: 8, lifetime: 1.2, speed: 90, spread: 40, size: 4,
  direction: -90, gravity: 90,
});
export const PARTICLE_EMITTER_LIMITS = Object.freeze({
  rate: Object.freeze([1, 30]), lifetime: Object.freeze([.2, 3]),
  speed: Object.freeze([0, 300]), spread: Object.freeze([0, 180]),
  size: Object.freeze([1, 12]), direction: Object.freeze([-180, 180]),
  gravity: Object.freeze([0, 300]),
});
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const finite = value => typeof value === 'number' && Number.isFinite(value);
const worldClamp = (value, size = 0) => clamp(value, -WORLD_LIMIT, WORLD_LIMIT - size);
const copy = value => JSON.parse(JSON.stringify(value));

export function normalizeParticleEmitter(value) {
  const source = value && typeof value === 'object' && !Array.isArray(value) ? value : {};
  const normalized = {...DEFAULT_PARTICLE_EMITTER};
  for (const key of ['enabled', 'continuous', 'gravityEnabled', 'glow'])
    if (typeof source[key] === 'boolean') normalized[key] = source[key];
  for (const key of ['rate', 'speed', 'spread', 'size', 'direction', 'gravity']) {
    if (!Number.isFinite(source[key])) continue;
    const [min, max] = PARTICLE_EMITTER_LIMITS[key];
    normalized[key] = clamp(Math.trunc(source[key]), min, max);
  }
  if (Number.isFinite(source.lifetime))
    normalized.lifetime = clamp(Math.round(source.lifetime * 10) / 10, .2, 3);
  return normalized;
}
function validParticleEmitter(value) {
  if (value === undefined) return true; // older published levels get defaults
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  for (const key of ['enabled', 'continuous', 'gravityEnabled', 'glow'])
    if (value[key] !== undefined && typeof value[key] !== 'boolean') return false;
  for (const key of Object.keys(PARTICLE_EMITTER_LIMITS)) {
    if (key === 'lifetime' || value[key] === undefined) continue;
    const [min, max] = PARTICLE_EMITTER_LIMITS[key];
    if (!Number.isInteger(value[key]) || value[key] < min || value[key] > max) return false;
  }
  if (value.lifetime !== undefined && (!finite(value.lifetime) ||
      value.lifetime < .2 || value.lifetime > 3 ||
      Math.abs(value.lifetime * 10 - Math.round(value.lifetime * 10)) > 1e-6)) return false;
  return true;
}

function localId() {
  return `draft-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 9)}`;
}
function rgb(value) {
  return typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value);
}
function defaultObject(id, type, x, y, width, height, color, number = 0) {
  return {id, type, name: TYPE_LABELS[type], x, y, w: width, h: height,
    angle: 0, flipX: false, flipY: false, color, number, visible: true,
    layer: 0, layer2: 0, zOrder: 0};
}

export function setTriggerKind(level, id, kind) {
  if (!TRIGGER_KINDS.includes(kind)) return false;
  const object = level?.objects?.find(item => item.id === id && item.type === 'trigger');
  if (!object) return false;
  const trigger = object.trigger ||= {event: 'touch'};
  const previousKind = trigger.kind || 'move';
  trigger.kind = kind;
  trigger.action = TRIGGER_ACTIONS[kind];
  if (kind === 'move' && previousKind !== 'move') {
    trigger.valueX = 1;trigger.valueY = 0;
  }
  if (kind !== 'gravity' && previousKind === 'gravity') {
    const target = level.objects.find(item => item.type === 'goal') ||
      level.objects.find(item => item.id !== object.id);
    trigger.targetId = target?.id || 0;
    trigger.groupId = Number.isInteger(target?.number) ? target.number : 0;
  }
  if (kind === 'rotate') {
    if (!Number.isInteger(trigger.duration) || trigger.duration < 1 ||
        trigger.duration > ROTATION_DURATION_MAX) trigger.duration = 3;
    delete trigger.degrees;delete trigger.value;
  } else if (kind === 'gravity') {
    const value = previousKind === 'gravity' && Number.isFinite(trigger.value) ?
      Math.trunc(trigger.value) : 0;
    trigger.value = clamp(value, -100, 100);
    delete trigger.valueX;delete trigger.valueY;
    delete trigger.degrees;delete trigger.duration;
  }
  return true;
}

export function newDraft(id = localId()) {
  return {
    localId: id, title: 'Новый уровень', description: '', width: LEVEL_WIDTH,
    height: LEVEL_HEIGHT, objects: [
      defaultObject(1, 'player', 1, 7, .65, .85, DEFAULT_COLORS.player, 1),
      defaultObject(2, 'goal', 14, 6, 1, 2, DEFAULT_COLORS.goal, 2),
      defaultObject(3, 'ground', 0, 8, 16, 2, DEFAULT_COLORS.ground, 3),
    ],
  };
}

export function addObject(level, type, x, y, triggerKind = 'move') {
  if (!LEVEL_TYPES.includes(type) || level.objects.length >= MAX_LEVEL_OBJECTS) return null;
  const size = TYPE_SIZES[type];
  const object = defaultObject(
    Math.max(0, ...level.objects.map(o => Number(o.id) || 0)) + 1,
    type, worldClamp(x, size[0]), worldClamp(y, size[1]),
    size[0], size[1], DEFAULT_COLORS[type],
    Math.max(0, ...level.objects.map(o => Number(o.number) || 0)) + 1,
  );
  if (type === 'particle') object.emitter = {...DEFAULT_PARTICLE_EMITTER};
  if (type === 'trigger') {
    const kind = TRIGGER_KINDS.includes(triggerKind) ? triggerKind : 'move';
    const target = level.objects.find(o => o.type === 'goal') || level.objects[0];
    const targetGroup = Number.isInteger(target?.number) ? target.number : 0;
    const action = TRIGGER_ACTIONS[kind];
    object.trigger = {kind, event: 'touch', action,
      ...(kind === 'gravity' ? {} : {targetId: target?.id || 0, groupId: targetGroup}),
      valueX: kind === 'move' ? 1 : 0, valueY: 0,
      duration: kind === 'rotate' ? 3 : 0,
      value: kind === 'move' ? 1 : 0,
      color: '#ffc54e'};
  }
  if (type === 'player' || type === 'goal') {
    const old = level.objects.find(o => o.type === type);
    if (old) {
      old.x = object.x;old.y = object.y;
      return old;
    }
  }
  level.objects.push(object);
  return object;
}

function pointInTriangle(x, y, vertices) {
  const cross = (a, b) => (x - b[0]) * (a[1] - b[1]) -
    (a[0] - b[0]) * (y - b[1]);
  const d1 = cross(vertices[0], vertices[1]);
  const d2 = cross(vertices[1], vertices[2]);
  const d3 = cross(vertices[2], vertices[0]);
  const hasNegative = d1 < -1e-9 || d2 < -1e-9 || d3 < -1e-9;
  const hasPositive = d1 > 1e-9 || d2 > 1e-9 || d3 > 1e-9;
  return !(hasNegative && hasPositive);
}
function objectFlipX(object) {
  return (object.flipX === true) !== (object.type === 'enemy');
}
function artBounds(type, flipX = false, flipY = false) {
  const [baseLeft, baseTop, baseRight, baseBottom] =
    ART_ALPHA_BOUNDS[type] || FULL_ART_BOUNDS;
  const left = flipX ? 1 - baseRight : baseLeft;
  const right = flipX ? 1 - baseLeft : baseRight;
  const top = flipY ? 1 - baseBottom : baseTop;
  const bottom = flipY ? 1 - baseTop : baseBottom;
  return [left, top, right, bottom];
}
function playerVisibleHitbox(state, player) {
  const fullW = player.w * TILE_W, fullH = player.h * TILE_H;
  const [left, top, right, bottom] = artBounds('player', player.flipX, player.flipY);
  return {x: state.x + fullW * left, y: state.y + fullH * top,
    w: fullW * (right - left), h: fullH * (bottom - top)};
}

export function findObjectAt(level, x, y) {
  return level.objects
    .filter(o => {
      if (o.visible === false) return false;
      const angle = (o.angle || 0) * Math.PI / 180;
      const dx = x - (o.x + o.w / 2), dy = y - (o.y + o.h / 2);
      const localX = Math.cos(angle) * dx + Math.sin(angle) * dy;
      const localY = -Math.sin(angle) * dx + Math.cos(angle) * dy;
      if (Math.abs(localX) > o.w / 2 || Math.abs(localY) > o.h / 2) return false;
      const rawU = localX / o.w + .5, rawV = localY / o.h + .5;
      const u = objectFlipX(o) ? 1 - rawU : rawU;
      const v = o.flipY ? 1 - rawV : rawV;
      if (o.type === 'slope') return pointInTriangle(u, v, SLOPE_VERTICES);
      if (o.type === 'hazard') return pointInTriangle(u, v, SPIKE_VERTICES);
      const [left, top, right, bottom] = artBounds(o.type);
      return u >= left && u <= right && v >= top && v <= bottom;
    })
    .sort((a, b) => (b.layer || 0) - (a.layer || 0) ||
                    (b.zOrder || 0) - (a.zOrder || 0))
    .at(0) || null;
}

function selectedObjects(level, ids) {
  const wanted = new Set(ids || []);
  return level?.objects?.filter(object => wanted.has(object.id)) || [];
}

export function moveObjects(level, ids, dx, dy) {
  if (!finite(dx) || !finite(dy)) return 0;
  const objects = selectedObjects(level, ids);
  for (const object of objects) {
    object.x = worldClamp(object.x + dx, object.w);
    object.y = worldClamp(object.y + dy, object.h);
  }
  return objects.length;
}

export function resizeObjects(level, ids, axis, amount) {
  if (!finite(amount) || !['width', 'height'].includes(axis)) return 0;
  const objects = selectedObjects(level, ids);
  for (const object of objects) {
    const property = axis === 'width' ? 'w' : 'h';
    const maximum = axis === 'width' ? MAX_OBJECT_WIDTH : MAX_OBJECT_HEIGHT;
    object[property] = clamp(object[property] + amount, MIN_OBJECT_SIZE, maximum);
    const position = axis === 'width' ? 'x' : 'y';
    object[position] = worldClamp(object[position], object[property]);
  }
  return objects.length;
}

export function rotateObjects(level, ids, degrees) {
  if (!finite(degrees)) return 0;
  const objects = selectedObjects(level, ids);
  for (const object of objects)
    object.angle = ((Number(object.angle) + degrees) % 360 + 360) % 360;
  return objects.length;
}

export function flipObjects(level, ids, axis) {
  if (!['x', 'y'].includes(axis)) return 0;
  const property = axis === 'x' ? 'flipX' : 'flipY';
  const objects = selectedObjects(level, ids);
  for (const object of objects) object[property] = object[property] !== true;
  return objects.length;
}

export function panCamera(camera, dx, dy) {
  const x = finite(camera?.x) ? camera.x : 0;
  const y = finite(camera?.y) ? camera.y : 0;
  if (!finite(dx)) dx = 0;
  if (!finite(dy)) dy = 0;
  return {
    x: clamp(x + dx, -WORLD_LIMIT, WORLD_LIMIT - LEVEL_WIDTH),
    y: clamp(y + dy, -WORLD_LIMIT, WORLD_LIMIT - LEVEL_HEIGHT),
  };
}

export function copyObjects(level, ids) {
  return selectedObjects(level, ids)
    .filter(object => object.type !== 'player' && object.type !== 'goal')
    .map(copy);
}

export function pasteObjects(level, clipboard, dx = 1, dy = 1) {
  if (!Array.isArray(clipboard) || !clipboard.length ||
      !finite(dx) || !finite(dy) || !level?.objects ||
      level.objects.length + clipboard.length > MAX_LEVEL_OBJECTS) return [];
  const used = new Set(level.objects.map(object => object.id));
  const copies = [];
  let nextId = Math.max(0, ...used) + 1;
  for (const source of clipboard) {
    while (used.has(nextId) && nextId <= 1_000_000) nextId++;
    if (nextId > 1_000_000) return [];
    const object = copy(source);
    object.id = nextId;used.add(nextId++);
    object.x = worldClamp(object.x + dx, object.w);
    object.y = worldClamp(object.y + dy, object.h);
    copies.push(object);
  }
  level.objects.push(...copies);
  return copies;
}

export function validateDraft(level) {
  const fail = message => ({ok: false, message});
  if (!level || typeof level !== 'object') return fail('Черновик не найден.');
  if (typeof level.title !== 'string' || !level.title.trim() || level.title.length > 80)
    return fail('Укажи название длиной от 1 до 80 символов.');
  if (typeof level.description !== 'string' || level.description.length > 160)
    return fail('Описание должно быть не длиннее 160 символов.');
  if (level.width !== LEVEL_WIDTH || level.height !== LEVEL_HEIGHT ||
      !Array.isArray(level.objects) || level.objects.length < 1 ||
      level.objects.length > MAX_LEVEL_OBJECTS)
    return fail(`Нужен уровень с окном просмотра 16×10 и 1–${MAX_LEVEL_OBJECTS} объектами.`);
  const ids = new Set();
  let hasPlayer = false, hasGoal = false;
  for (const object of level.objects) {
    if (!object || !LEVEL_TYPES.includes(object.type) ||
        !Number.isInteger(object.id) || object.id < 1 || object.id > 1_000_000 || ids.has(object.id))
      return fail('У объектов должны быть уникальные числовые ID.');
    ids.add(object.id);
    if (typeof object.name !== 'string' || object.name.length > 48 ||
        ![object.x, object.y, object.w, object.h, object.angle].every(finite) ||
        object.x < -WORLD_LIMIT || object.y < -WORLD_LIMIT ||
        object.w <= 0 || object.h <= 0 ||
        object.x + object.w > WORLD_LIMIT ||
        object.y + object.h > WORLD_LIMIT ||
        object.w > MAX_OBJECT_WIDTH || object.h > MAX_OBJECT_HEIGHT ||
        object.angle < 0 || object.angle >= 360 ||
        (object.flipX !== undefined && typeof object.flipX !== 'boolean') ||
        (object.flipY !== undefined && typeof object.flipY !== 'boolean') ||
        !rgb(object.color) ||
        !Number.isInteger(object.number) || object.number < 0 || object.number > 9999 ||
        typeof object.visible !== 'boolean')
      return fail(`Проверь размеры, цвет и положение объекта «${TYPE_LABELS[object.type]}».`);
    hasPlayer ||= object.type === 'player';
    hasGoal ||= object.type === 'goal';
    if (object.type === 'particle' && !validParticleEmitter(object.emitter))
      return fail('Проверь параметры эмиттера частиц.');
    if (object.type === 'trigger') {
      const t = object.trigger;
      const kind = t?.kind || 'move';
      const validGroup = Number.isInteger(t?.groupId) &&
        t.groupId >= 0 && t.groupId <= GROUP_ID_MAX;
      const validTarget = Number.isInteger(t?.targetId) &&
        t.targetId >= 0 && t.targetId <= 1_000_000;
      const directTargetConfigured = validTarget && t.targetId > 0;
      const targetConfigured = validGroup || validTarget;
      const moveX = t?.valueX ?? t?.value;
      const moveY = t?.valueY ?? 0;
      const degrees = t?.degrees ?? t?.value;
      const timedRotate = kind === 'rotate' && t?.duration !== undefined;
      const validRotation = timedRotate ? validGroup && t.action === 'rotate' &&
          Number.isInteger(t.duration) && t.duration >= 1 && t.duration <= ROTATION_DURATION_MAX :
        finite(degrees) && degrees >= -360 && degrees <= 360;
      const foreverConfigured = kind === 'forever' && validGroup;
      const specialKind = kind === 'invisibility' || kind === 'no-collision';
      const gravityKind = kind === 'gravity';
      const fixedAction = kind === 'invisibility' ? 'invisible' : 'no-collision';
      const validAction = gravityKind ? t?.action === 'set-gravity' :
        specialKind ? t?.action === fixedAction :
        foreverConfigured ? FOREVER_ACTIONS.has(t.action) : ACTIONS.has(t?.action);
      const validValues = kind === 'move' ? finite(moveX) && moveX >= -9999 && moveX <= 9999 &&
          finite(moveY) && moveY >= -9999 && moveY <= 9999 :
        kind === 'rotate' ? validRotation :
        kind === 'forever' ? foreverConfigured ||
          (validTarget && finite(t?.value) && t.value >= -100 && t.value <= 100) :
        gravityKind ? Number.isInteger(t?.value) && t.value >= -100 && t.value <= 100 :
        specialKind;
      if (!t || !TRIGGER_KINDS.includes(kind) || !EVENTS.has(t.event) ||
          !validAction || (!gravityKind && kind !== 'forever' && !targetConfigured) ||
          (kind === 'forever' && !foreverConfigured && !validTarget) ||
          (specialKind && !validGroup && !directTargetConfigured) ||
          (t.groupId !== undefined && !validGroup) || !validValues || !rgb(t.color))
        return fail('Настрой триггер: событие, группу, действие и параметры.');
    }
  }
  if (!hasPlayer || !hasGoal) return fail('На уровне обязательны игрок и финиш.');
  return {ok: true, message: ''};
}

function recordObject(object) {
  const result = {
    id: object.id, type: object.type, name: object.name || TYPE_LABELS[object.type],
    x: object.x, y: object.y, w: object.w, h: object.h, angle: object.angle || 0,
    flipX: object.flipX === true, flipY: object.flipY === true,
    color: object.color, number: object.number || 0, visible: object.visible !== false,
    layer: object.layer || 0, layer2: object.layer2 || 0, zOrder: object.zOrder || 0,
  };
  if (object.type === 'particle')
    result.emitter = normalizeParticleEmitter(object.emitter);
  if (object.type === 'trigger') {
    const t = object.trigger || {};
    const kind = TRIGGER_KINDS.includes(t.kind) ? t.kind : 'move';
    const trigger = {kind, event: t.event || 'touch', action: t.action || 'move',
      color: t.color || '#ffc54e'};
    if (kind !== 'gravity') {
      trigger.targetId = Number.isInteger(t.targetId) ? t.targetId : 0;
      if (Number.isInteger(t.groupId)) trigger.groupId = t.groupId;
    }
    if (kind === 'gravity') {
      trigger.value = t.value ?? 0;
    } else if (kind === 'move') {
      trigger.valueX = t.valueX ?? t.value ?? 0;
      trigger.valueY = t.valueY ?? 0;
      trigger.value = trigger.valueX; // legacy readers treat this as the X offset
    } else if (kind === 'rotate') {
      if (Number.isInteger(t.duration)) trigger.duration = t.duration;
      else {
        trigger.degrees = t.degrees ?? t.value ?? 0;
        trigger.value = trigger.degrees; // legacy readers treat this as the angle
      }
    } else if (kind === 'forever' && !Number.isInteger(t.groupId)) {
      // Preserve the pre-group cyclic trigger payload when importing old records.
      trigger.value = t.value ?? 0;
    }
    result.trigger = trigger;
  }
  return result;
}
export function publishedRecord(id, level) {
  const check = validateDraft(level);
  if (!check.ok) throw new Error(check.message);
  if (!/^[1-9][0-9]{0,5}$/.test(id)) throw new Error('Некорректный ID публикации.');
  return {
    format: 'PVG3-PUBLISHED-LEVEL', version: 1, id,
    title: level.title.trim(), description: level.description,
    project: {format: 'PVG3-MAKER', version: 1,
      width: LEVEL_WIDTH, height: LEVEL_HEIGHT,
      objects: level.objects.map(recordObject)},
  };
}

export function draftFromPublished(record) {
  if (!isPublishedRecord(record)) throw new Error('Уровень имеет неподдерживаемый формат.');
  const objects = record.project.objects.map(object => {
    const trigger = object.type === 'trigger' ? {...object.trigger,
      kind: TRIGGER_KINDS.includes(object.trigger?.kind) ? object.trigger.kind : 'move'} : object.trigger;
    if (trigger?.kind === 'rotate' && trigger.duration === undefined) {
      const target = record.project.objects.find(candidate => candidate.id === trigger.targetId);
      if (!Number.isInteger(trigger.groupId))
        trigger.groupId = Number.isInteger(target?.number) ? target.number : 0;
      trigger.duration = 3;
      trigger.action = 'rotate';
      delete trigger.degrees;delete trigger.value;
    }
    return {...object, trigger,
      ...(object.type === 'particle' ? {emitter: normalizeParticleEmitter(object.emitter)} : {}),
      flipX: object.flipX === true, flipY: object.flipY === true,
      layer: Number.isInteger(object.layer) ? object.layer : 0,
      layer2: Number.isInteger(object.layer2) ? object.layer2 : 0,
      zOrder: Number.isInteger(object.zOrder) ? object.zOrder : 0,
    };
  });
  return {localId: localId(), title: record.title, description: record.description || '',
    width: LEVEL_WIDTH, height: LEVEL_HEIGHT, objects};
}

export function isPublishedRecord(record, expectedId = record?.id) {
  if (!record || record.format !== 'PVG3-PUBLISHED-LEVEL' || record.version !== 1 ||
      record.id !== expectedId || !/^[1-9][0-9]{0,5}$/.test(record.id) ||
      typeof record.title !== 'string' || !record.project ||
      record.project.format !== 'PVG3-MAKER' || record.project.version !== 1) return false;
  const draft = {title: record.title, description: record.description || '',
    width: record.project.width, height: record.project.height,
    objects: record.project.objects};
  return validateDraft(draft).ok;
}

export function resolveControlMode(preference, coarsePointer) {
  if (preference === 'buttons' || preference === 'keyboard') return preference;
  return coarsePointer ? 'buttons' : 'keyboard';
}

export function createTouchButtonState() {
  const held = {back: new Set(), forward: new Set()};
  const pointers = new Map();
  return {
    press(pointerId, direction) {
      if (!Object.hasOwn(held, direction) || pointers.has(pointerId)) return false;
      pointers.set(pointerId, direction);held[direction].add(pointerId);return true;
    },
    release(pointerId) {
      const direction = pointers.get(pointerId);
      if (!direction) return false;
      pointers.delete(pointerId);held[direction].delete(pointerId);return true;
    },
    clear() {held.back.clear();held.forward.clear();pointers.clear();},
    get axis() {return Number(held.forward.size > 0) - Number(held.back.size > 0);},
  };
}

export function createPreviewState(level) {
  const player = level.objects.find(o => o.type === 'player');
  if (!player) throw new Error('Уровень без игрока.');
  const objects = copy(level.objects);
  for (const object of objects)
    if (object.type === 'particle') object.emitter = normalizeParticleEmitter(object.emitter);
  const state = {objects, x: player.x * TILE_W, y: player.y * TILE_H,
    vx: 0, vy: 0, gravity: 1450, grounded: false, time: 0, coins: 0, won: false,
    collected: [], triggerFired: [], triggerActive: [], triggerTimers: Object.create(null),
    invisible: [], noCollision: [], groupRotations: [], jumpHeld: false,
    orbActivated: false, spawn: {x: player.x * TILE_W, y: player.y * TILE_H}};
  runTriggers(state, 'start');
  return state;
}
function playerTriangleContact(px, py, pw, ph, object, vx, vy, silhouette,
                                climbable = false) {
  const angle = (Number(object.angle) || 0) * Math.PI / 180;
  const c = Math.cos(angle), s = Math.sin(angle);
  const objectX = (object.x + object.w / 2) * TILE_W;
  const objectY = (object.y + object.h / 2) * TILE_H;
  const flipX = objectFlipX(object);
  const vertices = silhouette.map(([sourceU, sourceV]) => {
    const u = flipX ? 1 - sourceU : sourceU;
    const v = object.flipY ? 1 - sourceV : sourceV;
    const localX = (u - .5) * object.w * TILE_W;
    const localY = (v - .5) * object.h * TILE_H;
    return [objectX + localX * c - localY * s,
      objectY + localX * s + localY * c];
  });
  const axes = [[1, 0], [0, 1]];
  for (let i = 0; i < vertices.length; i++) {
    const a = vertices[i], b = vertices[(i + 1) % vertices.length];
    const edgeX = b[0] - a[0], edgeY = b[1] - a[1];
    const length = Math.hypot(edgeX, edgeY);
    if (length > 1e-9) axes.push([-edgeY / length, edgeX / length]);
  }

  const playerX = px + pw / 2, playerY = py + ph / 2;
  let smallestDepth = Infinity, normalX = 0, normalY = 0;
  let climbDepth = Infinity, climbX = 0, climbY = 0;
  for (const [axisX, axisY] of axes) {
    let objectMin = Infinity, objectMax = -Infinity;
    for (const [x, y] of vertices) {
      const projection = x * axisX + y * axisY;
      objectMin = Math.min(objectMin, projection);
      objectMax = Math.max(objectMax, projection);
    }
    const playerCenter = playerX * axisX + playerY * axisY;
    const playerRadius = pw / 2 * Math.abs(axisX) + ph / 2 * Math.abs(axisY);
    const playerMin = playerCenter - playerRadius;
    const playerMax = playerCenter + playerRadius;
    const movePositive = objectMax - playerMin;
    const moveNegative = playerMax - objectMin;
    if (movePositive <= 0 || moveNegative <= 0) return null;
    const depth = Math.min(movePositive, moveNegative);
    const motion = vx * axisX + vy * axisY;
    const positive = movePositive < moveNegative - 1e-7 ||
      (Math.abs(movePositive - moveNegative) <= 1e-7 && motion <= 0);
    const candidateX = axisX * (positive ? 1 : -1);
    const candidateY = axisY * (positive ? 1 : -1);
    if (depth < smallestDepth) {
      smallestDepth = depth;
      normalX = candidateX;normalY = candidateY;
    }
    const candidateMotion = vx * candidateX + vy * candidateY;
    if (Math.abs(candidateX) > .1 && candidateY < -.2 && candidateMotion < -1e-7 &&
        depth < climbDepth) {
      climbDepth = depth;climbX = candidateX;climbY = candidateY;
    }
  }
  if (climbable && climbDepth < Infinity)
    return {x: climbX, y: climbY, depth: climbDepth};
  return {x: normalX, y: normalY, depth: smallestDepth};
}

function playerInsideOrbRange(px, py, pw, ph, object) {
  const centerX = (object.x + object.w / 2) * TILE_W;
  const centerY = (object.y + object.h / 2) * TILE_H;
  const radius = Math.min(object.w * TILE_W, object.h * TILE_H) * .46;
  const closestX = clamp(centerX, px, px + pw);
  const closestY = clamp(centerY, py, py + ph);
  return Math.hypot(closestX - centerX, closestY - centerY) <= radius + .1;
}
function playerObjectContact(px, py, pw, ph, object, vx = 0, vy = 0) {
  if (object.type === 'particle') return null;
  if (object.type === 'slope')
    return playerTriangleContact(px, py, pw, ph, object, vx, vy,
                                 SLOPE_VERTICES, true);
  if (object.type === 'hazard')
    return playerTriangleContact(px, py, pw, ph, object, vx, vy,
                                 SPIKE_VERTICES, false);
  const [left, top, right, bottom] = artBounds(object.type,
    objectFlipX(object), object.flipY);
  const frameW = object.w * TILE_W, frameH = object.h * TILE_H;
  const localOffsetX = ((left + right) / 2 - .5) * frameW;
  const localOffsetY = ((top + bottom) / 2 - .5) * frameH;
  const angle = (Number(object.angle) || 0) * Math.PI / 180;
  const c = Math.cos(angle), s = Math.sin(angle);
  const objectX = (object.x + object.w / 2) * TILE_W +
    localOffsetX * c - localOffsetY * s;
  const objectY = (object.y + object.h / 2) * TILE_H +
    localOffsetX * s + localOffsetY * c;
  const halfObjectW = frameW * (right - left) / 2;
  const halfObjectH = frameH * (bottom - top) / 2;
  const halfPlayerW = pw / 2, halfPlayerH = ph / 2;
  const dx = px + halfPlayerW - objectX;
  const dy = py + halfPlayerH - objectY;
  const axes = [[1, 0], [0, 1], [c, s], [-s, c]];
  let smallestOverlap = Infinity, normalX = 0, normalY = 0;
  for (const [axisX, axisY] of axes) {
    const objectRadius = halfObjectW * Math.abs(axisX * c + axisY * s) +
      halfObjectH * Math.abs(-axisX * s + axisY * c);
    const playerRadius = halfPlayerW * Math.abs(axisX) + halfPlayerH * Math.abs(axisY);
    const distance = dx * axisX + dy * axisY;
    const penetration = objectRadius + playerRadius - Math.abs(distance);
    if (penetration <= 0) return null;
    if (penetration < smallestOverlap) {
      const motion = vx * axisX + vy * axisY;
      const sign = distance > 1e-7 ? 1 : distance < -1e-7 ? -1 : motion > 0 ? -1 : 1;
      smallestOverlap = penetration;
      normalX = axisX * sign;normalY = axisY * sign;
    }
  }
  return {x: normalX, y: normalY, depth: smallestOverlap};
}
function activatePreviewOrb(state, player) {
  if (!player || state.noCollision.includes(player.id)) return null;
  const box = playerVisibleHitbox(state, player);
  const orb = [...state.objects].reverse().find(object =>
    (object.type === 'orb-yellow' || object.type === 'orb-orange') &&
    object.visible !== false && !state.noCollision.includes(object.id) &&
    playerInsideOrbRange(box.x, box.y, box.w, box.h, object));
  if (!orb) return null;
  state.vy = orb.type === 'orb-orange' ? -1050 : -650;
  state.grounded = false;
  state.orbActivated = true;
  return orb;
}
function resolvePreviewPlayer(state, player, solids) {
  let grounded = false;
  for (let iteration = 0; iteration < 4; iteration++) {
    let collided = false;
    for (const object of solids) {
      const box = playerVisibleHitbox(state, player);
      const contact = playerObjectContact(box.x, box.y, box.w, box.h, object,
                                           state.vx, state.vy);
      if (!contact) continue;
      state.x += contact.x * contact.depth;
      state.y += contact.y * contact.depth;
      const inwardVelocity = state.vx * contact.x + state.vy * contact.y;
      if (inwardVelocity < 0) {
        state.vx -= inwardVelocity * contact.x;
        state.vy -= inwardVelocity * contact.y;
      }
      if (contact.y < (object.type === 'slope' ? -.35 : -.5)) grounded = true;
      collided = true;
    }
    if (!collided) break;
  }
  state.grounded = grounded;
}
function executeTrigger(state, trigger) {
  const t = trigger.trigger || {};
  const kind = TRIGGER_KINDS.includes(t.kind) ? t.kind : 'move';
  if (kind === 'gravity') {
    const offset = Number.isInteger(t.value) ? clamp(t.value, -100, 100) : 0;
    state.gravity = 1450 + offset * 10;
    return;
  }
  if (kind === 'rotate' && t.duration !== undefined) {
    if (Number.isInteger(t.groupId) && Number.isInteger(t.duration) &&
        t.duration >= 1 && t.duration <= ROTATION_DURATION_MAX) {
      let rotation = state.groupRotations.find(item => item.groupId === t.groupId);
      if (!rotation) {
        rotation = {groupId: t.groupId, remaining: 0};
        state.groupRotations.push(rotation);
      }
      rotation.remaining = t.duration;
    }
    return;
  }
  if (kind === 'forever') {
    if (Number.isInteger(t.groupId)) {
      const visible = t.action === 'activate';
      for (const object of state.objects) {
        if (object.id !== trigger.id && object.type !== 'trigger' &&
            object.number === t.groupId) object.visible = visible;
      }
    } else {
      // Compatibility for published records created before forever had group actions.
      const first = state.objects.find(object => object.visible !== false &&
        object.type === 'trigger' && object.id !== trigger.id &&
        object.trigger?.kind !== 'forever');
      if (first) executeTrigger(state, first);
    }
    return;
  }
  const targets = Number.isInteger(t.groupId) ? state.objects.filter(object =>
    object.type !== 'trigger' && object.number === t.groupId) :
    state.objects.filter(object => object.id === t.targetId);
  if (kind === 'invisibility') {
    for (const target of targets)
      if (!state.invisible.includes(target.id)) state.invisible.push(target.id);
    return;
  }
  if (kind === 'no-collision') {
    for (const target of targets)
      if (!state.noCollision.includes(target.id)) state.noCollision.push(target.id);
    return;
  }
  const action = kind === 'rotate' ? 'rotate' : t.action;
  for (const target of targets) {
    switch (action) {
    case 'toggle': target.visible = !target.visible;break;
    case 'move':
      target.x = worldClamp(target.x + (t.valueX ?? t.value ?? 0), target.w);
      target.y = worldClamp(target.y + (t.valueY ?? 0), target.h);break;
    case 'rotate': {
      const degrees = kind === 'rotate' ? (t.degrees ?? t.value ?? 0) : (t.value ?? 0);
      target.angle = ((target.angle + degrees) % 360 + 360) % 360;break;
    }
    case 'recolor': target.color = t.color;break;
    case 'number': target.number = clamp(Math.trunc(t.value), 0, 9999);break;
    }
  }
}
function stepGroupRotations(state, dt) {
  for (let i = 0; i < state.groupRotations.length;) {
    const rotation = state.groupRotations[i];
    const step = Math.min(dt, rotation.remaining);
    if (step > 0) for (const object of state.objects) {
      if (object.type === 'trigger' || object.number !== rotation.groupId) continue;
      object.angle = ((object.angle + ROTATION_DEGREES_PER_SECOND * step) % 360 + 360) % 360;
    }
    rotation.remaining -= step;
    if (rotation.remaining <= 1e-7) state.groupRotations.splice(i, 1);
    else i++;
  }
}
function runTriggers(state, event) {
  for (const trigger of state.objects) {
    if (!trigger.visible || trigger.type !== 'trigger' ||
        state.noCollision.includes(trigger.id) || trigger.trigger?.event !== event) continue;
    const kind = TRIGGER_KINDS.includes(trigger.trigger.kind) ? trigger.trigger.kind : 'move';
    const completed = state.triggerFired.includes(trigger.id);
    const legacyLoop = kind === 'forever' && !Number.isInteger(trigger.trigger?.groupId);
    const active = state.triggerActive.includes(trigger.id);
    if (legacyLoop ? active : completed) continue;
    const player = state.objects.find(o => o.type === 'player');
    const box = player ? playerVisibleHitbox(state, player) : null;
    if (event === 'touch' && (!box ||
        !playerObjectContact(box.x, box.y, box.w, box.h, trigger, state.vx, state.vy))) continue;
    if (legacyLoop) {
      state.triggerActive.push(trigger.id);
      state.triggerTimers[trigger.id] = 0;
    } else {
      state.triggerFired.push(trigger.id);
      executeTrigger(state, trigger);
    }
  }
}
function runForeverTriggers(state, dt) {
  // Automatic loops begin when visible and repeat at most once per simulation
  // step, so a stalled frame can never trigger unbounded catch-up work.
  for (const trigger of state.objects) {
    if (trigger.type !== 'trigger' || trigger.trigger?.kind !== 'forever' ||
        Number.isInteger(trigger.trigger?.groupId) || trigger.visible === false) continue;
    if (!state.triggerActive.includes(trigger.id)) {
      state.triggerActive.push(trigger.id);state.triggerTimers[trigger.id] = 0;
      continue;
    }
    const elapsed = (state.triggerTimers[trigger.id] || 0) + dt;
    if (elapsed >= .5) {
      state.triggerTimers[trigger.id] = elapsed - .5;
      executeTrigger(state, trigger);
    } else state.triggerTimers[trigger.id] = elapsed;
  }
}
export function stepPreview(state, input = {}, dt = 1 / 60) {
  if (!state || state.won) return state;
  dt = clamp(Number(dt) || 0, 0, .05);
  state.time += dt;
  state.orbActivated = false;
  const player = state.objects.find(o => o.type === 'player');
  if (!player) return state;
  const pw = player.w * TILE_W;
  const playerCollisionEnabled = !state.noCollision.includes(player.id);
  const solids = playerCollisionEnabled ? state.objects.filter(o =>
    o.visible && !state.noCollision.includes(o.id) &&
    ['block', 'ground', 'slope'].includes(o.type)) : [];
  state.vx = (input.axis || 0) * 250;
  const jumpPressed = !!input.jump && !state.jumpHeld;
  state.jumpHeld = !!input.jump;
  if (jumpPressed && !activatePreviewOrb(state, player) && state.grounded) {
    state.vy = -570;state.grounded = false;
  }
  const gravity = Number.isFinite(state.gravity) ? state.gravity : 1450;
  const predictedVy = Math.min(780, state.vy + gravity * dt);
  const displacement = Math.max(Math.abs(state.vx * dt), Math.abs(predictedVy * dt));
  const substeps = clamp(Math.ceil(displacement / 4), 1, 16);
  const subDt = dt / substeps;
  state.grounded = false;
  for (let step = 0; step < substeps; step++) {
    state.vy = Math.min(780, state.vy + gravity * subDt);
    state.x = clamp(state.x + state.vx * subDt,
      -WORLD_LIMIT * TILE_W, WORLD_LIMIT * TILE_W - pw);
    state.y += state.vy * subDt;
    if (playerCollisionEnabled) resolvePreviewPlayer(state, player, solids);
  }
  if (state.y > WORLD_LIMIT * TILE_H) {
    state.x = state.spawn.x;state.y = state.spawn.y;state.vx = state.vy = 0;
    state.grounded = false;
  }
  if (playerCollisionEnabled) for (const o of state.objects) {
    if (!o.visible || o.type === 'player' || o.type === 'trigger' ||
        o.type === 'orb-yellow' || o.type === 'orb-orange' ||
        state.noCollision.includes(o.id)) continue;
    const box = playerVisibleHitbox(state, player);
    if (!playerObjectContact(box.x, box.y, box.w, box.h, o, state.vx, state.vy)) continue;
    if (o.type === 'coin' && !state.collected.includes(o.id)) {
      state.collected.push(o.id);state.coins++;o.visible = false;runTriggers(state, 'coin');
    } else if (o.type === 'hazard' || o.type === 'enemy') {
      state.x = state.spawn.x;state.y = state.spawn.y;state.vx = state.vy = 0;
      state.grounded = false;
    } else if (o.type === 'goal') state.won = true;
  }
  runTriggers(state, 'touch');
  if (input.trigger) runTriggers(state, 'manual');
  runForeverTriggers(state, dt);
  stepGroupRotations(state, dt);
  return state;
}

function triggerArtKey(kind) {
  return kind === 'rotate' ? 'triggerRotate' :
    kind === 'forever' ? 'triggerForever' :
    kind === 'invisibility' ? 'triggerInvisibility' :
    kind === 'no-collision' ? 'triggerNoCollision' :
    kind === 'gravity' ? 'triggerGravity' : 'triggerMove';
}
function drawWorkshopArt(ctx, art, type, x, y, w, h, triggerKind = 'move') {
  if (type === 'particle') return false;
  const image = type === 'trigger' ? (art?.[triggerArtKey(triggerKind)] || art?.trigger) : art?.[type];
  if (!image?.naturalWidth) return false;
  if (type !== 'ground') {
    ctx.drawImage(image, x, y, w, h);
    return true;
  }
  // Repeat the supplied 2×1 platform tile, including at the edges of the view.
  const tileW = 2 * TILE_W, tileH = TILE_H;
  for (let dy = 0; dy < h; dy += tileH)
    for (let dx = 0; dx < w; dx += tileW)
      ctx.drawImage(image, x + dx, y + dy,
        Math.min(tileW, w - dx), Math.min(tileH, h - dy));
  return true;
}
function drawWorkshopObject(ctx, art, object, x, y, w, h) {
  const angle = ((object.angle || 0) % 360) * Math.PI / 180;
  const flipX = objectFlipX(object), flipY = object.flipY === true;
  if (!angle && !flipX && !flipY)
    return drawWorkshopArt(ctx, art, object.type, x, y, w, h,
                           object.trigger?.kind);
  ctx.save();ctx.translate(x + w / 2, y + h / 2);ctx.rotate(angle);
  ctx.scale(flipX ? -1 : 1, flipY ? -1 : 1);
  const drawn = drawWorkshopArt(ctx, art, object.type, -w / 2, -h / 2, w, h,
                                object.trigger?.kind);
  ctx.restore();
  return drawn;
}

function drawParticleEmitterMarker(ctx, x, y, w, h, color, angle = 0) {
  ctx.save();ctx.translate(x + w / 2, y + h / 2);
  ctx.rotate((Number(angle) || 0) * Math.PI / 180);
  const radius = Math.max(2, Math.min(Math.max(0, w), Math.max(0, h)) * .36);
  ctx.fillStyle = 'rgba(20,40,60,.94)';
  ctx.beginPath();ctx.arc(0, 0, radius, 0, Math.PI * 2);ctx.fill();
  ctx.strokeStyle = color;ctx.lineWidth = Math.max(2, radius * .12);ctx.stroke();
  ctx.fillStyle = color;
  ctx.font = `bold ${Math.max(10, Math.min(32, radius * 1.45))}px PTSans, sans-serif`;
  ctx.textAlign = 'center';ctx.textBaseline = 'middle';ctx.fillText('P', 0, 1);
  ctx.restore();
}

export function drawEditorCanvas(canvas, level, selectedId = 0, tool = 'build', art = {}, camera = {x: 0, y: 0}) {
  const ctx = canvas.getContext('2d');
  const cameraX = clamp(Number(camera?.x) || 0, -WORLD_LIMIT, WORLD_LIMIT - LEVEL_WIDTH);
  const cameraY = clamp(Number(camera?.y) || 0, -WORLD_LIMIT, WORLD_LIMIT - LEVEL_HEIGHT);
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#14283c';ctx.fillRect(0, 0, canvas.width, canvas.height);
  const firstCol = Math.floor(cameraX), firstRow = Math.floor(cameraY);
  for (let c = firstCol; (c - cameraX) * TILE_W <= canvas.width; c++) {
    const x = Math.round((c - cameraX) * TILE_W);
    ctx.strokeStyle = '#2a4056';ctx.lineWidth = 1;
    ctx.beginPath();ctx.moveTo(x, 0);ctx.lineTo(x, canvas.height);ctx.stroke();
  }
  for (let r = firstRow; (r - cameraY) * TILE_H <= canvas.height; r++) {
    const y = Math.round((r - cameraY) * TILE_H);
    ctx.beginPath();ctx.moveTo(0, y);ctx.lineTo(canvas.width, y);ctx.stroke();
  }
  const drawObjects = [...level.objects].sort((a, b) => (a.layer || 0) - (b.layer || 0) ||
      (a.zOrder || 0) - (b.zOrder || 0));
  const selected = selectedId instanceof Set ? selectedId :
    new Set(Array.isArray(selectedId) ? selectedId : selectedId ? [selectedId] : []);
  for (const o of drawObjects) {
    if (o.visible === false) continue;
    const x = (o.x - cameraX) * TILE_W + 3, y = (o.y - cameraY) * TILE_H + 3;
    const w = o.w * TILE_W - 6, h = o.h * TILE_H - 6;
    ctx.fillStyle = o.color || DEFAULT_COLORS[o.type];
    if (o.type === 'particle') {
      drawParticleEmitterMarker(ctx, x, y, w, h, o.color || DEFAULT_COLORS.particle, o.angle);
    } else {
      const triggerArt = o.type === 'trigger' &&
        (art?.[triggerArtKey(o.trigger?.kind)]?.naturalWidth || art.trigger?.naturalWidth);
      if (triggerArt) {ctx.globalAlpha = .2;ctx.fillRect(x, y, w, h);ctx.globalAlpha = 1;}
      const hasArt = drawWorkshopObject(ctx, art, o, x, y, w, h);
      if (!hasArt && o.type === 'coin') {
        ctx.beginPath();ctx.arc(x + w / 2, y + h / 2, Math.min(w, h) / 2, 0, Math.PI * 2);ctx.fill();
      } else if (!hasArt && o.type === 'hazard') {
        ctx.beginPath();ctx.moveTo(x + w / 2, y);ctx.lineTo(x + w, y + h);ctx.lineTo(x, y + h);ctx.closePath();ctx.fill();
      } else if (!hasArt) {
        ctx.fillRect(x, y, w, h);
        if (o.type === 'trigger') {ctx.strokeStyle = '#fff2d8';ctx.lineWidth = 3;ctx.strokeRect(x + 2, y + 2, w - 4, h - 4);}
        if (o.type === 'goal') {ctx.fillStyle = '#fff2d8';ctx.fillRect(x + w * .65, y, 4, h);}
        if (o.type === 'player') {
          ctx.fillStyle = '#fff';ctx.fillRect(x + w * .18, y + h * .25, w * .18, h * .12);
          ctx.fillRect(x + w * .58, y + h * .25, w * .18, h * .12);
        }
      }
    }
    if (selected.has(o.id)) {
      const angle = ((o.angle || 0) % 360) * Math.PI / 180;
      const silhouette = o.type === 'slope' ? SLOPE_VERTICES :
        o.type === 'hazard' ? SPIKE_VERTICES : null;
      ctx.save();ctx.translate(x + w / 2, y + h / 2);ctx.rotate(angle);
      ctx.scale(objectFlipX(o) ? -1 : 1, o.flipY ? -1 : 1);
      ctx.strokeStyle = '#27d2d6';ctx.lineWidth = 4;
      if (silhouette) {
        ctx.beginPath();
        silhouette.forEach(([u, v], index) => {
          const px = (u - .5) * w, py = (v - .5) * h;
          if (index) ctx.lineTo(px, py);else ctx.moveTo(px, py);
        });
        ctx.closePath();ctx.stroke();
      } else {
        const [left, top, right, bottom] = artBounds(o.type);
        ctx.strokeRect(-w / 2 + left * w - 1, -h / 2 + top * h - 1,
          (right - left) * w + 2, (bottom - top) * h + 2);
      }
      ctx.restore();
    }
    ctx.fillStyle = '#10131d';ctx.font = '14px PTSans, sans-serif';ctx.fillText(String(o.number || ''), x + 4, y + 17);
  }
  ctx.fillStyle = 'rgba(0,0,0,.55)';ctx.fillRect(8, 8, 400, 30);
  ctx.fillStyle = '#fff2d8';ctx.font = '16px PTSans, sans-serif';
  const hint = tool === 'delete' ? 'Выбери объект для удаления' :
    tool === 'pan' ? 'Перетаскивай, чтобы перемещать бесконечную карту' :
    `Бесконечная карта · вид ${Math.floor(cameraX)}, ${Math.floor(cameraY)}`;
  ctx.fillText(hint, 18, 29);
}

const PREVIEW_PARTICLE_COUNT = 5;
function drawParticleDot(ctx, x, y, radius, color, alpha) {
  if (alpha <= 0) return;
  const previousAlpha = ctx.globalAlpha ?? 1;
  ctx.globalAlpha = Math.min(1, alpha);ctx.fillStyle = color;
  ctx.beginPath();ctx.arc(x, y, radius, 0, Math.PI * 2);ctx.fill();
  ctx.globalAlpha = previousAlpha;
}
function particleHash(id, index, lane) {
  let value = ((id >>> 0) ^ Math.imul(index >>> 0, 0x9e3779b9) ^
    Math.imul(lane >>> 0, 0x85ebca6b)) >>> 0;
  value ^= value >>> 16;value = Math.imul(value, 0x7feb352d) >>> 0;
  value ^= value >>> 15;value = Math.imul(value, 0x846ca68b) >>> 0;
  value ^= value >>> 16;
  return value >>> 0;
}
function particleRandom(id, index, lane) {
  return (particleHash(id, index, lane) >>> 8) / 16777216;
}
export function sampleParticleEmitter(settings, id, time, slot, areaW, areaH) {
  const emitter = normalizeParticleEmitter(settings);
  if (!emitter.enabled || !Number.isFinite(time) || time < 0 ||
      !Number.isInteger(slot) || slot < 0 || slot >= 48) return null;
  const {lifetime, rate} = emitter;
  let age, particleIndex;
  if (emitter.continuous) {
    const phase = time * rate + particleRandom(id, 0, 1);
    const newest = Math.floor(phase);
    age = (phase - newest + slot) / rate;
    if (age > lifetime) return null;
    particleIndex = newest - slot;
  } else {
    const period = 1.5;
    const currentBurst = Math.floor(time / period);
    const burstPhase = time - currentBurst * period;
    const burstIndex = Math.floor(slot / rate);
    const indexInBurst = slot % rate;
    if (currentBurst < burstIndex || burstIndex * period > lifetime + period) return null;
    const birthOffset = .24 * indexInBurst / rate;
    age = burstPhase + burstIndex * period - birthOffset;
    if (age < 0 || age > lifetime) return null;
    particleIndex = (currentBurst - burstIndex) * rate + indexInBurst;
  }
  const seed = particleIndex >>> 0;
  const angle = (emitter.direction + (particleRandom(id, seed, 2) - .5) *
    emitter.spread) * Math.PI / 180;
  const speed = emitter.speed * (.75 + .5 * particleRandom(id, seed, 3));
  const remaining = 1 - age / lifetime;
  const x = (particleRandom(id, seed, 4) - .5) * areaW * .32 + Math.cos(angle) * speed * age;
  let y = (particleRandom(id, seed, 5) - .5) * areaH * .32 + Math.sin(angle) * speed * age;
  if (emitter.gravityEnabled) y += .5 * emitter.gravity * age * age;
  const size = emitter.size * (.62 + .38 * remaining) *
    (.82 + .36 * particleRandom(id, seed, 6));
  const opacity = remaining * (.78 + .22 * particleRandom(id, seed, 7));
  return {x, y, size, opacity};
}
function drawConfiguredParticleTrail(ctx, emitterValue, id, time, x, y,
                                     areaW, areaH, color, angle = 0,
                                     flipX = false, flipY = false,
                                     maxParticles = 48) {
  const emitter = normalizeParticleEmitter(emitterValue);
  let drawn = 0;
  ctx.save();ctx.translate(x, y);ctx.rotate((Number(angle) || 0) * Math.PI / 180);
  ctx.scale(flipX ? -1 : 1, flipY ? -1 : 1);
  for (let slot = 0; slot < Math.min(48, maxParticles); slot++) {
    const particle = sampleParticleEmitter(emitter, id, time, slot, areaW, areaH);
    if (!particle) continue;
    if (emitter.glow)
      drawParticleDot(ctx, particle.x, particle.y, particle.size * 1.5,
        color, particle.opacity * .16);
    drawParticleDot(ctx, particle.x, particle.y,
      Math.max(.7, particle.size * .5), color, particle.opacity);
    drawn++;
  }
  ctx.restore();
  return drawn;
}
export function drawParticleEmitterPreview(canvas, settings,
                                          color = '#68f0d8', time = 0) {
  const ctx = canvas.getContext('2d');
  const width = canvas.width, height = canvas.height;
  const emitter = normalizeParticleEmitter(settings);
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = '#03070b';ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = '#203445';ctx.lineWidth = 1;
  ctx.strokeRect(1, 1, width - 2, height - 2);
  const centerX = width / 2, centerY = height * .68;
  const count = drawConfiguredParticleTrail(ctx, emitter, 17, Math.max(0, time),
    centerX, centerY, 44, 34, color);
  drawParticleEmitterMarker(ctx, centerX - 26, centerY - 26, 52, 52, color);
  return count;
}
function drawPreviewParticles(ctx, state, cameraX, cameraY, viewWidth, viewHeight) {
  const time = Number(state.time) || 0;
  let particleBudget = 4096;
  for (const object of state.objects) {
    if (object.visible === false ||
        (object.type !== 'particle' && object.type !== 'orb-yellow' &&
         object.type !== 'orb-orange') || state.invisible?.includes(object.id) ||
        (object.type === 'particle' &&
         (particleBudget <= 0 || object.emitter?.enabled === false))) continue;
    const width = object.w * TILE_W, height = object.h * TILE_H;
    const centerX = (object.x + object.w / 2) * TILE_W - cameraX;
    const centerY = (object.y + object.h / 2) * TILE_H - cameraY;
    const emitter = object.type === 'particle' ?
      normalizeParticleEmitter(object.emitter) : null;
    const extent = Math.max(width, height) + 32 + (emitter ?
      emitter.speed * emitter.lifetime +
      (emitter.gravityEnabled ? .5 * emitter.gravity * emitter.lifetime ** 2 : 0) : 0);
    if (centerX + extent < 0 || centerX - extent > viewWidth ||
        centerY + extent < 0 || centerY - extent > viewHeight) continue;
    const angle = (Number(object.angle) || 0) * Math.PI / 180;
    const cos = Math.cos(angle), sin = Math.sin(angle);
    if (object.type === 'orb-yellow' || object.type === 'orb-orange') {
      const color = object.type === 'orb-orange' ? '#ffbb66' : '#fff86b';
      const orbitRadius = Math.min(width, height) * .58;
      for (let index = 0; index < PREVIEW_PARTICLE_COUNT; index++) {
        const phase = time * 2.1 + index * Math.PI * 2 / PREVIEW_PARTICLE_COUNT +
          (object.id % 4093) * .023;
        const localX = Math.cos(phase) * orbitRadius * (object.flipX ? -1 : 1);
        const localY = Math.sin(phase) * orbitRadius * .7 * (object.flipY ? -1 : 1);
        const x = centerX + localX * cos - localY * sin;
        const y = centerY + localX * sin + localY * cos;
        const flicker = .5 + .5 * Math.sin(phase * 1.6);
        drawParticleDot(ctx, x, y, 1.2 + flicker * .8, color,
          .18 + flicker * .62);
      }
    } else {
      particleBudget -= drawConfiguredParticleTrail(ctx,
        emitter || DEFAULT_PARTICLE_EMITTER, object.id, time,
        centerX, centerY, width, height,
        object.color || DEFAULT_COLORS.particle, object.angle,
        object.flipX === true, object.flipY === true, particleBudget);
    }
  }
}

export function drawPreviewCanvas(canvas, state, control = 'keyboard', art = {}) {
  const ctx = canvas.getContext('2d');
  const player = state.objects.find(o => o.type === 'player');
  const playerW = (player?.w || .65) * TILE_W, playerH = (player?.h || .85) * TILE_H;
  const cameraX = player ? state.x + playerW / 2 - canvas.width * .4 : 0;
  const cameraY = player ? state.y + playerH / 2 - (canvas.height + 64) * .52 : 0;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#8bc7df';ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#eff3d8';
  const cloudX = -((cameraX * .18) % (canvas.width + 300));
  for (const x of [cloudX - 160, cloudX + canvas.width - 100]) {
    ctx.beginPath();ctx.ellipse(x + 205, 90 - cameraY * .05, 96, 24, 0, 0, Math.PI * 2);ctx.fill();
    ctx.beginPath();ctx.ellipse(x + 252, 89 - cameraY * .05, 63, 31, 0, 0, Math.PI * 2);ctx.fill();
  }
  const grassY = 590 - cameraY;
  if (grassY < canvas.height) {
    ctx.fillStyle = '#8cb95c';ctx.fillRect(0, Math.max(64, grassY), canvas.width,
      Math.max(0, canvas.height - Math.max(64, grassY)));
    if (grassY >= 64) {ctx.fillStyle = '#222';ctx.fillRect(0, grassY, canvas.width, 3);}
  }
  for (const o of state.objects) {
    if (!o.visible || state.invisible?.includes(o.id) ||
        o.type === 'player' || o.type === 'trigger' || o.type === 'particle') continue;
    const x = o.x * TILE_W - cameraX, y = o.y * TILE_H - cameraY;
    const w = o.w * TILE_W, h = o.h * TILE_H;
    ctx.fillStyle = o.color || DEFAULT_COLORS[o.type];
    const triggerArt = o.type === 'trigger' &&
      (art?.[triggerArtKey(o.trigger?.kind)]?.naturalWidth || art.trigger?.naturalWidth);
    if (triggerArt) {ctx.globalAlpha = .18;ctx.fillRect(x, y, w, h);ctx.globalAlpha = 1;}
    const hasArt = drawWorkshopObject(ctx, art, o, x, y, w, h);
    if (hasArt) continue;
    if (o.type === 'hazard') {
      ctx.beginPath();ctx.moveTo(x + w / 2, y);ctx.lineTo(x + w, y + h);ctx.lineTo(x, y + h);ctx.closePath();ctx.fill();
    } else if (o.type === 'coin') {
      ctx.beginPath();ctx.arc(x + w / 2, y + h / 2, Math.min(w, h) * .42, 0, Math.PI * 2);ctx.fill();
    } else if (o.type === 'goal') {
      ctx.fillRect(x, y, 8, h);ctx.fillRect(x, y, w, 8);ctx.fillRect(x, y + h - 8, w, 8);
    } else if (o.type === 'trigger') {
      ctx.globalAlpha = .35;ctx.fillRect(x, y, w, h);ctx.globalAlpha = 1;
      ctx.strokeStyle = '#fff';ctx.lineWidth = 3;ctx.strokeRect(x + 2, y + 2, w - 4, h - 4);
    } else ctx.fillRect(x, y, w, h);
  }
  drawPreviewParticles(ctx, state, cameraX, cameraY, canvas.width, canvas.height);
  if (player && player.visible !== false && !state.invisible?.includes(player.id)) {
    const x = state.x - cameraX, y = state.y - cameraY;
    const w = playerW, h = playerH;
    if (!drawWorkshopObject(ctx, art, player, x, y, w, h)) {
      ctx.fillStyle = player.color || DEFAULT_COLORS.player;
      ctx.fillRect(x, y, w, h);
      ctx.fillStyle = '#fff';ctx.fillRect(x + w * .2, y + h * .22, 8, 9);ctx.fillRect(x + w * .62, y + h * .22, 8, 9);
      ctx.fillStyle = '#111';ctx.fillRect(x + w * .25, y + h * .24, 3, 5);ctx.fillRect(x + w * .67, y + h * .24, 3, 5);
    }
  }
  ctx.fillStyle = '#14283c';ctx.fillRect(0, 0, canvas.width, 64);
  ctx.fillStyle = '#fff';ctx.font = '22px PTSans, sans-serif';
  ctx.fillText(`Монеты: ${state.coins} · ${control === 'keyboard' ? 'A/D, пробел' : 'экранные кнопки'}`, 24, 41);
  if (state.won) {
    ctx.fillStyle = 'rgba(0,0,0,.62)';ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = '#fff';ctx.font = 'bold 48px PTSans, sans-serif';ctx.textAlign = 'center';
    ctx.fillText('Уровень пройден!', canvas.width / 2, canvas.height / 2);
    ctx.textAlign = 'left';
  }
}
