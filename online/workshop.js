/* Shared web workshop model, PVG3-MAKER codec and small platformer preview.
 * This format is also read by src/online_protocol.c in the native C client. */
export const LEVEL_WIDTH = 16;
export const LEVEL_HEIGHT = 10;
export const TILE_W = 80;
export const TILE_H = 72;
export const MAX_LEVEL_OBJECTS = 120;
export const LEVEL_TYPES = Object.freeze([
  'block', 'ground', 'hazard', 'coin', 'enemy', 'player', 'goal', 'trigger',
]);
export const TYPE_LABELS = Object.freeze({
  block: 'Блок', ground: 'Платформа', hazard: 'Шипы', coin: 'Монета',
  enemy: 'Гусь', player: 'Игрок', goal: 'Финиш', trigger: 'Триггер',
});
const TYPE_SIZES = Object.freeze({
  block: [1, 1], ground: [2, 1], hazard: [1, 1], coin: [.55, .55],
  enemy: [.8, .8], player: [.65, .85], goal: [1, 2], trigger: [1, 1],
});
const DEFAULT_COLORS = Object.freeze({
  block: '#55c8ea', ground: '#65a845', hazard: '#e56c5b', coin: '#ffc54e',
  enemy: '#9560bd', player: '#5ab7e8', goal: '#69d16c', trigger: '#f27652',
});
const TYPE_TO_ID = Object.freeze({
  block: 0, ground: 1, hazard: 2, coin: 3, enemy: 4,
  player: 5, goal: 6, trigger: 7,
});
const TYPE_NAMES = Object.freeze([
  'block', 'ground', 'hazard', 'coin', 'enemy', 'player', 'goal', 'trigger',
]);
const EVENTS = new Set(['touch', 'coin', 'manual']);
const ACTIONS = new Set(['toggle', 'move', 'recolor', 'number']);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const finite = value => typeof value === 'number' && Number.isFinite(value);
const copy = value => JSON.parse(JSON.stringify(value));

function localId() {
  return `draft-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 9)}`;
}
function rgb(value) {
  return typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value);
}
function defaultObject(id, type, x, y, width, height, color, number = 0) {
  return {id, type, name: TYPE_LABELS[type], x, y, w: width, h: height,
    angle: 0, color, number, visible: true, layer: 0, layer2: 0, zOrder: 0};
}

export function newDraft(id = localId()) {
  return {
    localId: id, title: 'Новый уровень', description: '', width: LEVEL_WIDTH,
    height: LEVEL_HEIGHT, objects: [
      defaultObject(1, 'player', 1, 7, .65, .85, DEFAULT_COLORS.player),
      defaultObject(2, 'goal', 14, 6, 1, 2, DEFAULT_COLORS.goal),
      defaultObject(3, 'ground', 0, 8, 16, 2, DEFAULT_COLORS.ground),
    ],
  };
}

export function addObject(level, type, x, y) {
  if (!LEVEL_TYPES.includes(type) || level.objects.length >= MAX_LEVEL_OBJECTS) return null;
  const size = TYPE_SIZES[type];
  const object = defaultObject(
    Math.max(0, ...level.objects.map(o => Number(o.id) || 0)) + 1,
    type, clamp(x, 0, LEVEL_WIDTH - size[0]), clamp(y, 0, LEVEL_HEIGHT - size[1]),
    size[0], size[1], DEFAULT_COLORS[type], 0,
  );
  if (type === 'trigger') {
    const target = level.objects.find(o => o.type === 'goal') || level.objects[0];
    object.trigger = {event: 'touch', action: 'move',
      targetId: target?.id || 0, value: 1, color: '#ffc54e'};
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

export function findObjectAt(level, x, y) {
  return level.objects
    .filter(o => o.visible !== false && x >= o.x && y >= o.y &&
      x < o.x + o.w && y < o.y + o.h)
    .sort((a, b) => (b.layer || 0) - (a.layer || 0) ||
                    (b.zOrder || 0) - (a.zOrder || 0))
    .at(0) || null;
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
    return fail('Уровень должен быть сеткой 16×10 с 1–120 объектами.');
  const ids = new Set();
  let hasPlayer = false, hasGoal = false;
  for (const object of level.objects) {
    if (!object || !LEVEL_TYPES.includes(object.type) ||
        !Number.isInteger(object.id) || object.id < 1 || object.id > 1_000_000 || ids.has(object.id))
      return fail('У объектов должны быть уникальные числовые ID.');
    ids.add(object.id);
    if (typeof object.name !== 'string' || object.name.length > 48 ||
        ![object.x, object.y, object.w, object.h, object.angle].every(finite) ||
        object.x < 0 || object.y < 0 || object.w <= 0 || object.h <= 0 ||
        object.x + object.w > LEVEL_WIDTH + .001 ||
        object.y + object.h > LEVEL_HEIGHT + .001 ||
        object.w > LEVEL_WIDTH || object.h > LEVEL_HEIGHT ||
        object.angle < 0 || object.angle >= 360 || !rgb(object.color) ||
        !Number.isInteger(object.number) || object.number < 0 || object.number > 9999 ||
        typeof object.visible !== 'boolean')
      return fail(`Проверь размеры, цвет и положение объекта «${TYPE_LABELS[object.type]}».`);
    hasPlayer ||= object.type === 'player';
    hasGoal ||= object.type === 'goal';
    if (object.type === 'trigger') {
      const t = object.trigger;
      if (!t || !EVENTS.has(t.event) || !ACTIONS.has(t.action) ||
          !Number.isInteger(t.targetId) || t.targetId < 0 || t.targetId > 1_000_000 ||
          !finite(t.value) || t.value < -100 || t.value > 100 || !rgb(t.color))
        return fail('Настрой триггер: событие, действие, цель и величину.');
    }
  }
  if (!hasPlayer || !hasGoal) return fail('На уровне обязательны игрок и финиш.');
  return {ok: true, message: ''};
}

function recordObject(object) {
  const result = {
    id: object.id, type: object.type, name: object.name || TYPE_LABELS[object.type],
    x: object.x, y: object.y, w: object.w, h: object.h, angle: object.angle || 0,
    color: object.color, number: object.number || 0, visible: object.visible !== false,
    layer: object.layer || 0, layer2: object.layer2 || 0, zOrder: object.zOrder || 0,
  };
  if (object.type === 'trigger') {
    const t = object.trigger || {};
    result.trigger = {event: t.event || 'touch', action: t.action || 'move',
      targetId: t.targetId || 0, value: t.value || 0, color: t.color || '#ffc54e'};
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
  return {
    localId: localId(), title: record.title, description: record.description || '',
    width: LEVEL_WIDTH, height: LEVEL_HEIGHT,
    objects: record.project.objects.map(object => ({...object,
      layer: Number.isInteger(object.layer) ? object.layer : 0,
      layer2: Number.isInteger(object.layer2) ? object.layer2 : 0,
      zOrder: Number.isInteger(object.zOrder) ? object.zOrder : 0,
    })),
  };
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
  if (['buttons', 'joystick', 'keyboard'].includes(preference)) return preference;
  return coarsePointer ? 'buttons' : 'keyboard';
}

export function createPreviewState(level) {
  const player = level.objects.find(o => o.type === 'player');
  if (!player) throw new Error('Уровень без игрока.');
  return {objects: copy(level.objects), x: player.x * TILE_W, y: player.y * TILE_H,
    vx: 0, vy: 0, grounded: false, time: 0, coins: 0, won: false,
    collected: [], triggerFired: [],
    spawn: {x: player.x * TILE_W, y: player.y * TILE_H}};
}
function overlap(ax, ay, aw, ah, bx, by, bw, bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}
function runTriggers(state, event) {
  for (const trigger of state.objects) {
    if (!trigger.visible || trigger.type !== 'trigger' || trigger.trigger?.event !== event ||
        state.triggerFired.includes(trigger.id)) continue;
    const player = state.objects.find(o => o.type === 'player');
    const px = state.x, py = state.y, pw = (player?.w || .65) * TILE_W,
      ph = (player?.h || .85) * TILE_H;
    if (event === 'touch' && !overlap(px, py, pw, ph, trigger.x * TILE_W,
        trigger.y * TILE_H, trigger.w * TILE_W, trigger.h * TILE_H)) continue;
    if (event === 'manual' && Math.hypot(px + pw / 2 - (trigger.x + trigger.w / 2) * TILE_W,
        py + ph / 2 - (trigger.y + trigger.h / 2) * TILE_H) > 150) continue;
    const target = state.objects.find(o => o.id === trigger.trigger.targetId);
    state.triggerFired.push(trigger.id);
    if (!target) continue;
    switch (trigger.trigger.action) {
    case 'toggle': target.visible = !target.visible;break;
    case 'move': target.x = clamp(target.x + trigger.trigger.value, 0, LEVEL_WIDTH - target.w);break;
    case 'recolor': target.color = trigger.trigger.color;break;
    case 'number': target.number = clamp(Math.trunc(trigger.trigger.value), 0, 9999);break;
    }
  }
}
export function stepPreview(state, input = {}, dt = 1 / 60) {
  if (!state || state.won) return state;
  dt = clamp(Number(dt) || 0, 0, .05);
  state.time += dt;
  const player = state.objects.find(o => o.type === 'player');
  if (!player) return state;
  const pw = player.w * TILE_W, ph = player.h * TILE_H;
  const solids = state.objects.filter(o => o.visible && ['block', 'ground'].includes(o.type));
  const oldX = state.x;
  state.vx = (input.axis || 0) * 250;
  state.x = clamp(state.x + state.vx * dt, 0, LEVEL_WIDTH * TILE_W - pw);
  for (const o of solids) {
    const bx = o.x * TILE_W, by = o.y * TILE_H, bw = o.w * TILE_W, bh = o.h * TILE_H;
    if (!overlap(state.x, state.y, pw, ph, bx, by, bw, bh)) continue;
    if (state.vx > 0) state.x = bx - pw;
    else if (state.vx < 0) state.x = bx + bw;
  }
  const oldY = state.y;
  if (input.jump && state.grounded) {state.vy = -570;state.grounded = false;}
  state.vy = Math.min(780, state.vy + 1450 * dt);
  state.y += state.vy * dt;
  state.grounded = false;
  for (const o of solids) {
    const bx = o.x * TILE_W, by = o.y * TILE_H, bw = o.w * TILE_W, bh = o.h * TILE_H;
    if (!overlap(state.x, state.y, pw, ph, bx, by, bw, bh)) continue;
    if (state.vy >= 0 && oldY + ph <= by + 8) {
      state.y = by - ph;state.vy = 0;state.grounded = true;
    } else if (state.vy < 0 && oldY >= by + bh - 5) {
      state.y = by + bh;state.vy = 0;
    }
  }
  if (state.y > LEVEL_HEIGHT * TILE_H + 80) {
    state.x = state.spawn.x;state.y = state.spawn.y;state.vx = state.vy = 0;
  }
  for (const o of state.objects) {
    if (!o.visible || o.type === 'player') continue;
    const bx = o.x * TILE_W, by = o.y * TILE_H, bw = o.w * TILE_W, bh = o.h * TILE_H;
    if (!overlap(state.x, state.y, pw, ph, bx, by, bw, bh)) continue;
    if (o.type === 'coin' && !state.collected.includes(o.id)) {
      state.collected.push(o.id);state.coins++;o.visible = false;runTriggers(state, 'coin');
    } else if (o.type === 'hazard' || o.type === 'enemy') {
      state.x = state.spawn.x;state.y = state.spawn.y;state.vx = state.vy = 0;
    } else if (o.type === 'goal') state.won = true;
    else if (o.type === 'trigger') runTriggers(state, 'touch');
  }
  if (input.trigger) runTriggers(state, 'manual');
  if (state.x !== oldX) runTriggers(state, 'touch');
  return state;
}

export function drawEditorCanvas(canvas, level, selectedId = 0, tool = 'build') {
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#14283c';ctx.fillRect(0, 0, canvas.width, canvas.height);
  for (let c = 0; c <= LEVEL_WIDTH; c++) {
    ctx.strokeStyle = '#2a4056';ctx.lineWidth = 1;
    ctx.beginPath();ctx.moveTo(c * TILE_W, 0);ctx.lineTo(c * TILE_W, canvas.height);ctx.stroke();
  }
  for (let r = 0; r <= LEVEL_HEIGHT; r++) {
    ctx.beginPath();ctx.moveTo(0, r * TILE_H);ctx.lineTo(canvas.width, r * TILE_H);ctx.stroke();
  }
  const drawObjects = [...level.objects].sort((a, b) => (a.layer || 0) - (b.layer || 0) ||
      (a.zOrder || 0) - (b.zOrder || 0));
  for (const o of drawObjects) {
    if (o.visible === false) continue;
    const x = o.x * TILE_W + 3, y = o.y * TILE_H + 3;
    const w = o.w * TILE_W - 6, h = o.h * TILE_H - 6;
    ctx.fillStyle = o.color || DEFAULT_COLORS[o.type];
    if (o.type === 'coin') {
      ctx.beginPath();ctx.arc(x + w / 2, y + h / 2, Math.min(w, h) / 2, 0, Math.PI * 2);ctx.fill();
    } else if (o.type === 'hazard') {
      ctx.beginPath();ctx.moveTo(x + w / 2, y);ctx.lineTo(x + w, y + h);ctx.lineTo(x, y + h);ctx.closePath();ctx.fill();
    } else {
      ctx.fillRect(x, y, w, h);
      if (o.type === 'trigger') {ctx.strokeStyle = '#fff2d8';ctx.lineWidth = 3;ctx.strokeRect(x + 2, y + 2, w - 4, h - 4);}
      if (o.type === 'goal') {ctx.fillStyle = '#fff2d8';ctx.fillRect(x + w * .65, y, 4, h);}
      if (o.type === 'player') {
        ctx.fillStyle = '#fff';ctx.fillRect(x + w * .18, y + h * .25, w * .18, h * .12);
        ctx.fillRect(x + w * .58, y + h * .25, w * .18, h * .12);
      }
    }
    if (o.id === selectedId) {ctx.strokeStyle = '#27d2d6';ctx.lineWidth = 4;ctx.strokeRect(x - 1, y - 1, w + 2, h + 2);}
    ctx.fillStyle = '#10131d';ctx.font = '14px PTSans, sans-serif';ctx.fillText(String(o.number || ''), x + 4, y + 17);
  }
  ctx.fillStyle = 'rgba(0,0,0,.55)';ctx.fillRect(8, 8, 266, 30);
  ctx.fillStyle = '#fff2d8';ctx.font = '16px PTSans, sans-serif';
  ctx.fillText(tool === 'delete' ? 'Выбери объект для удаления' : 'Сетка 16 × 10 · касание по клетке', 18, 29);
}

export function drawPreviewCanvas(canvas, state, control = 'keyboard') {
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#8bc7df';ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#eff3d8';
  ctx.beginPath();ctx.ellipse(205, 90, 96, 24, 0, 0, Math.PI * 2);ctx.fill();
  ctx.beginPath();ctx.ellipse(252, 89, 63, 31, 0, 0, Math.PI * 2);ctx.fill();
  ctx.fillStyle = '#8cb95c';ctx.fillRect(0, 8 * TILE_H, canvas.width, 2 * TILE_H);
  ctx.fillStyle = '#222';ctx.fillRect(0, 8 * TILE_H, canvas.width, 3);
  for (const o of state.objects) {
    if (!o.visible || o.type === 'player') continue;
    const x = o.x * TILE_W, y = o.y * TILE_H, w = o.w * TILE_W, h = o.h * TILE_H;
    ctx.fillStyle = o.color || DEFAULT_COLORS[o.type];
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
  const player = state.objects.find(o => o.type === 'player');
  if (player) {
    const x = state.x, y = state.y, w = player.w * TILE_W, h = player.h * TILE_H;
    ctx.fillStyle = player.color || DEFAULT_COLORS.player;
    ctx.fillRect(x, y, w, h);
    ctx.fillStyle = '#fff';ctx.fillRect(x + w * .2, y + h * .22, 8, 9);ctx.fillRect(x + w * .62, y + h * .22, 8, 9);
    ctx.fillStyle = '#111';ctx.fillRect(x + w * .25, y + h * .24, 3, 5);ctx.fillRect(x + w * .67, y + h * .24, 3, 5);
  }
  ctx.fillStyle = '#14283c';ctx.fillRect(0, 0, canvas.width, 64);
  ctx.fillStyle = '#fff';ctx.font = '22px PTSans, sans-serif';
  ctx.fillText(`Монеты: ${state.coins} · ${control === 'keyboard' ? 'A/D или ←/→, пробел' : 'экранные кнопки'}`, 24, 41);
  if (state.won) {
    ctx.fillStyle = 'rgba(0,0,0,.62)';ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = '#fff';ctx.font = 'bold 48px PTSans, sans-serif';ctx.textAlign = 'center';
    ctx.fillText('Уровень пройден!', canvas.width / 2, canvas.height / 2);
    ctx.textAlign = 'left';
  }
}
