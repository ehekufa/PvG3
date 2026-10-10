import {PLANTS, DUCKS, W, H, X, Y, CW, CH, ROWS, COLS,
        newMatch, validMatch, applyCommand, stepMatch} from './rules.js';
import {DATABASE, validId, randomPlayerId, listRooms, getRoom, createRoom,
        joinRoom, chooseRole, writeState, writeCommand, heartbeat, leaveRoom,
        listPublishedLevels, getPublishedLevel, publishLevel,
        setLevelLike, recordLevelDownload} from './firebase.js';
import {rotateToken} from './accounts.js';
import {preloadArtwork, drawGame} from './draw.js';
import {LEVEL_WIDTH, LEVEL_HEIGHT, MAX_LEVEL_OBJECTS, WORLD_LIMIT,
        MIN_OBJECT_SIZE, MAX_OBJECT_WIDTH, MAX_OBJECT_HEIGHT, TILE_W, TILE_H,
        TYPE_LABELS, TRIGGER_KINDS, TRIGGER_LABELS, canManuallyRecolorType,
        isOfficialLevel, newDraft, setTriggerKind,
        addObject, findObjectAt, validateDraft, draftFromPublished,
        moveObjects, resizeObjects, rotateObjects, flipObjects, panCamera, copyObjects, pasteObjects,
        createPreviewState, stepPreview,
        drawEditorCanvas, drawPreviewCanvas, normalizeParticleEmitter,
        drawParticleEmitterPreview, filterPublishedLevels,
        resolveControlMode, createTouchButtonState} from './workshop.js';
import {currentSession, createAccount, signIn, signOut, isModerator, onSessionChange,
        loadBans, isBanned, loadComments, postComment, hideComment, banAccount,
        unbanAccount, setLevelOfficial, MAX_COMMENT} from './accounts.js';

const $ = id => document.getElementById(id);
const requestedLocale = new URLSearchParams(location.search).get('lang');
const WS_ENGLISH = requestedLocale ? requestedLocale.toLowerCase().startsWith('en') :
  (navigator.language || '').toLowerCase().startsWith('en');
document.documentElement.lang = WS_ENGLISH ? 'en' : 'ru';
const wsText = (ru, en) => WS_ENGLISH ? en : ru;
function applyWorkshopLocale() {
  for (const node of document.querySelectorAll('[data-ws-ru][data-ws-en]'))
    node.textContent = node.getAttribute(WS_ENGLISH ? 'data-ws-en' : 'data-ws-ru');
  $('ws-tags-input').placeholder = wsText('скорость, тайминг', 'speed, timing');
  $('ws-catalog-search').placeholder = wsText(
    'Название, автор, описание или тег', 'Title, author, description, or tag');
  $('ws-catalog-page').setAttribute('aria-label',
    wsText('Каталог уровней игроков', 'Player level catalog'));
}
applyWorkshopLocale();
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
const WS_TUTORIAL_KEY = 'pvg3-workshop-tutorial-v1';
const WS_ACCOUNT_LOGIN_KEY = 'pvg3-account-last-login-v1';
const WS_DB_NAME = 'pvg3-workshop';
const WS_DB_STORE = 'drafts';
const WS_LOCAL_FALLBACK_MAX = 1024 * 1024;
let wsDbPromise = null;
let wsDraftSaveTimer = 0;
const wsPages = {home: $('ws-home-page'), editor: $('ws-editor-page'),
  preview: $('ws-preview-page'), catalog: $('ws-catalog-page')};
let wsDraftFromLocalStorage = false;
let wsPage = 'home', wsDraft = loadWorkshopDraft(), wsTool = 'build', wsType = 'block';
let wsDraftReady = Promise.resolve();
let publishAfterAccount = false;
let wsTriggerKind = 'move', wsBlockType = 'block', wsOrbType = 'orb-yellow';
let wsGoalType = 'goal', wsPortalType = 'portal-normal', wsPaletteSelected = true;
let wsSelectedId = 0, wsSelectedIds = new Set(), wsClipboard = [];
let wsDrag = null, wsPanDrag = null, wsCatalogGeneration = 0, wsCatalog = [];
let wsCamera = {x: 0, y: 0};
let wsPreviewState = null, wsPreviewLevel = null, wsPreviewReturn = 'editor';
let wsBans = {};
let wsParticleDialogObjectId = 0, wsParticlePreviewTime = 0, wsParticleAnimation = 0;
let wsControlPreference = localSetting(WS_CONTROL_KEY, 'auto'), wsControlMode = 'keyboard';
// Tool captions such as «ЗЕРКАЛЬНОЕ ОТРАЖЕНИЕ» are a tutorial aid only.
let wsTutorialHints = localSetting(WS_TUTORIAL_KEY, '0') === '1';
let wsJumpQueued = false, wsDashQueued = false, wsTriggerQueued = false;
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
  triggerGravity: 'Триггер-гравитации.png', triggerColor: 'Триггер-цвет.png',
  'orb-orange': 'Оранжевый opб.png', 'orb-yellow': 'Жёлтый орб.png',
  checkpoint: 'Чекпоинт-выключен.png', checkpointActive: 'Чекпоинт-включён.png',
  'portal-normal': 'Портал-обычный.png', 'portal-jetpack': 'Портал-джетпака.png',
  jetpackActive: 'джетпак-активен.png', jetpackInactive: 'Джетпак-отключён.png',
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
let statsClientId = '';
try {
  const savedStatsId = localStorage.getItem('pvg3-level-stats-client');
  statsClientId = savedStatsId && /^[0-9a-f]{32}$/.test(savedStatsId) ?
    savedStatsId : randomPlayerId();
  localStorage.setItem('pvg3-level-stats-client', statsClientId);
} catch {statsClientId = playerId;}

function show(name) {
  screen = name;
  for (const [key, el] of Object.entries(sections)) el.classList.toggle('hidden', key !== name);
}
function localSetting(key, fallback) {
  try {return localStorage.getItem(key) ?? fallback;}
  catch {return fallback;}
}
function normalizeWorkshopDraft(saved) {
  const validShape = saved && saved.width === LEVEL_WIDTH && saved.height === LEVEL_HEIGHT &&
    typeof saved.title === 'string' && saved.title.length <= 80 &&
    typeof saved.description === 'string' && saved.description.length <= 160 &&
    Array.isArray(saved.objects) && saved.objects.length <= MAX_LEVEL_OBJECTS &&
    saved.objects.every(o => o && Object.hasOwn(TYPE_LABELS, o.type) && Number.isInteger(o.id) &&
      Number.isFinite(o.x) && Number.isFinite(o.y) && Number.isFinite(o.w) && Number.isFinite(o.h) &&
      typeof o.color === 'string' && /^#[0-9a-f]{6}$/i.test(o.color));
  if (!validShape) return null;
  for (const object of saved.objects) {
    if (!canManuallyRecolorType(object.type)) object.color = '#fffdf8';
    object.flipX = object.flipX === true;object.flipY = object.flipY === true;
    if (object.type === 'particle') object.emitter = normalizeParticleEmitter(object.emitter);
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
    } else if (object.trigger.kind === 'gravity') {
      object.trigger.action = 'set-gravity';
      const value = Number.isInteger(object.trigger.value) ? object.trigger.value : 0;
      object.trigger.value = Math.max(-100, Math.min(100, value));
    }
  }
  return saved;
}
function loadWorkshopDraft() {
  try {
    const saved = normalizeWorkshopDraft(JSON.parse(localStorage.getItem(WS_DRAFT_KEY) || 'null'));
    if (saved) {wsDraftFromLocalStorage = true;return saved;}
  } catch {}
  return newDraft();
}
function openWorkshopDb() {
  if (!globalThis.indexedDB) return Promise.reject(new Error('IndexedDB недоступна'));
  if (!wsDbPromise) {
    wsDbPromise = new Promise((resolve, reject) => {
      const request = indexedDB.open(WS_DB_NAME, 1);
      request.onupgradeneeded = () => {
        if (!request.result.objectStoreNames.contains(WS_DB_STORE))
          request.result.createObjectStore(WS_DB_STORE);
      };
      request.onsuccess = () => {
        const db = request.result;
        db.onversionchange = () => {db.close();wsDbPromise = null;};
        resolve(db);
      };
      request.onerror = () => reject(request.error || new Error('Не удалось открыть хранилище'));
      request.onblocked = () => reject(new Error('Хранилище черновика занято другой вкладкой'));
    }).catch(error => {wsDbPromise = null;throw error;});
  }
  return wsDbPromise;
}
function readWorkshopDraftFromDb() {
  return openWorkshopDb().then(db => new Promise((resolve, reject) => {
    const request = db.transaction(WS_DB_STORE, 'readonly').objectStore(WS_DB_STORE).get(WS_DRAFT_KEY);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('Не удалось прочитать черновик'));
  }));
}
function writeWorkshopDraftToDb(serialized) {
  return openWorkshopDb().then(db => new Promise((resolve, reject) => {
    const transaction = db.transaction(WS_DB_STORE, 'readwrite');
    transaction.objectStore(WS_DB_STORE).put(serialized, WS_DRAFT_KEY);
    transaction.oncomplete = resolve;
    transaction.onerror = () => reject(transaction.error || new Error('Не удалось сохранить черновик'));
    transaction.onabort = () => reject(transaction.error || new Error('Сохранение черновика отменено'));
  }));
}
async function loadWorkshopDraftFromDb() {
  if (wsDraftFromLocalStorage) return; // recent small drafts already live in the sync fallback
  try {
    const serialized = await readWorkshopDraftFromDb();
    if (typeof serialized !== 'string') return;
    const saved = normalizeWorkshopDraft(JSON.parse(serialized));
    if (saved) wsDraft = saved;
  } catch {}
}
wsDraftReady = loadWorkshopDraftFromDb();
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
  $('ws-draft-status').textContent = wsDraft.publishedId ?
    `Последняя публикация: ID ${wsDraft.publishedId}` : '';
}
async function persistWorkshopDraft() {
  let serialized, localSaved = false;
  try {serialized = JSON.stringify(wsDraft);} catch {return;}
  if (serialized.length <= WS_LOCAL_FALLBACK_MAX) {
    try {localStorage.setItem(WS_DRAFT_KEY, serialized);localSaved = true;} catch {}
  }
  try {
    await writeWorkshopDraftToDb(serialized);
    if (!localSaved) {
      try {localStorage.removeItem(WS_DRAFT_KEY);} catch {}
    }
  } catch {}
}
function saveWorkshopDraft() {
  clearTimeout(wsDraftSaveTimer);
  wsDraftSaveTimer = setTimeout(() => {
    wsDraftSaveTimer = 0;
    void persistWorkshopDraft();
  }, 250);
  $('ws-object-count').textContent = `${wsDraft.objects.length} / ${MAX_LEVEL_OBJECTS} объектов`;
  renderWorkshopHome();
}
function flushWorkshopDraft() {
  if (!wsDraftSaveTimer) return;
  clearTimeout(wsDraftSaveTimer);wsDraftSaveTimer = 0;
  void persistWorkshopDraft();
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
    {kind: 'gravity', label: TRIGGER_LABELS.gravity, art: 'triggerGravity'},
    {kind: 'recolor', label: TRIGGER_LABELS.recolor, art: 'triggerColor'},
    {kind: 'background', label: TRIGGER_LABELS.background, art: 'triggerColor'},
  ] : wsType === 'orb' ? [
    {kind: 'orb-yellow', label: TYPE_LABELS['orb-yellow'], art: 'orb-yellow'},
    {kind: 'orb-orange', label: TYPE_LABELS['orb-orange'], art: 'orb-orange'},
  ] : wsType === 'goal' ? [
    {kind: 'goal', label: TYPE_LABELS.goal, art: 'goal'},
    {kind: 'checkpoint', label: TYPE_LABELS.checkpoint, art: 'checkpoint'},
  ] : wsType === 'portal' ? [
    {kind: 'portal-normal', label: TYPE_LABELS['portal-normal'], art: 'portal-normal'},
    {kind: 'portal-jetpack', label: TYPE_LABELS['portal-jetpack'], art: 'portal-jetpack'},
  ] : wsType === 'particle' ? [
    {kind: 'single', label: TYPE_LABELS.particle, glyph: 'P'},
  ] : [{kind: 'single', label: TYPE_LABELS[wsType], art: wsType}];
  for (const option of options) {
    const button = document.createElement('button');
    button.type = 'button';button.className = 'ws-palette-item';
    const active = wsPaletteSelected &&
      (wsType === 'trigger' ? wsTriggerKind === option.kind :
        wsType === 'block' ? wsBlockType === option.kind :
        wsType === 'orb' ? wsOrbType === option.kind :
        wsType === 'goal' ? wsGoalType === option.kind :
        wsType === 'portal' ? wsPortalType === option.kind : true);
    if (active) button.classList.add('active');
    const icon = option.glyph ? document.createElement('span') : document.createElement('img');
    if (option.glyph) {
      icon.className = 'ws-palette-glyph';icon.textContent = option.glyph;
    } else {
      icon.alt = '';icon.setAttribute('aria-hidden', 'true');
      icon.src = wsArt[option.art]?.src || '';
    }
    icon.setAttribute('aria-hidden', 'true');
    const label = document.createElement('span');label.textContent = option.label;
    button.append(icon, label);
    button.addEventListener('click', () => {
      if (wsType === 'trigger') wsTriggerKind = option.kind;
      else if (wsType === 'block') wsBlockType = option.kind;
      else if (wsType === 'orb') wsOrbType = option.kind;
      else if (wsType === 'goal') wsGoalType = option.kind;
      else if (wsType === 'portal') wsPortalType = option.kind;
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
  $('ws-difficulty-input').value = wsDraft.difficulty || 'normal';
  $('ws-tags-input').value = (wsDraft.tags || []).join(', ');
  $('ws-ability-double-jump').checked = wsDraft.movement?.doubleJump === true;
  $('ws-ability-dash').checked = wsDraft.movement?.dash === true;
  $('ws-ability-wall-slide').checked = wsDraft.movement?.wallSlide === true;
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
  $('ws-particle-fields').classList.toggle('hidden',
    selected.length !== 1 || selected[0]?.type !== 'particle');
  const canCopy = selected.some(item => item.type !== 'player' && item.type !== 'goal');
  $('ws-copy-selected').disabled = !canCopy;
  $('ws-paste-selected').disabled = !wsClipboard.length ||
    wsDraft.objects.length + wsClipboard.length > MAX_LEVEL_OBJECTS;
  $('ws-delete-selected').disabled = !selected.length;
  $('ws-particle-settings').disabled = selected.length !== 1 || selected[0]?.type !== 'particle';
  for (const button of document.querySelectorAll('[data-ws-nudge], [data-ws-scale], [data-ws-rotate], [data-ws-flip]'))
    button.disabled = !selected.length;
  if (!selected.length) return;
  if (selected.length > 1) {
    $('ws-selected-label').textContent = `${selected.length} объектов`;
    return;
  }
  if (!object) return;
  const triggerKind = object.trigger?.kind || 'move';
  const triggerName = triggerKind === 'count' ? wsText('Счётчик', 'Count') :
    triggerKind === 'toggle' ? wsText('Переключатель', 'Toggle') :
    triggerKind === 'spawn' ? wsText('Появление', 'Spawn') : TRIGGER_LABELS[triggerKind];
  const triggerLabel = object.type === 'trigger' ? ` · ${triggerName}` : '';
  $('ws-selected-label').textContent = `${TYPE_LABELS[object.type]}${triggerLabel} · ID ${object.id}`;
  $('ws-object-x').value = Number(object.x.toFixed(2));
  $('ws-object-y').value = Number(object.y.toFixed(2));
  $('ws-object-width').value = Number(object.w.toFixed(2));
  $('ws-object-height').value = Number(object.h.toFixed(2));
  $('ws-object-angle').value = Number((object.angle || 0).toFixed(1));
  $('ws-object-number').value = object.number || 0;
  const colorInput = $('ws-object-color');
  const canRecolor = canManuallyRecolorType(object.type);
  colorInput.value = object.color;
  colorInput.disabled = !canRecolor;
  colorInput.closest('.ws-color-field').classList.toggle('is-disabled', !canRecolor);
  const defaultColorInput = $('ws-object-color-default');
  defaultColorInput.checked = canRecolor && object.defaultColor === true;
  defaultColorInput.disabled = !canRecolor;
  defaultColorInput.closest('.ws-color-default-field')
    .classList.toggle('is-disabled', !canRecolor);
  const triggerFields = $('ws-trigger-fields');
  triggerFields.classList.toggle('hidden', object.type !== 'trigger');
  if (object.type === 'particle') object.emitter = normalizeParticleEmitter(object.emitter);
  if (object.type !== 'trigger') return;
  const t = object.trigger || {};
  const kind = TRIGGER_KINDS.includes(t.kind) ? t.kind : 'move';
  $('ws-trigger-kind').value = kind;
  const forever = kind === 'forever';
  const rotate = kind === 'rotate';
  const gravity = kind === 'gravity';
  const background = kind === 'background';
  const recolor = kind === 'recolor';
  const moving = kind === 'move';
  const grouped = ['move', 'rotate', 'invisibility', 'no-collision',
    'recolor', 'count', 'toggle', 'spawn'].includes(kind);
  const legacyTarget = wsDraft.objects.find(candidate => candidate.id === t.targetId);
  const groupId = Number.isInteger(t.groupId) ? t.groupId :
    Number.isInteger(legacyTarget?.number) ? legacyTarget.number : 0;
  $('ws-trigger-event').value = t.event || 'touch';
  $('ws-trigger-touch-mode-field').classList.toggle('hidden', (t.event || 'touch') !== 'touch');
  $('ws-trigger-touch-mode').value = ['enter', 'exit', 'stay'].includes(t.touchMode) ?
    t.touchMode : 'enter';
  $('ws-trigger-motion-fields').classList.toggle('hidden', forever || gravity || background);
  $('ws-trigger-group-field').classList.toggle('hidden', !grouped);
  $('ws-trigger-count-field').classList.toggle('hidden', kind !== 'count');
  $('ws-trigger-count').value = Number.isInteger(t.count) ? t.count : 3;
  $('ws-trigger-move-fields').classList.toggle('hidden', !moving);
  $('ws-trigger-rotate-field').classList.toggle('hidden', !rotate);
  $('ws-trigger-forever-fields').classList.toggle('hidden', !forever);
  $('ws-trigger-gravity-field').classList.toggle('hidden', !gravity);
  $('ws-trigger-color-field').classList.toggle('hidden', !recolor && !background);
  $('ws-trigger-color-label').textContent = background ?
    wsText('Цвет фона', 'Background color') : wsText('Цвет объектов', 'Object color');
  $('ws-trigger-color').value = /^#[0-9a-f]{6}$/i.test(t.color || '') ? t.color : '#ffc54e';
  $('ws-trigger-group').value = groupId;
  $('ws-trigger-forever-group').value = groupId;
  $('ws-trigger-action').value = ['activate', 'unactivate'].includes(t.action) ? t.action : 'activate';
  $('ws-trigger-x').value = t.valueX ?? t.value ?? 0;
  $('ws-trigger-y').value = t.valueY ?? 0;
  $('ws-trigger-duration').value = t.duration ?? 3;
  const gravityValue = Number.isInteger(t.value) ? Math.max(-100, Math.min(100, t.value)) : 0;
  $('ws-trigger-gravity').value = gravityValue;
  $('ws-trigger-gravity-value').textContent = `${gravityValue > 0 ? '+' : ''}${gravityValue}`;
}
function wsRedrawEditor() {
  renderSelectedObject();
  drawEditorCanvas($('ws-editor-canvas'), wsDraft, wsSelectedIds, wsTool, wsArt, wsCamera);
  $('ws-object-count').textContent = `${wsDraft.objects.length} / ${MAX_LEVEL_OBJECTS} объектов`;
}
function updateParticleDialogFields(emitter) {
  for (const input of document.querySelectorAll('[data-emitter-key]')) {
    const key = input.dataset.emitterKey;
    if (input.type === 'checkbox') input.checked = emitter[key];
    else input.value = emitter[key];
  }
  $('ws-emitter-rate-value').textContent = String(emitter.rate);
  $('ws-emitter-rate-label').textContent = emitter.continuous ? 'Частиц в секунду' :
    'Частиц во всплеске';
  $('ws-emitter-lifetime-value').textContent = `${emitter.lifetime.toFixed(1)} с`;
  $('ws-emitter-speed-value').textContent = `${emitter.speed} px/с`;
  $('ws-emitter-spread-value').textContent = `${emitter.spread}°`;
  $('ws-emitter-direction-value').textContent = `${emitter.direction}°`;
  $('ws-emitter-size-value').textContent = `${emitter.size} px`;
  $('ws-emitter-gravity-value').textContent = `${emitter.gravity} px/с²`;
  $('ws-emitter-gravity').disabled = !emitter.gravityEnabled;
}
function openParticleDialog() {
  const object = wsObject(wsSelectedId);
  if (!object || object.type !== 'particle' || wsSelectedIds.size !== 1) return;
  wsParticleDialogObjectId = object.id;
  object.emitter = normalizeParticleEmitter(object.emitter);
  updateParticleDialogFields(object.emitter);
  wsParticlePreviewTime = 0;
  const dialog = $('ws-particle-dialog');
  if (!dialog.open) dialog.showModal();
  if (wsParticleAnimation) cancelAnimationFrame(wsParticleAnimation);
  let previousFrame = 0;
  const animate = now => {
    if (!dialog.open) {wsParticleAnimation = 0;return;}
    if (previousFrame) wsParticlePreviewTime += Math.min(.05, (now - previousFrame) / 1000);
    previousFrame = now;
    const current = wsObject(wsParticleDialogObjectId);
    if (current?.type === 'particle')
      drawParticleEmitterPreview($('ws-particle-preview'), current.emitter,
        current.color, wsParticlePreviewTime);
    wsParticleAnimation = requestAnimationFrame(animate);
  };
  wsParticleAnimation = requestAnimationFrame(animate);
}
function updateSelectedParticleEmitter(key, value) {
  const object = wsObject(wsParticleDialogObjectId);
  if (!object || object.type !== 'particle') return;
  const emitter = normalizeParticleEmitter(object.emitter);
  emitter[key] = ['enabled', 'continuous', 'gravityEnabled', 'glow'].includes(key) ?
    !!value : Number(value);
  object.emitter = normalizeParticleEmitter(emitter);
  updateParticleDialogFields(object.emitter);
  wsRedrawEditor();saveWorkshopDraft();
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
    if (!wsPaletteSelected) return;
    const objectType = wsType === 'block' ? wsBlockType :
      wsType === 'orb' ? wsOrbType :
      wsType === 'goal' ? wsGoalType :
      wsType === 'portal' ? wsPortalType : wsType;
    const placed = addObject(wsDraft, objectType, Math.floor(point.x), Math.floor(point.y), wsTriggerKind);
    if (!placed) {
      showWorkshopMessage(`Достигнут общий лимит ${MAX_LEVEL_OBJECTS} объектов. Удали лишние объекты перед добавлением новых.`);return;
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
  if (!object || wsSelectedIds.size !== 1 ||
      (property === 'color' && !canManuallyRecolorType(object.type)) ||
      (property === 'defaultColor' && !canManuallyRecolorType(object.type))) return;
  const n = Number(value);
  if (property !== 'color' && property !== 'defaultColor' &&
      !Number.isFinite(n)) return;
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
  } else if (property === 'color') {object.color = value;object.defaultColor = false;}
  else if (property === 'defaultColor') object.defaultColor = value === true;
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
  showWorkshopMessage(wsClipboard.length ? `Скопировано объектов: ${wsClipboard.length}.` : '');
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
    if (!['touch', 'coin', 'manual', 'start'].includes(value)) return;
    t.event = value;
  } else if (property === 'touchMode') {
    if (!['enter', 'exit', 'stay'].includes(value)) return;
    t.touchMode = value;
  } else if (property === 'count') {
    const n = Number(value);if (!Number.isFinite(n)) return;
    t.count = Math.max(1, Math.min(999, Math.trunc(n)));
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
  } else if (property === 'gravity') {
    const n = Number(value);if (!Number.isFinite(n)) return;
    t.value = Math.max(-100, Math.min(100, Math.trunc(n)));
    t.action = 'set-gravity';
  } else if (property === 'color') {
    if (!/^#[0-9a-f]{6}$/i.test(value)) return;
    t.color = value;
    if (t.kind === 'recolor') t.action = 'recolor';
    if (t.kind === 'background') t.action = 'set-background';
  } else if (property === 'action') {
    if (!['activate', 'unactivate'].includes(value)) return;
    t.action = value;
  }
  wsRedrawEditor();saveWorkshopDraft();
}
function beginNewDraft() {
  if (!confirm('Создать новый уровень вместо текущего черновика? Опубликованные уровни не затрагиваются.')) return;
  wsDraft = newDraft();setWorkshopSelection([]);wsClipboard = [];wsTool = 'build';wsType = 'block';
  wsTriggerKind = 'move';wsGoalType = 'goal';wsPortalType = 'portal-normal';
  wsPaletteSelected = true;
  wsCamera = {x: 0, y: 0};
  saveWorkshopDraft();setWorkshopPage('editor');
}
function setCatalogMessage(message = '') {
  const node = $('ws-catalog-message');node.textContent = message;
  node.classList.toggle('hidden', !message);
}
function updateCatalogTagOptions() {
  const select = $('ws-catalog-tag'), selected = select.value || 'all';
  const tags = [...new Set(wsCatalog.flatMap(level => Array.isArray(level.tags) ? level.tags : []))]
    .filter(tag => typeof tag === 'string' && tag.trim())
    .sort((a, b) => a.localeCompare(b, WS_ENGLISH ? 'en' : 'ru'));
  select.replaceChildren();
  const all = document.createElement('option');all.value = 'all';
  all.textContent = wsText('Все теги', 'All tags');select.append(all);
  for (const tag of tags) {
    const option = document.createElement('option');option.value = tag;
    option.textContent = `#${tag}`;select.append(option);
  }
  select.value = tags.includes(selected) ? selected : 'all';
}
function filteredCatalog() {
  return filterPublishedLevels(wsCatalog, {
    query: $('ws-catalog-search').value,
    difficulty: $('ws-catalog-difficulty').value,
    tag: $('ws-catalog-tag').value,
    sort: $('ws-catalog-sort').value,
    language: WS_ENGLISH ? 'en' : 'ru',
  });
}
function renderFilteredCatalog() {renderWorkshopCatalog(filteredCatalog());}
function difficultyLabel(value) {
  const labels = {easy: wsText('Лёгкая', 'Easy'), normal: wsText('Обычная', 'Normal'),
    hard: wsText('Сложная', 'Hard'), expert: wsText('Экспертная', 'Expert')};
  return labels[value] || labels.normal;
}
function renderWorkshopCatalog(levels) {
  const list = $('ws-catalog-list');list.replaceChildren();
  if (!levels.length) {
    const empty = document.createElement('p');empty.className = 'muted';
    empty.textContent = wsCatalog.length ?
      wsText('Нет уровней по выбранным фильтрам.', 'No levels match these filters.') :
      wsText('В каталоге пока нет опубликованных уровней.', 'No published levels yet.');
    list.append(empty);return;
  }
  for (const level of levels) {
    const official = isOfficialLevel(level.id, level);
    const card = document.createElement('article');
    card.className = official ? 'ws-level-card ws-level-card-official' : 'ws-level-card';
    const content = document.createElement('div');
    const meta = document.createElement('div');meta.className = 'ws-level-meta';
    const id = document.createElement('small');id.className = 'ws-level-id';id.textContent = `ID ${level.id}`;
    meta.append(id);
    if (official) {
      const badge = document.createElement('span');badge.className = 'ws-official-badge';
      badge.textContent = wsText('ОФИЦИАЛЬНЫЙ', 'OFFICIAL');
      badge.setAttribute('aria-label', wsText('Официальный уровень', 'Official level'));
      meta.append(badge);
    }
    const difficulty = document.createElement('span');difficulty.className = 'ws-difficulty-badge';
    difficulty.textContent = difficultyLabel(level.difficulty);meta.append(difficulty);
    const title = document.createElement('h3');title.textContent = level.title;
    const description = document.createElement('p');description.textContent = level.description ||
      wsText('Авторский платформерный уровень.', 'Player-made platform level.');
    content.append(meta, title, description);
    if (Array.isArray(level.tags) && level.tags.length) {
      const tags = document.createElement('div');tags.className = 'ws-level-tags';
      for (const value of level.tags) {
        const tag = document.createElement('span');tag.className = 'ws-level-tag';
        tag.textContent = `#${value}`;tags.append(tag);
      }
      content.append(tags);
    }
    const playButton = document.createElement('button');playButton.type = 'button';
    playButton.className = 'ws-level-play';
    playButton.textContent = wsText('Играть', 'Play');
    playButton.addEventListener('click', () => playPublishedLevel(level.id, playButton));
    const panel = document.createElement('div');panel.className = 'ws-comments hidden';
    const stats = document.createElement('div');stats.className = 'ws-level-stats';
    const downloads = document.createElement('span');downloads.className = 'ws-download-count';
    downloads.textContent = `${wsText('Скачивания', 'Downloads')}: ${level.downloads || 0}`;
    const like = document.createElement('button');like.type = 'button';
    like.className = `ws-like-button${level.liked ? ' is-liked' : ''}`;
    like.setAttribute('aria-pressed', String(level.liked === true));
    like.setAttribute('aria-label', level.liked ?
      wsText('Убрать лайк', 'Remove like') : wsText('Поставить лайк', 'Like this level'));
    like.innerHTML = `<span aria-hidden="true">♥</span><span>${level.likes || 0}</span>`;
    like.addEventListener('click', () => toggleCatalogLike(level, like));
    stats.append(downloads, like);
    card.append(content, playButton, stats, levelFooter(level, official, panel), panel);
    list.append(card);
  }
}
async function toggleCatalogLike(level, button) {
  const wasLiked = level.liked === true;
  button.disabled = true;
  try {
    const liked = await setLevelLike(level.id, statsClientId, !wasLiked);
    level.liked = liked;
    level.likes = Math.max(0, (level.likes || 0) + (liked ? 1 : -1));
    renderFilteredCatalog();
  } catch (error) {
    setCatalogMessage(error.message);
    button.disabled = false;
  }
}
function levelFooter(level, official, panel) {
  const footer = document.createElement('div');footer.className = 'ws-level-footer';
  if (level.author) {
    const author = document.createElement('small');author.className = 'ws-level-author';
    author.textContent = isBanned(wsBans, level.author) ?
      wsText(`Автор: ${level.author} (забанен)`, `Author: ${level.author} (banned)`) :
      wsText(`Автор: ${level.author}`, `Author: ${level.author}`);
    footer.append(author);
  }
  const toggle = document.createElement('button');toggle.type = 'button';
  toggle.className = 'ws-comments-toggle';toggle.textContent = 'Комментарии';
  toggle.addEventListener('click', () => {
    panel.classList.toggle('hidden');
    if (!panel.classList.contains('hidden')) renderComments(level.id, panel);
  });
  footer.append(toggle);
  if (isModerator()) footer.append(moderatorActions(level, official));
  return footer;
}
function moderatorActions(level, official) {
  const wrap = document.createElement('div');wrap.className = 'ws-mod-actions';
  const officialButton = document.createElement('button');
  officialButton.type = 'button';officialButton.className = 'small-button';
  officialButton.textContent = official ? 'Снять «официальный»' : 'Сделать официальным';
  officialButton.addEventListener('click', async () => {
    officialButton.disabled = true;
    try {await setLevelOfficial(level.id, !official);notice('Готово.');await loadWorkshopCatalog();}
    catch (error) {setCatalogMessage(error.message);}
    finally {officialButton.disabled = false;}
  });
  wrap.append(officialButton);
  if (!level.author) return wrap;
  const banned = isBanned(wsBans, level.author);
  const reason = document.createElement('input');
  reason.type = 'text';reason.maxLength = 140;reason.className = 'ws-ban-reason';
  reason.placeholder = banned ? 'Причина разбана' : 'Причина бана';
  const banButton = document.createElement('button');
  banButton.type = 'button';banButton.className = 'small-button';
  banButton.textContent = banned ? 'Разбанить автора' : 'Забанить автора';
  banButton.addEventListener('click', async () => {
    banButton.disabled = true;
    try {
      if (banned) await unbanAccount(level.author, reason.value);
      else await banAccount(level.author, reason.value);
      wsBans = await loadBans().catch(() => ({}));
      notice('Готово.');await loadWorkshopCatalog();
    } catch (error) {setCatalogMessage(error.message);}
    finally {banButton.disabled = false;}
  });
  wrap.append(reason, banButton);
  return wrap;
}
async function renderComments(levelId, panel) {
  panel.replaceChildren();
  const loading = document.createElement('p');loading.className = 'muted';
  loading.textContent = 'Загружаем сообщения…';panel.append(loading);
  let comments;
  try {
    comments = (await loadComments(levelId))
      .filter(comment => !comment.hidden && !isBanned(wsBans, comment.login));
  } catch (error) {
    panel.replaceChildren();
    const note = document.createElement('p');note.className = 'muted';
    note.textContent = `Сообщения не загрузились: ${error.message}`;panel.append(note);return;
  }
  panel.replaceChildren();
  if (!comments.length) {
    const note = document.createElement('p');note.className = 'muted';
    note.textContent = 'Сообщений пока нет.';panel.append(note);
  }
  for (const comment of comments) panel.append(commentNode(levelId, comment, panel));
  if (currentSession()) panel.append(commentForm(levelId, panel));
  else {
    const note = document.createElement('p');note.className = 'muted';
    note.textContent = 'Писать сообщения могут только игроки с аккаунтом.';
    panel.append(note);
  }
}
function commentNode(levelId, comment, panel) {
  const row = document.createElement('div');row.className = 'ws-comment';
  const author = document.createElement('strong');author.textContent = comment.login;
  const text = document.createElement('span');text.textContent = comment.text;
  row.append(author, text);
  const active = currentSession();
  if (active && (active.admin || active.login === comment.login)) {
    const hide = document.createElement('button');hide.type = 'button';
    hide.className = 'ws-comment-hide';hide.textContent = 'Скрыть';
    hide.addEventListener('click', async () => {
      hide.disabled = true;
      try {await hideComment(levelId, comment.id);await renderComments(levelId, panel);}
      catch (error) {setCatalogMessage(error.message);}
      finally {hide.disabled = false;}
    });
    row.append(hide);
  }
  return row;
}
function commentForm(levelId, panel) {
  const form = document.createElement('form');form.className = 'ws-comment-form';
  const input = document.createElement('input');
  input.type = 'text';input.maxLength = MAX_COMMENT;input.placeholder = 'Сообщение под уровнем';
  const submit = document.createElement('button');
  submit.type = 'submit';submit.className = 'small-button';submit.textContent = 'Отправить';
  form.addEventListener('submit', async event => {
    event.preventDefault();
    submit.disabled = true;
    try {await postComment(levelId, input.value);input.value = '';await renderComments(levelId, panel);}
    catch (error) {setCatalogMessage(error.message);}
    finally {submit.disabled = false;}
  });
  form.append(input, submit);
  return form;
}
async function loadWorkshopCatalog() {
  const generationId = ++wsCatalogGeneration;
  setCatalogMessage('');
  const list = $('ws-catalog-list');list.replaceChildren();
  const loading = document.createElement('p');loading.className = 'muted';loading.textContent = 'Загружаем каталог…';list.append(loading);
  try {
    const [levels, bans] = await Promise.all([listPublishedLevels(statsClientId),
      loadBans().catch(() => ({}))]);
    if (generationId !== wsCatalogGeneration || wsPage !== 'catalog') return;
    wsCatalog = levels;wsBans = bans;updateCatalogTagOptions();renderFilteredCatalog();
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
    const summary = wsCatalog.find(level => level.id === id);
    if (summary && !summary.downloaded) {
      try {
        const counted = await recordLevelDownload(id, statsClientId);
        summary.downloaded = true;
        if (counted) summary.downloads = (summary.downloads || 0) + 1;
      } catch (error) {
        /* Statistics never block access to a successfully fetched level. */
        setCatalogMessage(wsText('Не удалось обновить счётчик скачиваний.',
          'Could not update the download counter.'));
      }
    }
    const level = draftFromPublished(record);
    startWorkshopPreview(level, record.title, 'catalog', isOfficialLevel(record.id, record));
  } catch (error) {
    setCatalogMessage(error.status === 401 || error.status === 403 ?
      wsText('Firebase запретил чтение /levels. Проверь правила базы.',
        'Firebase denied access to /levels. Check the database rules.') : error.message);
  } finally {button.disabled = false;}
}
function startWorkshopPreview(level, title, returnPage, official = false) {
  const check = validateDraft(level);
  if (!check.ok) {showWorkshopMessage(check.message);return;}
  wsPreviewLevel = level;wsPreviewState = createPreviewState(level);
  wsPreviewReturn = returnPage;wsWinAnnounced = false;
  const previewTitle = $('ws-preview-title');
  previewTitle.replaceChildren(document.createTextNode(title || level.title));
  if (official) {
    const badge = document.createElement('span');badge.className = 'ws-official-badge ws-official-badge-heading';
    badge.textContent = 'ОФИЦИАЛЬНЫЙ';badge.setAttribute('aria-label', 'Официальный уровень');
    previewTitle.append(' ', badge);
  }
  $('ws-preview-status').textContent = '';
  $('ws-preview-back').textContent = returnPage === 'catalog' ? 'Назад · Каталог' : 'Назад · В редактор';
  clearWorkshopInput();applyWorkshopControl(wsControlPreference);
  setWorkshopPage('preview');
}
function drawCurrentPreview() {
  if (wsPreviewState) drawPreviewCanvas($('ws-preview-canvas'), wsPreviewState, wsControlMode, wsArt);
}
function updateWorkshopJetpackControls() {
  const jetpack = !!wsPreviewState?.jetpack;
  for (const button of document.querySelectorAll('[data-ws-normal-control]'))
    button.classList.toggle('hidden', jetpack);
  for (const button of document.querySelectorAll('[data-ws-jetpack-control]'))
    button.classList.toggle('hidden', !jetpack);
  const dash = $('ws-dash-control');
  dash.classList.toggle('hidden', jetpack || wsPreviewState?.abilities?.dash !== true);
}
function previewJumpPointerDown(event) {
  if (screen !== 'workshop' || wsPage !== 'preview' || !wsPreviewState) return;
  event.preventDefault();
  if (!wsPreviewState.jetpack) wsJumpQueued = true;
}
function setAccountMessage(message = '') {
  const node = $('ws-account-message');
  node.textContent = message;node.classList.toggle('hidden', !message);
}
function renderAccountStatus() {
  const active = currentSession();
  const status = $('ws-account-status');
  const button = $('ws-account-open');
  status.textContent = active ?
    active.admin ? `${active.login} · модератор` : active.login : 'Гость';
  status.classList.toggle('signed-out', !active);
  button.textContent = active ? 'Аккаунт' : 'Войти';
  button.setAttribute('aria-label', active ? `Аккаунт ${active.login}` : 'Войти в аккаунт');
  $('ws-account-signout').classList.toggle('hidden', !active);
  if (active && !$('ws-account-login').value)
    $('ws-account-login').value = active.login;
}
function openAccountDialog() {
  setAccountMessage('');
  const login = $('ws-account-login');
  if (!login.value) {
    const active = currentSession();
    let remembered = '';
    try {remembered = localStorage.getItem(WS_ACCOUNT_LOGIN_KEY) || '';} catch {}
    login.value = active?.login || remembered;
  }
  $('ws-account-dialog').showModal();
  const focus = login.value ? $('ws-account-password') : login;
  focus.focus({preventScroll: true});
}
async function submitAccount(kind) {
  const login = $('ws-account-login').value, password = $('ws-account-password').value;
  const button = $(kind === 'create' ? 'ws-account-create' : 'ws-account-signin');
  button.disabled = true;setAccountMessage('');
  try {
    const session = kind === 'create' ?
      await createAccount(login, password) : await signIn(login, password);
    try {localStorage.setItem(WS_ACCOUNT_LOGIN_KEY, session.login);} catch {}
    $('ws-account-password').value = '';
    $('ws-account-dialog').close();
    notice(`Вход выполнен · ${session.login}`);
    if (wsPage === 'catalog') await loadWorkshopCatalog();
    if (publishAfterAccount) {
      publishAfterAccount = false;
      await publishWorkshopDraft();
    }
  } catch (error) {
    setAccountMessage(error.message);
  } finally {button.disabled = false;}
}
function applyWorkshopTutorial(enabled) {
  wsTutorialHints = enabled === true;
  try {localStorage.setItem(WS_TUTORIAL_KEY, wsTutorialHints ? '1' : '0');} catch {}
  const box = $('ws-tutorial-toggle');
  if (box) box.checked = wsTutorialHints;
  for (const tools of document.querySelectorAll('.ws-tools'))
    tools.classList.toggle('hints-hidden', !wsTutorialHints);
}
function applyWorkshopControl(preference) {
  wsControlPreference = ['auto', 'buttons', 'keyboard'].includes(preference) ? preference : 'auto';
  try {localStorage.setItem(WS_CONTROL_KEY, wsControlPreference);} catch {}
  const coarsePointer = !!(window.matchMedia && window.matchMedia('(pointer: coarse)').matches);
  wsControlMode = resolveControlMode(wsControlPreference === 'auto' ? '' : wsControlPreference, coarsePointer);
  $('ws-control-select').value = wsControlPreference;
  $('ws-buttons-controls').classList.toggle('hidden', wsControlMode !== 'buttons');
  clearWorkshopInput();updateWorkshopJetpackControls();drawCurrentPreview();
}
function clearWorkshopInput() {
  wsTouchButtons.clear();
  wsJumpQueued = wsDashQueued = wsTriggerQueued = false;wsKeys.clear();
}
function resetWorkshopPreview() {
  if (!wsPreviewLevel) return;
  wsPreviewState = createPreviewState(wsPreviewLevel);wsWinAnnounced = false;
  $('ws-preview-status').textContent = '';
  clearWorkshopInput();updateWorkshopJetpackControls();drawCurrentPreview();
}
function workshopFrame(dt) {
  if (screen !== 'workshop' || wsPage !== 'preview' || !wsPreviewState) return;
  const axis = wsControlMode === 'buttons' ? wsTouchButtons.axis :
    Number(wsKeys.has('d') || wsKeys.has('arrowright')) - Number(wsKeys.has('a') || wsKeys.has('arrowleft'));
  const vertical = wsPreviewState.jetpack ?
    wsControlMode === 'buttons' ? wsTouchButtons.vertical :
      Number(wsKeys.has('arrowup') || wsKeys.has('w')) -
      Number(wsKeys.has('arrowdown') || wsKeys.has('s')) : 0;
  const jump = !wsPreviewState.jetpack && (wsJumpQueued ||
    (wsControlMode === 'keyboard' &&
      (wsKeys.has(' ') || wsKeys.has('arrowup') || wsKeys.has('w'))));
  const dash = wsDashQueued || (wsControlMode === 'keyboard' &&
    (wsKeys.has('shift') || wsKeys.has('x')));
  const trigger = wsTriggerQueued || (wsControlMode === 'keyboard' && wsKeys.has('e'));
  wsJumpQueued = wsDashQueued = wsTriggerQueued = false;
  stepPreview(wsPreviewState, {axis, vertical, jump, dash, trigger}, dt);
  updateWorkshopJetpackControls();
  if (wsPreviewState.orbActivated)
    $('ws-preview-status').textContent = wsText('Орб активирован!', 'Orb activated!');
  drawCurrentPreview();
  if (wsPreviewState.won && !wsWinAnnounced) {
    wsWinAnnounced = true;$('ws-preview-status').textContent =
      wsText(`Уровень пройден! Собрано монет: ${wsPreviewState.coins}.`,
        `Level complete! Coins collected: ${wsPreviewState.coins}.`);
    notice(wsText('Уровень пройден!', 'Level complete!'));
  }
}
async function publishWorkshopDraft() {
  const check = validateDraft(wsDraft);
  if (!check.ok) {showWorkshopMessage(check.message);return;}
  const active = currentSession();
  if (!active) {
    publishAfterAccount = true;
    showWorkshopMessage('Для публикации войдите или создайте аккаунт. После входа уровень опубликуется сам.');
    openAccountDialog();
    return;
  }
  const button = $('ws-publish');button.disabled = true;
  showWorkshopMessage('');
  try {
    const result = await publishLevel(wsDraft,
      {login: active.login, tok: active.token});
    /* The public record briefly carries this token; retire it right away. */
    let tokenRetired = false;
    try {tokenRetired = !!(await rotateToken());} catch { /* surface below */ }
    wsDraft.publishedId = result.id;wsDraft.publishedAt = Date.now();
    saveWorkshopDraft();
    const message = `Уровень опубликован под ID ${result.id}. Он доступен в каталоге и нативной игре.` +
      (tokenRetired ? '' : ' Не удалось сразу заменить токен сессии — проверь интернет и войди заново.');
    showWorkshopMessage(message);
    notice(`Уровень опубликован · ID ${result.id}`, 6000);
  } catch (error) {
    let tokenWarning = '';
    if (error.levelWriteAttempted) {
      if (error.levelWritten) {
        wsDraft.publishedId = error.levelId;wsDraft.publishedAt = Date.now();
        saveWorkshopDraft();
      }
      let tokenRetired = false;
      try {tokenRetired = !!(await rotateToken());} catch { /* warn below */ }
      if (!tokenRetired)
        tokenWarning = ' Не удалось заменить токен сессии — проверь интернет и войди заново.';
    }
    const baseMessage = error.message.startsWith('Уровень ') ? error.message :
      error.status === 401 || error.status === 403 ?
      'Firebase отклонил запись. Для публикации правила должны разрешать создание записей в /levels и /levels-index. Правила базы автоматически не менялись.' :
      error.message;
    showWorkshopMessage(baseMessage + tokenWarning);
  } finally {button.disabled = false;}
}
async function openWorkshop() {
  await wsDraftReady;
  setWorkshopPage('home');
}
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
  const mine = room[slot]?.role, otherSlot = slot === 'host' ? 'guest' : 'host';
  const other = room[otherSlot]?.role;
  for (const card of document.querySelectorAll('.side-card')) {
    const role = card.dataset.side;
    card.classList.toggle('selected', mine === role);
    card.disabled = !!room.state || (other === role && mine !== role);
  }
}
function enterMatch() {
  if (screen === 'match') return;
  show('match');
  $('match-code').textContent = roomId;
  $('match-title').textContent = room?.[slot]?.role === 'plants' ? 'Защити Хлебушка' : 'Прорви защиту Кирилла';
  $('finish-wave').classList.toggle('hidden', room?.[slot]?.role !== 'zombies');
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
  if (screen === 'workshop') {
    workshopFrame(dt);
    if (wsPage === 'editor' && wsDraft.objects.some(object =>
        object.visible !== false && (object.pulse === true || object.shake === true)))
      drawEditorCanvas($('ws-editor-canvas'), wsDraft, wsSelectedIds, wsTool,
                       wsArt, wsCamera, now / 1000);
  }
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
$('ws-catalog-search').addEventListener('input', renderFilteredCatalog);
$('ws-catalog-difficulty').addEventListener('change', renderFilteredCatalog);
$('ws-catalog-tag').addEventListener('change', renderFilteredCatalog);
$('ws-catalog-sort').addEventListener('change', renderFilteredCatalog);
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
for (const button of document.querySelectorAll('[data-ws-tool]'))
  button.addEventListener('click', () => setWorkshopTool(button.dataset.wsTool));
for (const button of document.querySelectorAll('[data-ws-type]')) button.addEventListener('click', () => {
  wsType = button.dataset.wsType;
  if (wsType === 'trigger') wsTriggerKind = 'move';
  if (wsType === 'block') wsBlockType = 'block';
  if (wsType === 'orb') wsOrbType = 'orb-yellow';
  if (wsType === 'goal') wsGoalType = 'goal';
  if (wsType === 'portal') wsPortalType = 'portal-normal';
  wsPaletteSelected = false;wsTool = 'build';
  renderWorkshopEditor();
});
$('ws-preview-canvas').addEventListener('pointerdown', previewJumpPointerDown);
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
$('ws-title-input').addEventListener('input', event => {
  wsDraft.title = event.target.value;showWorkshopMessage('');saveWorkshopDraft();
});
$('ws-description-input').addEventListener('input', event => {
  wsDraft.description = event.target.value;saveWorkshopDraft();
});
$('ws-difficulty-input').addEventListener('change', event => {
  wsDraft.difficulty = event.target.value;saveWorkshopDraft();
});
$('ws-tags-input').addEventListener('input', event => {
  wsDraft.tags = event.target.value.split(',').map(tag => tag.trim()).filter(Boolean).slice(0, 9);
  saveWorkshopDraft();
});
const movementKeys = {
  'ws-ability-double-jump': 'doubleJump',
  'ws-ability-dash': 'dash',
  'ws-ability-wall-slide': 'wallSlide',
};
for (const [id, key] of Object.entries(movementKeys))
  $(id).addEventListener('change', event => {
    wsDraft.movement ||= {};
    wsDraft.movement[key] = event.target.checked;
    saveWorkshopDraft();
  });
$('ws-object-x').addEventListener('change', e => updateSelectedProperty('x', e.target.value));
$('ws-object-y').addEventListener('change', e => updateSelectedProperty('y', e.target.value));
$('ws-object-width').addEventListener('change', e => updateSelectedProperty('width', e.target.value));
$('ws-object-height').addEventListener('change', e => updateSelectedProperty('height', e.target.value));
$('ws-object-angle').addEventListener('change', e => updateSelectedProperty('angle', e.target.value));
$('ws-object-number').addEventListener('change', e => updateSelectedProperty('number', e.target.value));
$('ws-object-color').addEventListener('input', e => updateSelectedProperty('color', e.target.value));
$('ws-object-color-default').addEventListener('change', e =>
  updateSelectedProperty('defaultColor', e.target.checked));
$('ws-particle-settings').addEventListener('click', openParticleDialog);
for (const id of ['ws-particle-close', 'ws-particle-close-bottom'])
  $(id).addEventListener('click', () => $('ws-particle-dialog').close());
$('ws-particle-reset').addEventListener('click', () => {
  const object = wsObject(wsParticleDialogObjectId);
  if (!object || object.type !== 'particle') return;
  object.emitter = normalizeParticleEmitter();
  updateParticleDialogFields(object.emitter);wsRedrawEditor();saveWorkshopDraft();
});
$('ws-particle-dialog').addEventListener('close', () => {
  wsParticleDialogObjectId = 0;
  if (wsParticleAnimation) cancelAnimationFrame(wsParticleAnimation);
  wsParticleAnimation = 0;
});
for (const input of document.querySelectorAll('[data-emitter-key]')) {
  const eventName = input.type === 'checkbox' ? 'change' : 'input';
  input.addEventListener(eventName, () =>
    updateSelectedParticleEmitter(input.dataset.emitterKey,
      input.type === 'checkbox' ? input.checked : input.value));
}
$('ws-trigger-kind').addEventListener('change', e => updateSelectedTrigger('kind', e.target.value));
$('ws-trigger-event').addEventListener('change', e => updateSelectedTrigger('event', e.target.value));
$('ws-trigger-touch-mode').addEventListener('change', e => updateSelectedTrigger('touchMode', e.target.value));
$('ws-trigger-count').addEventListener('change', e => updateSelectedTrigger('count', e.target.value));
$('ws-trigger-group').addEventListener('change', e => updateSelectedTrigger('groupId', e.target.value));
$('ws-trigger-forever-group').addEventListener('change', e => updateSelectedTrigger('groupId', e.target.value));
$('ws-trigger-x').addEventListener('change', e => updateSelectedTrigger('valueX', e.target.value));
$('ws-trigger-y').addEventListener('change', e => updateSelectedTrigger('valueY', e.target.value));
$('ws-trigger-duration').addEventListener('change', e => updateSelectedTrigger('duration', e.target.value));
$('ws-trigger-gravity').addEventListener('input', e => updateSelectedTrigger('gravity', e.target.value));
$('ws-trigger-color').addEventListener('input', e => updateSelectedTrigger('color', e.target.value));
$('ws-trigger-action').addEventListener('change', e => updateSelectedTrigger('action', e.target.value));
$('ws-control-select').addEventListener('change', e => applyWorkshopControl(e.target.value));
$('ws-tutorial-toggle').addEventListener('change', e => applyWorkshopTutorial(e.target.checked));
$('ws-account-open').addEventListener('click', openAccountDialog);
$('ws-account-close').addEventListener('click', () => {
  publishAfterAccount = false;
  $('ws-account-dialog').close();
});
$('ws-account-dialog').addEventListener('cancel', () => {publishAfterAccount = false;});
$('ws-account-form').addEventListener('submit', event => {
  event.preventDefault();submitAccount('signin');
});
$('ws-account-create').addEventListener('click', () => submitAccount('create'));
$('ws-account-signout').addEventListener('click', async () => {
  signOut();notice('Выход выполнен.');
  if (wsPage === 'catalog') await loadWorkshopCatalog();
});
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
  const action = button.dataset.wsPress;
  const press = () => {
    if (action === 'jump') wsJumpQueued = true;
    else if (action === 'dash') wsDashQueued = true;
    else wsTriggerQueued = true;
  };
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
  if (['arrowleft', 'arrowright', 'arrowup', 'arrowdown', ' ',
       'a', 'd', 'w', 's', 'e', 'x', 'shift'].includes(key)) event.preventDefault();
  wsKeys.add(key);
  if (!wsPreviewState?.jetpack && !event.repeat &&
      ['arrowup', ' ', 'w'].includes(key)) wsJumpQueued = true;
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
window.addEventListener('pagehide', flushWorkshopDraft);
$('music').addEventListener('click', async () => {
  const audio = $('soundtrack');
  if (audio.paused) {
    try {await audio.play();$('music').textContent = 'Выключить музыку';}
    catch {notice('Браузер не разрешил воспроизвести музыку.');}
  } else {audio.pause();$('music').textContent = 'Музыка';}
});
canvas.addEventListener('pointerdown', pointer);
applyWorkshopTutorial(wsTutorialHints);
onSessionChange(renderAccountStatus);renderAccountStatus();
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
