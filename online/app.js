import {PLANTS, DUCKS, W, H, X, Y, CW, CH, ROWS, COLS,
        newMatch, validMatch, applyCommand, stepMatch} from './rules.js';
import {DATABASE, validId, randomPlayerId, listRooms, getRoom, createRoom,
        joinRoom, chooseRole, writeState, writeCommand, heartbeat, leaveRoom,
        listPublishedLevels, getPublishedLevel, publishLevel} from './firebase.js';
import {preloadArtwork, drawGame} from './draw.js';
import {LEVEL_WIDTH, LEVEL_HEIGHT, MAX_LEVEL_OBJECTS, WORLD_LIMIT,
        MIN_OBJECT_SIZE, MAX_OBJECT_WIDTH, MAX_OBJECT_HEIGHT, TYPE_LABELS,
        TRIGGER_KINDS, TRIGGER_LABELS, newDraft, setTriggerKind,
        addObject, findObjectAt, validateDraft, draftFromPublished,
        moveObjects, resizeObjects, rotateObjects, flipObjects, panCamera, copyObjects, pasteObjects,
        createPreviewState, stepPreview, drawEditorCanvas, drawPreviewCanvas,
        resolveControlMode, createTouchButtonState} from './workshop.js';

const $ = id => document.getElementById(id);
const canvas = $('battle');
const sections = {rooms: $('rooms-screen'), lobby: $('lobby-screen'), match: $('match-screen'),
  workshop: $('workshop-screen')};
let screen = 'rooms', roomId = '', slot = '', room = null, state = null;
let chosenPlant = -1, chosenDuck = -1, mapChoice = 1, pendingSeq = 0;
let seq = 0, lastSnapshot = 0, lastPublish = 0, writing = false;
let lastFrame = performance.now(), lastPing = 0, generation = 0;
let toastTimer;
const WS_DRAFT_KEY = 'pvg3-workshop-draft-v1';
const WS_CONTROL_KEY = 'pvg3-workshop-control-v1';
const wsPages = {home: $('ws-home-page'), editor: $('ws-editor-page'),
  preview: $('ws-preview-page'), catalog: $('ws-catalog-page')};
let wsPage = 'home', wsDraft = loadWorkshopDraft(), wsTool = 'build', wsType = 'block';
let wsTriggerKind = 'move', wsBlockType = 'block', wsPaletteSelected = true;
let wsSelectedId = 0, wsSelectedIds = new Set(), wsClipboard = [];
let wsDrag = null, wsPanDrag = null, wsCatalogGeneration = 0, wsCatalog = [];
let wsCamera = {x: 0, y: 0};
let wsPreviewState = null, wsPreviewLevel = null, wsPreviewReturn = 'editor';
let wsControlPreference = localSetting(WS_CONTROL_KEY, 'auto'), wsControlMode = 'keyboard';
let wsJumpQueued = false, wsTriggerQueued = false;
const wsTouchButtons = createTouchButtonState();
const wsKeys = new Set();
let wsWinAnnounced = false;
const WS_ART_FILES = {
  block: 'Блок.png', ground: 'Платформа.png', hazard: 'Шип.png', slope: 'Склон.png',
  coin: 'coin-token.png', enemy: 'zombie-duck.png', player: 'khlebushek.png',
  goal: 'Флажок - финиш.png',
  triggerMove: 'Триггер-движения.png', triggerRotate: 'Триггер-вращения.png',
  triggerForever: 'Триггер-вечно.png',
  triggerInvisibility: 'Триггер-невидимости.png',
  triggerNoCollision: 'Триггер-нет столкновения.png',
};
const wsArt = Object.fromEntries(Object.entries(WS_ART_FILES).map(([type, file]) => {
  const image = new Image();
  image.decoding = 'async';
  image.addEventListener('load', () => {
    if (screen !== 'workshop') return;
    if (wsPage === 'editor') {
      renderPaletteOptions();
      drawEditorCanvas($('ws-editor-canvas'), wsDraft, wsSelectedIds, wsTool, wsArt, wsCamera);
    }
    else if (wsPage === 'preview') drawCurrentPreview();
  }, {once: true});
  image.src = new URL(`../assets/art/${file}`, import.meta.url).href;
  return [type, image];
}));
// A browser tab keeps its seat after reload, while a second tab is a second
// player. A shared localStorage ID would incorrectly identify both as one.
const savedId = sessionStorage.getItem('pvg3-online-player');
const playerId = savedId && /^[0-9a-f]{32}$/.test(savedId) ? savedId : randomPlayerId();
sessionStorage.setItem('pvg3-online-player', playerId);

function show(name) {
  screen = name;
  for (const [key, el] of Object.entries(sections)) el.classList.toggle('hidden', key !== name);
}
function localSetting(key, fallback) {
  try {return localStorage.getItem(key) ?? fallback;}
  catch {return fallback;}
}
function loadWorkshopDraft() {
  try {
    const saved = JSON.parse(localStorage.getItem(WS_DRAFT_KEY) || 'null');
    const validShape = saved && saved.width === LEVEL_WIDTH && saved.height === LEVEL_HEIGHT &&
      typeof saved.title === 'string' && saved.title.length <= 80 &&
      typeof saved.description === 'string' && saved.description.length <= 160 &&
      Array.isArray(saved.objects) && saved.objects.length <= MAX_LEVEL_OBJECTS &&
      saved.objects.every(o => o && Object.hasOwn(TYPE_LABELS, o.type) && Number.isInteger(o.id) &&
        Number.isFinite(o.x) && Number.isFinite(o.y) && Number.isFinite(o.w) && Number.isFinite(o.h) &&
        typeof o.color === 'string' && /^#[0-9a-f]{6}$/i.test(o.color));
    if (validShape) {
      for (const object of saved.objects) {
        object.flipX = object.flipX === true;object.flipY = object.flipY === true;
        if (object.type !== 'trigger') continue;
        object.trigger ||= {event: 'touch', action: 'move', targetId: 0, value: 1, color: '#ffc54e'};
        object.trigger.kind ||= 'move';
        if (object.trigger.kind === 'rotate' && object.trigger.duration === undefined) {
          const target = saved.objects.find(candidate => candidate.id === object.trigger.targetId);
          if (!Number.isInteger(object.trigger.groupId))
            object.trigger.groupId = Number.isInteger(target?.number) ? target.number : 0;
          object.trigger.duration = 3;
          object.trigger.action = 'rotate';
          delete object.trigger.degrees;delete object.trigger.value;
        }
      }
      return saved;
    }
  } catch {}
  return newDraft();
}
function setWorkshopPage(page) {
  if (!wsPages[page]) return;
  wsPage = page;
  show('workshop');
  for (const [key, element] of Object.entries(wsPages))
    element.classList.toggle('hidden', key !== page);
  if (page === 'home') renderWorkshopHome();
  if (page === 'editor') renderWorkshopEditor();
  if (page === 'preview') drawCurrentPreview();
}
function renderWorkshopHome() {
  $('ws-draft-title').textContent = wsDraft.title.trim() || 'Новый уровень';
  const check = validateDraft(wsDraft);
  $('ws-draft-summary').textContent = `${wsDraft.objects.length} объектов · бесконечная карта · ` +
    (check.ok ? 'можно проверять и публиковать.' : check.message);
  $('ws-draft-status').textContent = wsDraft.publishedId ?
    `Последняя публикация: ID ${wsDraft.publishedId}. Повторная публикация создаст новую запись.` :
    'Черновик хранится только в этом браузере.';
}
function saveWorkshopDraft(message = 'Черновик сохранён на этом устройстве.') {
  try {
    localStorage.setItem(WS_DRAFT_KEY, JSON.stringify(wsDraft));
    $('ws-autosave-status').textContent = message;
  } catch {
    $('ws-autosave-status').textContent = 'Не удалось сохранить черновик в браузере.';
  }
  $('ws-object-count').textContent = `${wsDraft.objects.length} / ${MAX_LEVEL_OBJECTS} объектов`;
  renderWorkshopHome();
}
function showWorkshopMessage(message = '') {
  const node = $('ws-editor-message');
  node.textContent = message;
  node.classList.toggle('hidden', !message);
}
function renderPaletteOptions() {
  const container = $('ws-palette-items');
  if (!container) return;
  for (const button of document.querySelectorAll('[data-ws-type]'))
    button.classList.toggle('active', button.dataset.wsType === wsType);
  container.replaceChildren();
  const options = wsType === 'block' ? [
    {kind: 'block', label: TYPE_LABELS.block, art: 'block'},
    {kind: 'slope', label: TYPE_LABELS.slope, art: 'slope'},
    {kind: 'ground', label: TYPE_LABELS.ground, art: 'ground'},
  ] : wsType === 'trigger' ? [
    {kind: 'move', label: TRIGGER_LABELS.move, art: 'triggerMove'},
    {kind: 'rotate', label: TRIGGER_LABELS.rotate, art: 'triggerRotate'},
    {kind: 'forever', label: TRIGGER_LABELS.forever, art: 'triggerForever'},
    {kind: 'invisibility', label: TRIGGER_LABELS.invisibility, art: 'triggerInvisibility'},
    {kind: 'no-collision', label: TRIGGER_LABELS['no-collision'], art: 'triggerNoCollision'},
  ] : [{kind: 'single', label: TYPE_LABELS[wsType], art: wsType}];
  for (const option of options) {
    const button = document.createElement('button');
    button.type = 'button';button.className = 'ws-palette-item';
    const active = wsPaletteSelected &&
      (wsType === 'trigger' ? wsTriggerKind === option.kind :
        wsType === 'block' ? wsBlockType === option.kind : true);
    if (active) button.classList.add('active');
    const icon = document.createElement('img');
    icon.alt = '';icon.setAttribute('aria-hidden', 'true');
    icon.src = wsArt[option.art]?.src || '';
    const label = document.createElement('span');label.textContent = option.label;
    button.append(icon, label);
    button.addEventListener('click', () => {
      if (wsType === 'trigger') wsTriggerKind = option.kind;
      else if (wsType === 'block') wsBlockType = option.kind;
      wsPaletteSelected = true;
      wsTool = 'build';
      renderWorkshopEditor();
    });
    container.append(button);
  }
}
function renderWorkshopEditor() {
  $('ws-title-input').value = wsDraft.title;
  $('ws-description-input').value = wsDraft.description;
  $('ws-object-count').textContent = `${wsDraft.objects.length} / ${MAX_LEVEL_OBJECTS} объектов`;
  for (const button of document.querySelectorAll('[data-ws-tool]'))
    button.classList.toggle('active', button.dataset.wsTool === wsTool);
  renderPaletteOptions();
  renderSelectedObject();
  drawEditorCanvas($('ws-editor-canvas'), wsDraft, wsSelectedIds, wsTool, wsArt, wsCamera);
}
function notice(text, duration = 4400) {
  $('toast').textContent = text;
  $('toast').classList.remove('hidden');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $('toast').classList.add('hidden'), duration);
}
function connection(ok, text) {
  $('connection').classList.toggle('ready', ok === true);
  $('connection').classList.toggle('error', ok === false);
  $('connection').lastChild.textContent = text;
}
function session() {
  if (roomId) sessionStorage.setItem('pvg3-room', JSON.stringify({id: roomId, slot}));
  else sessionStorage.removeItem('pvg3-room');
}
function clearInvite() {
  if (new URLSearchParams(location.search).has('room'))
    history.replaceState(null, '', location.pathname);
}
function wsObject(id) {return wsDraft.objects.find(object => object.id === id) || null;}
function setWorkshopSelection(ids, primary = 0) {
  const valid = new Set((ids || []).filter(id => wsObject(id)));
  wsSelectedIds = valid;
  wsSelectedId = valid.has(primary) ? primary : valid.values().next().value || 0;
}
function toggleWorkshopSelection(id) {
  if (!wsObject(id)) return;
  if (wsSelectedIds.has(id)) {
    wsSelectedIds.delete(id);
    if (wsSelectedId === id) wsSelectedId = wsSelectedIds.values().next().value || 0;
  } else {
    wsSelectedIds.add(id);wsSelectedId = id;
  }
}
function renderSelectedObject() {
  const object = wsObject(wsSelectedId);
  const selected = [...wsSelectedIds].map(wsObject).filter(Boolean);
  const panel = $('ws-properties');
  panel.classList.toggle('hidden', !selected.length);
  $('ws-selection-count').textContent = `${selected.length} выбрано`;
  $('ws-single-properties').classList.toggle('hidden', selected.length !== 1);
  const canCopy = selected.some(item => item.type !== 'player' && item.type !== 'goal');
  $('ws-copy-selected').disabled = !canCopy;
  $('ws-paste-selected').disabled = !wsClipboard.length ||
    wsDraft.objects.length + wsClipboard.length > MAX_LEVEL_OBJECTS;
  $('ws-delete-selected').disabled = !selected.length;
  for (const button of document.querySelectorAll('[data-ws-nudge], [data-ws-scale], [data-ws-rotate], [data-ws-flip]'))
    button.disabled = !selected.length;
  if (!selected.length) return;
  if (selected.length > 1) {
    $('ws-selected-label').textContent = `${selected.length} объектов`;
    return;
  }
  if (!object) return;
  const triggerLabel = object.type === 'trigger' ? ` · ${TRIGGER_LABELS[object.trigger?.kind || 'move']}` : '';
  $('ws-selected-label').textContent = `${TYPE_LABELS[object.type]}${triggerLabel} · ID ${object.id}`;
  $('ws-object-x').value = Number(object.x.toFixed(2));
  $('ws-object-y').value = Number(object.y.toFixed(2));
  $('ws-object-width').value = Number(object.w.toFixed(2));
  $('ws-object-height').value = Number(object.h.toFixed(2));
  $('ws-object-angle').value = Number((object.angle || 0).toFixed(1));
  $('ws-object-number').value = object.number || 0;
  $('ws-object-color').value = object.color;
  const triggerFields = $('ws-trigger-fields');
  triggerFields.classList.toggle('hidden', object.type !== 'trigger');
  if (object.type !== 'trigger') return;
  const t = object.trigger || {};
  const kind = TRIGGER_KINDS.includes(t.kind) ? t.kind : 'move';
  $('ws-trigger-kind').value = kind;
  const forever = kind === 'forever';
  const rotate = kind === 'rotate';
  const moving = kind === 'move';
  const legacyTarget = wsDraft.objects.find(candidate => candidate.id === t.targetId);
  const groupId = Number.isInteger(t.groupId) ? t.groupId :
    Number.isInteger(legacyTarget?.number) ? legacyTarget.number : 0;
  $('ws-trigger-event').value = t.event || 'touch';
  $('ws-trigger-motion-fields').classList.toggle('hidden', forever);
  $('ws-trigger-move-fields').classList.toggle('hidden', !moving);
  $('ws-trigger-rotate-field').classList.toggle('hidden', !rotate);
  $('ws-trigger-forever-fields').classList.toggle('hidden', !forever);
  $('ws-trigger-group').value = groupId;
  $('ws-trigger-forever-group').value = groupId;
  $('ws-trigger-action').value = ['activate', 'unactivate'].includes(t.action) ? t.action : 'activate';
  $('ws-trigger-x').value = t.valueX ?? t.value ?? 0;
  $('ws-trigger-y').value = t.valueY ?? 0;
  $('ws-trigger-duration').value = t.duration ?? 3;
}
function wsRedrawEditor() {
  renderSelectedObject();
  drawEditorCanvas($('ws-editor-canvas'), wsDraft, wsSelectedIds, wsTool, wsArt, wsCamera);
  $('ws-object-count').textContent = `${wsDraft.objects.length} / ${MAX_LEVEL_OBJECTS} объектов`;
}
function setWorkshopTool(tool) {
  if (!['build', 'select', 'multi', 'delete', 'pan'].includes(tool)) return;
  wsTool = tool;
  for (const button of document.querySelectorAll('[data-ws-tool]'))
    button.classList.toggle('active', button.dataset.wsTool === tool);
  wsRedrawEditor();
}
function wsPoint(event) {
  const rect = $('ws-editor-canvas').getBoundingClientRect();
  return {x: wsCamera.x + (event.clientX - rect.left) * LEVEL_WIDTH / rect.width,
    y: wsCamera.y + (event.clientY - rect.top) * LEVEL_HEIGHT / rect.height};
}
function wsPointerDown(event) {
  if (screen !== 'workshop' || wsPage !== 'editor') return;
  event.preventDefault();
  const canvas = $('ws-editor-canvas');
  if (wsTool === 'pan') {
    wsPanDrag = {pointerId: event.pointerId, startX: event.clientX, startY: event.clientY,
      x: wsCamera.x, y: wsCamera.y};
    canvas.setPointerCapture?.(event.pointerId);
    return;
  }
  const point = wsPoint(event);
  const object = findObjectAt(wsDraft, point.x, point.y);
  showWorkshopMessage('');
  if (wsTool === 'build') {
    if (!wsPaletteSelected) {
      showWorkshopMessage('Сначала выбери объект в выбранной категории.');return;
    }
    const objectType = wsType === 'block' ? wsBlockType : wsType;
    const placed = addObject(wsDraft, objectType, Math.floor(point.x), Math.floor(point.y), wsTriggerKind);
    if (!placed) {
      showWorkshopMessage('Достигнут лимит 120 объектов. Удали лишние объекты перед добавлением новых.');return;
    }
    // Player and finish are singletons: addObject moves the existing object.
    setWorkshopSelection([placed.id], placed.id);
    saveWorkshopDraft();wsRedrawEditor();return;
  }
  if (wsTool === 'multi' || event.shiftKey) {
    if (object) toggleWorkshopSelection(object.id);
    wsRedrawEditor();return;
  }
  if (wsTool === 'delete') {
    if (object && ['player', 'goal'].includes(object.type)) {
      showWorkshopMessage('Игрок и финиш обязательны и не удаляются.');
    } else if (object) {
      wsDraft.objects = wsDraft.objects.filter(o => o.id !== object.id);
      setWorkshopSelection([...wsSelectedIds].filter(id => id !== object.id));
      saveWorkshopDraft();
    }
    wsRedrawEditor();return;
  }
  if (!object) {
    setWorkshopSelection([]);wsRedrawEditor();return;
  }
  if (!wsSelectedIds.has(object.id)) setWorkshopSelection([object.id], object.id);
  const positions = new Map([...wsSelectedIds].map(id => {
    const selected = wsObject(id);return [id, {x: selected.x, y: selected.y}];
  }));
  wsDrag = {pointerId: event.pointerId, ids: [...wsSelectedIds], positions,
    startX: event.clientX, startY: event.clientY, moved: false};
  canvas.setPointerCapture?.(event.pointerId);
  wsRedrawEditor();
}
function wsPointerMove(event) {
  const rect = $('ws-editor-canvas').getBoundingClientRect();
  if (wsPanDrag && event.pointerId === wsPanDrag.pointerId) {
    const dx = (event.clientX - wsPanDrag.startX) * LEVEL_WIDTH / rect.width;
    const dy = (event.clientY - wsPanDrag.startY) * LEVEL_HEIGHT / rect.height;
    wsCamera.x = Math.max(-WORLD_LIMIT, Math.min(WORLD_LIMIT - LEVEL_WIDTH, wsPanDrag.x - dx));
    wsCamera.y = Math.max(-WORLD_LIMIT, Math.min(WORLD_LIMIT - LEVEL_HEIGHT, wsPanDrag.y - dy));
    wsRedrawEditor();return;
  }
  if (!wsDrag || event.pointerId !== wsDrag.pointerId) return;
  const dx = (event.clientX - wsDrag.startX) * LEVEL_WIDTH / rect.width;
  const dy = (event.clientY - wsDrag.startY) * LEVEL_HEIGHT / rect.height;
  if (Math.abs(dx) + Math.abs(dy) > .025) wsDrag.moved = true;
  for (const id of wsDrag.ids) {
    const object = wsObject(id), position = wsDrag.positions.get(id);
    if (!object || !position) continue;
    object.x = Math.max(-WORLD_LIMIT, Math.min(WORLD_LIMIT - object.w, position.x + dx));
    object.y = Math.max(-WORLD_LIMIT, Math.min(WORLD_LIMIT - object.h, position.y + dy));
  }
  wsRedrawEditor();
}
function wsPointerUp(event) {
  if (wsPanDrag && (!event || event.pointerId === wsPanDrag.pointerId)) {
    wsPanDrag = null;return;
  }
  if (!wsDrag || (event && event.pointerId !== wsDrag.pointerId)) return;
  const moved = wsDrag.moved;wsDrag = null;
  if (moved) {saveWorkshopDraft();wsRedrawEditor();}
}
function updateSelectedProperty(property, value) {
  const object = wsObject(wsSelectedId);
  if (!object || wsSelectedIds.size !== 1) return;
  const n = Number(value);
  if (property !== 'color' && !Number.isFinite(n)) return;
  if (property === 'x' || property === 'y') {
    object[property] = Math.max(-WORLD_LIMIT,
      Math.min(WORLD_LIMIT - object[property === 'x' ? 'w' : 'h'], n));
  } else if (property === 'width' || property === 'height') {
    const size = Math.max(MIN_OBJECT_SIZE,
      Math.min(property === 'width' ? MAX_OBJECT_WIDTH : MAX_OBJECT_HEIGHT, n));
    const sizeKey = property === 'width' ? 'w' : 'h';
    const positionKey = property === 'width' ? 'x' : 'y';
    object[sizeKey] = size;
    object[positionKey] = Math.max(-WORLD_LIMIT, Math.min(WORLD_LIMIT - size, object[positionKey]));
  } else if (property === 'angle') {
    object.angle = ((n % 360) + 360) % 360;
  } else if (property === 'number') {
    object.number = Math.max(0, Math.min(9999, Math.trunc(n)));
  } else if (property === 'color') object.color = value;
  wsRedrawEditor();saveWorkshopDraft();
}
function wsStep(id, fallback) {
  const value = Number($(id).value);
  return Number.isFinite(value) && value > 0 ? value : fallback;
}
function wsPan(direction) {
  const step = wsStep('ws-pan-step', 1);
  const offsets = {left: [-step, 0], right: [step, 0], up: [0, -step], down: [0, step]};
  const [dx, dy] = offsets[direction] || [0, 0];
  wsCamera = panCamera(wsCamera, dx, dy);
  wsRedrawEditor();
}
function wsNudge(direction, multiplier = 1) {
  const step = wsStep('ws-move-step', .5) * multiplier;
  const offsets = {left: [-step, 0], right: [step, 0], up: [0, -step], down: [0, step]};
  const [dx, dy] = offsets[direction] || [0, 0];
  if (!moveObjects(wsDraft, wsSelectedIds, dx, dy)) return;
  saveWorkshopDraft();wsRedrawEditor();
}
function wsScale(axis, sign) {
  const step = wsStep('ws-scale-step', .1) * sign;
  if (!resizeObjects(wsDraft, wsSelectedIds, axis, step)) return;
  saveWorkshopDraft();wsRedrawEditor();
}
function wsRotate(sign) {
  const step = wsStep('ws-rotate-step', 45) * sign;
  if (!rotateObjects(wsDraft, wsSelectedIds, step)) return;
  saveWorkshopDraft();wsRedrawEditor();
}
function wsFlip(axis) {
  if (!flipObjects(wsDraft, wsSelectedIds, axis)) return;
  saveWorkshopDraft();wsRedrawEditor();
}
function wsUpdateRotateLabels() {
  const step = Math.round(wsStep('ws-rotate-step', 45) * 10) / 10;
  for (const button of document.querySelectorAll('[data-ws-rotate]')) {
    const sign = Number(button.dataset.wsRotate) < 0 ? '-' : '+';
    const label = button.querySelector('[data-ws-rotate-label]');
    if (label) label.textContent = `${sign}${step}°`;
  }
}
function wsCopySelection() {
  wsClipboard = copyObjects(wsDraft, wsSelectedIds);
  showWorkshopMessage(wsClipboard.length ? `Скопировано объектов: ${wsClipboard.length}.` :
    'Игрок и финиш уникальны. Выбери обычный объект для копирования.');
  wsRedrawEditor();
}
function wsPasteSelection() {
  const pasted = pasteObjects(wsDraft, wsClipboard, 1, 1);
  if (!pasted.length) {
    showWorkshopMessage(wsClipboard.length ? 'Нет места для вставки: достигнут лимит объектов.' :
      'Сначала скопируй выбранные объекты.');
    wsRedrawEditor();return;
  }
  setWorkshopSelection(pasted.map(object => object.id), pasted.at(-1).id);
  showWorkshopMessage(`Вставлено объектов: ${pasted.length}.`);
  saveWorkshopDraft();wsRedrawEditor();
}
function wsDeleteSelection() {
  if (!wsSelectedIds.size) return;
  const removed = new Set([...wsSelectedIds].filter(id =>
    !['player', 'goal'].includes(wsObject(id)?.type)));
  if (!removed.size) {
    showWorkshopMessage('Игрок и финиш обязательны и не удаляются.');
    return;
  }
  wsDraft.objects = wsDraft.objects.filter(object => !removed.has(object.id));
  setWorkshopSelection([...wsSelectedIds].filter(id => !removed.has(id)), wsSelectedId);
  saveWorkshopDraft();wsRedrawEditor();
}
function updateSelectedTrigger(property, value) {
  const object = wsObject(wsSelectedId);
  if (!object || object.type !== 'trigger') return;
  const t = object.trigger ||= {kind: 'move', event: 'touch', action: 'move'};
  if (property === 'kind') {
    if (!setTriggerKind(wsDraft, object.id, value)) return;
  } else if (property === 'event') {
    t.event = value;
  } else if (property === 'groupId') {
    const n = Number(value);if (!Number.isFinite(n)) return;
    t.groupId = Math.max(0, Math.min(9999, Math.trunc(n)));
    if (t.kind === 'forever' && !['activate', 'unactivate'].includes(t.action))
      t.action = 'activate';
  } else if (['valueX', 'valueY'].includes(property)) {
    const n = Number(value);if (!Number.isFinite(n)) return;
    t[property] = Math.max(-9999, Math.min(9999, Math.trunc(n)));
  } else if (property === 'duration') {
    const n = Number(value);if (!Number.isFinite(n)) return;
    t.duration = Math.max(1, Math.min(9999, Math.trunc(n)));
    t.action = 'rotate';
    delete t.degrees;delete t.value;
  } else if (property === 'action') {
    if (!['activate', 'unactivate'].includes(value)) return;
    t.action = value;
  }
  wsRedrawEditor();saveWorkshopDraft();
}
function beginNewDraft() {
  if (!confirm('Создать новый уровень вместо текущего черновика? Опубликованные уровни не затрагиваются.')) return;
  wsDraft = newDraft();setWorkshopSelection([]);wsClipboard = [];wsTool = 'build';wsType = 'block';
  wsTriggerKind = 'move';wsPaletteSelected = true;wsCamera = {x: 0, y: 0};
  saveWorkshopDraft('Создан новый черновик.');setWorkshopPage('editor');
}
function setCatalogMessage(message = '') {
  const node = $('ws-catalog-message');node.textContent = message;
  node.classList.toggle('hidden', !message);
}
function renderWorkshopCatalog(levels) {
  const list = $('ws-catalog-list');list.replaceChildren();
  if (!levels.length) {
    const empty = document.createElement('p');empty.className = 'muted';
    empty.textContent = 'В каталоге пока нет опубликованных уровней. Создай свой и нажми «Опубликовать».';
    list.append(empty);return;
  }
  for (const level of levels) {
    const card = document.createElement('article');card.className = 'ws-level-card';
    const content = document.createElement('div');
    const id = document.createElement('small');id.className = 'ws-level-id';id.textContent = `ID ${level.id}`;
    const title = document.createElement('h3');title.textContent = level.title;
    const description = document.createElement('p');description.textContent = level.description || 'Авторский платформерный уровень.';
    content.append(id, title, description);
    const button = document.createElement('button');button.type = 'button';button.textContent = 'Играть';
    button.addEventListener('click', () => playPublishedLevel(level.id, button));
    card.append(content, button);list.append(card);
  }
}
async function loadWorkshopCatalog() {
  const generationId = ++wsCatalogGeneration;
  setCatalogMessage('');
  const list = $('ws-catalog-list');list.replaceChildren();
  const loading = document.createElement('p');loading.className = 'muted';loading.textContent = 'Загружаем каталог…';list.append(loading);
  try {
    const levels = await listPublishedLevels();
    if (generationId !== wsCatalogGeneration || wsPage !== 'catalog') return;
    wsCatalog = levels;renderWorkshopCatalog(levels);
  } catch (error) {
    if (generationId !== wsCatalogGeneration || wsPage !== 'catalog') return;
    list.replaceChildren();
    const message = error.status === 401 || error.status === 403 ?
      'Firebase запретил чтение /levels-index. Для каталога в правилах базы нужно разрешить чтение этой ветки.' :
      `Не удалось загрузить каталог: ${error.message}`;
    setCatalogMessage(message);
  }
}
async function playPublishedLevel(id, button) {
  button.disabled = true;setCatalogMessage('');
  try {
    const record = await getPublishedLevel(id);
    const level = draftFromPublished(record);
    startWorkshopPreview(level, record.title, 'catalog');
  } catch (error) {
    setCatalogMessage(error.status === 401 || error.status === 403 ?
      'Firebase запретил чтение /levels. Проверь правила базы.' : error.message);
  } finally {button.disabled = false;}
}
function startWorkshopPreview(level, title, returnPage) {
  const check = validateDraft(level);
  if (!check.ok) {showWorkshopMessage(check.message);return;}
  wsPreviewLevel = level;wsPreviewState = createPreviewState(level);
  wsPreviewReturn = returnPage;wsWinAnnounced = false;
  $('ws-preview-title').textContent = title || level.title;
  $('ws-preview-status').textContent = 'Доберись до финиша и попробуй собрать монеты.';
  $('ws-preview-back').textContent = returnPage === 'catalog' ? 'Назад · Каталог' : 'Назад · В редактор';
  clearWorkshopInput();applyWorkshopControl(wsControlPreference);
  setWorkshopPage('preview');
}
function drawCurrentPreview() {
  if (wsPreviewState) drawPreviewCanvas($('ws-preview-canvas'), wsPreviewState, wsControlMode, wsArt);
}
function applyWorkshopControl(preference) {
  wsControlPreference = ['auto', 'buttons', 'keyboard'].includes(preference) ? preference : 'auto';
  try {localStorage.setItem(WS_CONTROL_KEY, wsControlPreference);} catch {}
  const coarsePointer = !!(window.matchMedia && window.matchMedia('(pointer: coarse)').matches);
  wsControlMode = resolveControlMode(wsControlPreference === 'auto' ? '' : wsControlPreference, coarsePointer);
  $('ws-control-select').value = wsControlPreference;
  $('ws-buttons-controls').classList.toggle('hidden', wsControlMode !== 'buttons');
  $('ws-control-help').textContent = wsControlMode === 'keyboard' ?
    'Клавиши A / D для движения, пробел или W для прыжка, E для действия.' :
    'Удерживай «Назад» или «Вперёд». Одновременно нажми «Прыжок»; «Действие» запускает ручные триггеры.';
  clearWorkshopInput();drawCurrentPreview();
}
function clearWorkshopInput() {
  wsTouchButtons.clear();
  wsJumpQueued = wsTriggerQueued = false;wsKeys.clear();
}
function resetWorkshopPreview() {
  if (!wsPreviewLevel) return;
  wsPreviewState = createPreviewState(wsPreviewLevel);wsWinAnnounced = false;
  $('ws-preview-status').textContent = 'С начала. Доберись до финиша и попробуй собрать монеты.';
  clearWorkshopInput();drawCurrentPreview();
}
function workshopFrame(dt) {
  if (screen !== 'workshop' || wsPage !== 'preview' || !wsPreviewState) return;
  const axis = wsControlMode === 'buttons' ? wsTouchButtons.axis :
    Number(wsKeys.has('d') || wsKeys.has('arrowright')) - Number(wsKeys.has('a') || wsKeys.has('arrowleft'));
  const jump = wsJumpQueued || (wsControlMode === 'keyboard' &&
    (wsKeys.has(' ') || wsKeys.has('arrowup') || wsKeys.has('w')));
  const trigger = wsTriggerQueued || (wsControlMode === 'keyboard' && wsKeys.has('e'));
  wsJumpQueued = false;wsTriggerQueued = false;
  stepPreview(wsPreviewState, {axis, jump, trigger}, dt);
  drawCurrentPreview();
  if (wsPreviewState.won && !wsWinAnnounced) {
    wsWinAnnounced = true;$('ws-preview-status').textContent = `Уровень пройден! Собрано монет: ${wsPreviewState.coins}.`;
    notice('Уровень пройден!');
  }
}
async function publishWorkshopDraft() {
  const check = validateDraft(wsDraft);
  if (!check.ok) {showWorkshopMessage(check.message);return;}
  const button = $('ws-publish');button.disabled = true;
  showWorkshopMessage('');$('ws-autosave-status').textContent = 'Публикуем уровень…';
  try {
    const result = await publishLevel(wsDraft);
    wsDraft.publishedId = result.id;wsDraft.publishedAt = Date.now();
    saveWorkshopDraft(`Опубликовано · ID ${result.id}. Запись появилась в каталоге.`);
    showWorkshopMessage(`Уровень опубликован под ID ${result.id}. Он доступен в каталоге и нативной игре.`);
    notice(`Уровень опубликован · ID ${result.id}`, 6000);
  } catch (error) {
    const message = error.message.startsWith('Уровень ') ? error.message :
      error.status === 401 || error.status === 403 ?
      'Firebase отклонил запись. Для публикации правила должны разрешать создание записей в /levels и /levels-index. Правила базы автоматически не менялись.' :
      error.message;
    $('ws-autosave-status').textContent = 'Черновик сохранён локально; публикация не подтверждена.';
    showWorkshopMessage(message);
  } finally {button.disabled = false;}
}
function openWorkshop() {setWorkshopPage('home');}
async function refreshRooms() {
  const list = $('room-list');
  if (screen !== 'rooms') return;
  list.innerHTML = '';
  const loading = document.createElement('p');
  loading.className = 'muted';loading.textContent = 'Обновляем комнаты…';list.append(loading);
  try {
    const info = (await listRooms()).slice(0, 12);
    if (screen !== 'rooms') return;
    list.innerHTML = '';
    for (const {id, room: entry} of info) {
      const button = document.createElement('button');
      button.type = 'button';button.className = 'room-item';
      const left = document.createElement('span');
      const code = document.createElement('strong');code.textContent = id;
      const detail = document.createElement('small');
      detail.textContent = entry.map === 5 ? 'Водное поле · кувшинки' : 'Обычный газон';
      left.append(code, detail);
      const tag = document.createElement('span');tag.className = 'badge';
      tag.textContent = 'Войти';
      button.append(left, tag);
      button.addEventListener('click', () => enterRoom(id));
      list.append(button);
    }
    if (!info.length) {
      const empty = document.createElement('p');empty.className = 'muted';
      empty.textContent = 'Пока нет свободных комнат. Создай свою кнопкой «+»!';
      list.append(empty);
    }
    connection(true, 'Firebase на связи');
  } catch (e) {
    list.innerHTML = '';
    const error = document.createElement('p');error.className = 'muted';error.textContent = e.message;
    list.append(error);connection(false, 'Нет связи');notice(e.message, 6500);
  }
}
function renderLobby() {
  if (!roomId || !room) return;
  $('room-code').textContent = roomId;
  $('lobby-subtitle').textContent = `${room.map === 5 ? 'Водная карта' : 'Обычный газон'} · ${room.guest ? 'оба игрока подключены' : 'ждём второго игрока'}`;
  const mine = room[slot]?.role, otherSlot = slot === 'host' ? 'guest' : 'host';
  const other = room[otherSlot]?.role;
  for (const card of document.querySelectorAll('.side-card')) {
    const role = card.dataset.side;
    card.classList.toggle('selected', mine === role);
    card.disabled = !!room.state || (other === role && mine !== role);
  }
  $('plants-taken').textContent = mine === 'plants' ? 'ТВОЯ СТОРОНА' : other === 'plants' ? 'Занято соперником' : 'Выбрать растения';
  $('zombies-taken').textContent = mine === 'zombies' ? 'ТВОЯ СТОРОНА' : other === 'zombies' ? 'Занято соперником' : 'Выбрать зомби';
  $('lobby-status').textContent = !room.guest ? 'Поделись ссылкой и жди друга. Сторону можно выбрать уже сейчас.' :
    mine && other && mine !== other ? 'Оба игрока готовы! Бой начинается…' :
    mine && mine === other ? 'Выбрана одна сторона. Одному игроку нужно поменять выбор.' :
    mine ? 'Ты готов. Ждём выбора соперника…' : 'Выбери, за кого будешь играть.';
}
function enterMatch() {
  if (screen === 'match') return;
  show('match');
  $('match-code').textContent = roomId;
  $('match-title').textContent = room?.[slot]?.role === 'plants' ? 'Защити Хлебушка' : 'Прорви защиту Кирилла';
  $('finish-wave').classList.toggle('hidden', room?.[slot]?.role !== 'zombies');
  $('match-hint').textContent = room?.[slot]?.role === 'plants' ?
    'Выбери пакетик и клетку. Монеты подсолнуха собираются касанием.' :
    'Выбери вид утки слева и коснись нужного ряда на поле.';
}
async function enterRoom(id, alreadyJoined = false) {
  if (!validId(id)) {notice('Неверный код комнаты.');return;}
  const gen = ++generation;
  connection(null, 'Подключаемся…');
  try {
    const joined = alreadyJoined ? await getRoom(id) : await joinRoom(id, playerId);
    if (gen !== generation) return;
    if (!joined || joined.version !== 1 ||
        (alreadyJoined && joined[slot]?.id !== playerId) ||
        (!alreadyJoined && joined.guest?.id !== playerId))
      throw new Error('Комната закрылась или другой игрок занял твоё место.');
    roomId = id;room = joined;
    if (!alreadyJoined) slot = 'guest';
    seq = Math.max(0, Number(joined.state?.ackGuest) || 0);
    session();renderLobby();show('lobby');
    if (validMatch(joined.state)) {
      state = joined.state;
      lastSnapshot = performance.now();enterMatch();
    }
    connection(true, 'В комнате');
    poll(gen);
  } catch (e) {
    if (gen !== generation) return;
    roomId = '';slot = '';room = null;state = null;
    session();clearInvite();show('rooms');connection(false, 'Ошибка комнаты');
    notice(e.message, 6000);refreshRooms();
  }
}
async function create() {
  $('create').disabled = true;
  connection(null, 'Создаём комнату…');
  const gen = ++generation;
  try {
    const result = await createRoom(playerId, mapChoice);
    if (gen !== generation) return;
    roomId = result.id;slot = 'host';room = result.room;state = null;
    session();renderLobby();show('lobby');connection(true, 'Комната создана');
    poll(gen);
  } catch (e) {connection(false, 'Ошибка комнаты');notice(e.message, 7000);}
  finally {$('create').disabled = false;}
}
async function choose(side) {
  if (!roomId || !slot || room?.state) return;
  const other = room[slot === 'host' ? 'guest' : 'host']?.role;
  if (other === side) {notice('Эту сторону уже выбрал соперник.');return;}
  try {
    await chooseRole(roomId, slot, side);
    if (room && room[slot]) room[slot].role = side;
    renderLobby();
  } catch (e) {notice(e.message);}
}
async function leave() {
  if (!roomId) {show('rooms');refreshRooms();return;}
  const id = roomId, oldSlot = slot;
  generation++;
  roomId = '';slot = '';room = null;state = null;pendingSeq = 0;
  session();clearInvite();show('rooms');connection(null, 'Выходим…');
  try {await leaveRoom(id, oldSlot, playerId);connection(true, 'Firebase на связи');}
  catch (e) {connection(false, 'Не удалось закрыть комнату');notice(e.message);}
  refreshRooms();
}
async function publish() {
  if (!state || !roomId || slot !== 'host' || writing) return;
  writing = true;lastPublish = performance.now();
  const id = roomId;
  try {await writeState(id, state);if (id === roomId) connection(true, 'Бой онлайн');}
  catch (e) {if (id === roomId) {connection(false, 'Бой без связи');notice(e.message, 6000);}}
  finally {writing = false;}
}
async function poll(gen) {
  if (gen !== generation || !roomId) return;
  try {
    const current = await getRoom(roomId);
    if (gen !== generation) return;
    if (!current || current.version !== 1 || current[slot]?.id !== playerId) {
      generation++;roomId = '';slot = '';room = null;state = null;pendingSeq = 0;
      session();clearInvite();show('rooms');connection(false, 'Комната закрыта');
      notice('Комната закрыта. Вернулись к списку комнат.');refreshRooms();
      return;
    }
    room = current;
    if (slot === 'host') {
      if (!state && current.host.role && current.guest?.role && current.host.role !== current.guest.role) {
        state = validMatch(current.state) ? current.state : newMatch(current.map);
        publish();
      }
      if (state && current.guest?.id && current.command?.id === current.guest.id &&
          Number.isSafeInteger(current.command.seq) && current.command.seq > state.ackGuest) {
        applyCommand(state, current.guest.role, current.command);
        state.ackGuest = current.command.seq; // acknowledge even a rejected move
        publish();
      }
    } else if (validMatch(current.state)) {
      // The other device is authoritative. Never run the physics on the guest.
      if (!state || current.state.time >= state.time || current.state.winner) {
        state = current.state;lastSnapshot = performance.now();
      }
      if (pendingSeq && state.ackGuest >= pendingSeq) pendingSeq = 0;
    }
    if (state) enterMatch();
    else {show('lobby');renderLobby();}
    if (performance.now() - lastPing > 5000) {
      lastPing = performance.now();
      await heartbeat(roomId, slot);
    }
    connection(true, state ? 'Бой онлайн' : 'В комнате');
  } catch (e) {
    if (gen === generation) {connection(false, 'Нет связи');notice(e.message, 4500);}
  } finally {
    if (gen === generation && roomId) setTimeout(() => poll(gen), 600);
  }
}
async function send(cmd) {
  if (!state || state.winner || !roomId || !room?.[slot]?.role) return;
  if (slot === 'host') {
    if (!applyCommand(state, room.host.role, cmd)) notice('Пока нельзя: проверь монеты, клетку и перезарядку.');
    else publish();
  } else {
    if (pendingSeq) {notice('Подожди, предыдущий ход ещё передаётся.');return;}
    pendingSeq = ++seq;
    const payload = cmd.kind === 'coin' ? {kind:'coin',coinId:cmd.id} : cmd;
    try {await writeCommand(roomId, {...payload, id: playerId, seq: pendingSeq});}
    catch (e) {pendingSeq = 0;notice(e.message, 6500);}
  }
}
function pointer(event) {
  if (screen !== 'match' || !state || state.winner) return;
  const rect = canvas.getBoundingClientRect();
  const x = (event.clientX - rect.left) * W / rect.width;
  const y = (event.clientY - rect.top) * H / rect.height;
  if (y >= 26 && y <= 93 && x >= 924) {
    if (x < 1080) $('book-dialog').showModal();
    else leave();
    return;
  }
  const role = room?.[slot]?.role;
  if (role === 'plants') {
    const coin = state.coins.find(c => Math.hypot(x-c.x, y-c.y) < 36);
    if (coin) {send({kind:'coin', id:coin.id});return;}
    if (x >= 18 && x <= 231) {
      const index = Math.floor((y - 137) / 107);
      if (index >= 0 && index < PLANTS.length && y <= 137 + index*107 + 98) {
        chosenPlant = index;notice(`Выбрано: ${PLANTS[index].name}`);return;
      }
    }
    if (chosenPlant >= 0 && x >= X && x < W && y >= Y && y < Y + ROWS*CH) {
      const row = Math.floor((y-Y)/CH), col = Math.floor((x-X)/CW);
      send({kind:'plant', type:chosenPlant, row, col});chosenPlant = -1;
    }
  } else if (role === 'zombies') {
    if (x >= 18 && x <= 231) {
      const index = Math.floor((y - 169) / 155);
      if (index >= 0 && index < DUCKS.length && y <= 169 + index*155 + 136) {
        chosenDuck = index;notice(`Выбрано: ${DUCKS[index].name}`);return;
      }
    }
    if (chosenDuck >= 0 && x >= X && y >= Y && y < Y + ROWS*CH) {
      send({kind:'spawn', type:DUCKS[chosenDuck].id, row:Math.floor((y-Y)/CH)});chosenDuck = -1;
    }
  }
}
function frame(now) {
  const dt = Math.min(.05, (now - lastFrame) / 1000);
  lastFrame = now;
  if (screen === 'match' && state) {
    if (slot === 'host' && room?.guest && !document.hidden) {
      stepMatch(state, dt);
      if (now - lastPublish > 320) publish();
    }
    drawGame(canvas, state, {role:room?.[slot]?.role, id:roomId,
      selected:room?.[slot]?.role === 'plants' ? chosenPlant : chosenDuck,
      pending:!!pendingSeq,
      liveDelay:slot === 'guest' ? Math.min(.4, (now-lastSnapshot)/1000) : 0});
  }
  if (screen === 'workshop') workshopFrame(dt);
  requestAnimationFrame(frame);
}
function populateBook() {
  for (const p of PLANTS) {
    const item = document.createElement('div');item.className = 'book-entry';
    const img = document.createElement('img');img.src = `../assets/art/${p.image}`;img.alt = '';
    const text = document.createElement('div');
    const name = document.createElement('strong');name.textContent = p.name;
    const detail = document.createElement('small');detail.textContent = `${p.cost} монет · ${p.hp} HP. ${p.detail}`;
    text.append(name, detail);item.append(img, text);$('book-plants').append(item);
  }
  for (const d of DUCKS) {
    const item = document.createElement('div');item.className = 'book-entry';
    const img = document.createElement('img');img.src = `../assets/art/${d.image}`;img.alt = '';
    const text = document.createElement('div');
    const name = document.createElement('strong');name.textContent = d.name;
    const detail = document.createElement('small');detail.textContent = `${d.cost} монет · ${d.hp} HP. ${d.detail}`;
    text.append(name, detail);item.append(img, text);$('book-ducks').append(item);
  }
}

$('refresh').addEventListener('click', refreshRooms);
$('create').addEventListener('click', create);
$('open-workshop').addEventListener('click', openWorkshop);
$('workshop-online-back').addEventListener('click', () => {show('rooms');refreshRooms();});
$('ws-continue').addEventListener('click', () => setWorkshopPage('editor'));
$('ws-new-draft').addEventListener('click', beginNewDraft);
$('ws-open-catalog').addEventListener('click', () => {setWorkshopPage('catalog');loadWorkshopCatalog();});
$('ws-editor-back').addEventListener('click', () => setWorkshopPage('home'));
$('ws-catalog-back').addEventListener('click', () => setWorkshopPage('home'));
$('ws-catalog-refresh').addEventListener('click', loadWorkshopCatalog);
$('ws-preview').addEventListener('click', () => {
  const check = validateDraft(wsDraft);
  if (!check.ok) {showWorkshopMessage(check.message);return;}
  startWorkshopPreview(wsDraft, wsDraft.title, 'editor');
});
$('ws-publish').addEventListener('click', publishWorkshopDraft);
$('ws-preview-back').addEventListener('click', () => {
  clearWorkshopInput();setWorkshopPage(wsPreviewReturn);
  if (wsPreviewReturn === 'catalog') loadWorkshopCatalog();
});
$('ws-preview-reset').addEventListener('click', resetWorkshopPreview);
$('ws-title-input').addEventListener('input', event => {
  wsDraft.title = event.target.value;showWorkshopMessage('');saveWorkshopDraft();
});
$('ws-description-input').addEventListener('input', event => {
  wsDraft.description = event.target.value;saveWorkshopDraft();
});
for (const button of document.querySelectorAll('[data-ws-tool]'))
  button.addEventListener('click', () => setWorkshopTool(button.dataset.wsTool));
for (const button of document.querySelectorAll('[data-ws-type]')) button.addEventListener('click', () => {
  wsType = button.dataset.wsType;
  if (wsType === 'trigger') wsTriggerKind = 'move';
  if (wsType === 'block') wsBlockType = 'block';
  wsPaletteSelected = false;wsTool = 'build';
  renderWorkshopEditor();
});
$('ws-editor-canvas').addEventListener('pointerdown', wsPointerDown);
$('ws-editor-canvas').addEventListener('pointermove', wsPointerMove);
$('ws-editor-canvas').addEventListener('pointerup', wsPointerUp);
$('ws-editor-canvas').addEventListener('pointercancel', wsPointerUp);
$('ws-editor-canvas').addEventListener('lostpointercapture', wsPointerUp);
$('ws-delete-selected').addEventListener('click', wsDeleteSelection);
$('ws-copy-selected').addEventListener('click', wsCopySelection);
$('ws-paste-selected').addEventListener('click', wsPasteSelection);
for (const button of document.querySelectorAll('[data-ws-nudge]'))
  button.addEventListener('click', () => wsNudge(button.dataset.wsNudge));
for (const button of document.querySelectorAll('[data-ws-scale]')) {
  const [axis, sign] = button.dataset.wsScale.split(':');
  button.addEventListener('click', () => wsScale(axis, Number(sign)));
}
for (const button of document.querySelectorAll('[data-ws-rotate]'))
  button.addEventListener('click', () => wsRotate(Number(button.dataset.wsRotate)));
for (const button of document.querySelectorAll('[data-ws-flip]'))
  button.addEventListener('click', () => wsFlip(button.dataset.wsFlip));
$('ws-rotate-step').addEventListener('input', wsUpdateRotateLabels);
wsUpdateRotateLabels();
for (const button of document.querySelectorAll('[data-ws-pan]'))
  button.addEventListener('click', () => wsPan(button.dataset.wsPan));
$('ws-object-x').addEventListener('change', e => updateSelectedProperty('x', e.target.value));
$('ws-object-y').addEventListener('change', e => updateSelectedProperty('y', e.target.value));
$('ws-object-width').addEventListener('change', e => updateSelectedProperty('width', e.target.value));
$('ws-object-height').addEventListener('change', e => updateSelectedProperty('height', e.target.value));
$('ws-object-angle').addEventListener('change', e => updateSelectedProperty('angle', e.target.value));
$('ws-object-number').addEventListener('change', e => updateSelectedProperty('number', e.target.value));
$('ws-object-color').addEventListener('input', e => updateSelectedProperty('color', e.target.value));
$('ws-trigger-kind').addEventListener('change', e => updateSelectedTrigger('kind', e.target.value));
$('ws-trigger-event').addEventListener('change', e => updateSelectedTrigger('event', e.target.value));
$('ws-trigger-group').addEventListener('change', e => updateSelectedTrigger('groupId', e.target.value));
$('ws-trigger-forever-group').addEventListener('change', e => updateSelectedTrigger('groupId', e.target.value));
$('ws-trigger-x').addEventListener('change', e => updateSelectedTrigger('valueX', e.target.value));
$('ws-trigger-y').addEventListener('change', e => updateSelectedTrigger('valueY', e.target.value));
$('ws-trigger-duration').addEventListener('change', e => updateSelectedTrigger('duration', e.target.value));
$('ws-trigger-action').addEventListener('change', e => updateSelectedTrigger('action', e.target.value));
$('ws-control-select').addEventListener('change', e => applyWorkshopControl(e.target.value));
for (const button of document.querySelectorAll('[data-ws-hold]')) {
  const direction = button.dataset.wsHold;
  button.addEventListener('pointerdown', event => {
    event.preventDefault();
    if (!wsTouchButtons.press(event.pointerId, direction)) return;
    button.setPointerCapture?.(event.pointerId);
  });
  const release = event => {wsTouchButtons.release(event.pointerId);};
  button.addEventListener('pointerup', release);
  button.addEventListener('pointercancel', release);
  button.addEventListener('lostpointercapture', release);
}
for (const button of document.querySelectorAll('[data-ws-press]')) {
  const action = button.dataset.wsPress === 'jump' ? 'jump' : 'trigger';
  const press = () => {if (action === 'jump') wsJumpQueued = true;else wsTriggerQueued = true;};
  button.addEventListener('pointerdown', event => {
    event.preventDefault();button.setPointerCapture?.(event.pointerId);press();
  });
  button.addEventListener('click', event => {if (event.detail === 0) press();});
}
window.addEventListener('keydown', event => {
  if (screen !== 'workshop') return;
  if (wsPage === 'editor') {
    if (event.target?.closest?.('input, textarea, select, [contenteditable="true"]')) return;
    const key = event.key.toLowerCase();
    if ((event.ctrlKey || event.metaKey) && key === 'c') {
      event.preventDefault();wsCopySelection();return;
    }
    if ((event.ctrlKey || event.metaKey) && key === 'v') {
      event.preventDefault();wsPasteSelection();return;
    }
    if (['arrowleft', 'arrowright', 'arrowup', 'arrowdown'].includes(key)) {
      event.preventDefault();wsNudge(key.slice(5), event.shiftKey ? 10 : 1);return;
    }
    if (key === 'delete' || key === 'backspace') {
      event.preventDefault();wsDeleteSelection();return;
    }
    return;
  }
  if (wsPage !== 'preview' || wsControlMode !== 'keyboard') return;
  const key = event.key.toLowerCase();
  if (['arrowleft', 'arrowright', 'arrowup', ' ', 'a', 'd', 'w', 'e'].includes(key)) event.preventDefault();
  wsKeys.add(key);
  if (!event.repeat && ['arrowup', ' ', 'w'].includes(key)) wsJumpQueued = true;
  if (!event.repeat && key === 'e') wsTriggerQueued = true;
});
window.addEventListener('keyup', event => wsKeys.delete(event.key.toLowerCase()));
window.addEventListener('blur', clearWorkshopInput);
for (const card of document.querySelectorAll('.map-card')) card.addEventListener('click', () => {
  mapChoice = Number(card.dataset.map);
  for (const other of document.querySelectorAll('.map-card')) other.classList.toggle('active', other === card);
});
for (const card of document.querySelectorAll('.side-card')) card.addEventListener('click', () => choose(card.dataset.side));
$('share').addEventListener('click', async () => {
  const url = `${location.origin}${location.pathname}?room=${roomId}`;
  try {await navigator.clipboard.writeText(url);notice('Ссылка на комнату скопирована.');}
  catch {notice(`Код комнаты: ${roomId}`);}
});
$('leave-lobby').addEventListener('click', leave);
$('leave-match').addEventListener('click', leave);
$('finish-wave').addEventListener('click', () => {
  if (room?.[slot]?.role === 'zombies') send({kind:'finish'});
});
$('book').addEventListener('click', () => $('book-dialog').showModal());
$('music').addEventListener('click', async () => {
  const audio = $('soundtrack');
  if (audio.paused) {
    try {await audio.play();$('music').textContent = 'Выключить музыку';}
    catch {notice('Браузер не разрешил воспроизвести музыку.');}
  } else {audio.pause();$('music').textContent = 'Музыка';}
});
canvas.addEventListener('pointerdown', pointer);
populateBook();preloadArtwork();
requestAnimationFrame(frame);
let previous = null;
try {previous = JSON.parse(sessionStorage.getItem('pvg3-room') || 'null');}
catch {sessionStorage.removeItem('pvg3-room');}
const invite = new URLSearchParams(location.search).get('room');
if (validId(invite) && previous?.id !== invite) {
  // A new invitation should take priority over another room from this tab.
  enterRoom(invite);
} else if (previous && validId(previous.id) && ['host','guest'].includes(previous.slot)) {
  slot = previous.slot;enterRoom(previous.id, true);
} else if (validId(invite)) enterRoom(invite);
else refreshRooms();
