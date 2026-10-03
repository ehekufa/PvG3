import {PLANTS, DUCKS, W, H, X, Y, CW, CH, ROWS, COLS,
        newMatch, validMatch, applyCommand, stepMatch} from './rules.js';
import {DATABASE, validId, randomPlayerId, listRooms, getRoom, createRoom,
        joinRoom, chooseRole, writeState, writeCommand, heartbeat, leaveRoom,
        listPublishedLevels, getPublishedLevel, publishLevel} from './firebase.js';
import {preloadArtwork, drawGame} from './draw.js';
import {LEVEL_WIDTH, LEVEL_HEIGHT, MAX_LEVEL_OBJECTS, TYPE_LABELS, newDraft,
        addObject, findObjectAt, validateDraft, draftFromPublished,
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
let wsSelectedId = 0, wsDrag = null, wsCatalogGeneration = 0, wsCatalog = [];
let wsPreviewState = null, wsPreviewLevel = null, wsPreviewReturn = 'editor';
let wsControlPreference = localSetting(WS_CONTROL_KEY, 'auto'), wsControlMode = 'keyboard';
let wsJumpQueued = false, wsTriggerQueued = false;
const wsTouchButtons = createTouchButtonState();
const wsKeys = new Set();
let wsWinAnnounced = false;
const WS_ART_FILES = {
  block: 'Блок.png', ground: 'Платформа.png', hazard: 'Шип.png',
  coin: 'coin-token.png', enemy: 'zombie-duck.png', player: 'khlebushek.png',
  goal: 'Флажок - финиш.png', trigger: 'Триггер-движения.png',
};
const wsArt = Object.fromEntries(Object.entries(WS_ART_FILES).map(([type, file]) => {
  const image = new Image();
  image.decoding = 'async';
  image.addEventListener('load', () => {
    if (screen !== 'workshop') return;
    if (wsPage === 'editor')
      drawEditorCanvas($('ws-editor-canvas'), wsDraft, wsSelectedId, wsTool, wsArt);
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
    if (validShape) return saved;
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
  $('ws-draft-summary').textContent = `${wsDraft.objects.length} объектов · сетка ${LEVEL_WIDTH} × ${LEVEL_HEIGHT} · ` +
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
function renderWorkshopEditor() {
  $('ws-title-input').value = wsDraft.title;
  $('ws-description-input').value = wsDraft.description;
  $('ws-object-count').textContent = `${wsDraft.objects.length} / ${MAX_LEVEL_OBJECTS} объектов`;
  for (const button of document.querySelectorAll('[data-ws-tool]'))
    button.classList.toggle('active', button.dataset.wsTool === wsTool);
  for (const button of document.querySelectorAll('[data-ws-type]'))
    button.classList.toggle('active', button.dataset.wsType === wsType);
  renderSelectedObject();
  drawEditorCanvas($('ws-editor-canvas'), wsDraft, wsSelectedId, wsTool, wsArt);
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
function renderTriggerTargets(trigger) {
  const select = $('ws-trigger-target');
  select.replaceChildren();
  const none = document.createElement('option');none.value = '0';none.textContent = 'Нет цели';select.append(none);
  for (const object of wsDraft.objects) {
    if (object.id === trigger.id || object.type === 'trigger') continue;
    const option = document.createElement('option');option.value = String(object.id);
    option.textContent = `${TYPE_LABELS[object.type]} · ${object.id}`;select.append(option);
  }
  select.value = String(trigger.trigger?.targetId || 0);
}
function renderSelectedObject() {
  const object = wsObject(wsSelectedId);
  const panel = $('ws-properties');
  panel.classList.toggle('hidden', !object);
  if (!object) return;
  $('ws-selected-label').textContent = `${TYPE_LABELS[object.type]} · ID ${object.id}`;
  $('ws-object-x').value = Number(object.x.toFixed(2));
  $('ws-object-y').value = Number(object.y.toFixed(2));
  $('ws-object-number').value = object.number || 0;
  $('ws-object-color').value = object.color;
  const triggerFields = $('ws-trigger-fields');
  triggerFields.classList.toggle('hidden', object.type !== 'trigger');
  if (object.type === 'trigger') {
    renderTriggerTargets(object);
    $('ws-trigger-event').value = object.trigger.event;
    $('ws-trigger-action').value = object.trigger.action;
    $('ws-trigger-value').value = object.trigger.value;
  }
}
function wsRedrawEditor() {
  renderSelectedObject();
  drawEditorCanvas($('ws-editor-canvas'), wsDraft, wsSelectedId, wsTool, wsArt);
  $('ws-object-count').textContent = `${wsDraft.objects.length} / ${MAX_LEVEL_OBJECTS} объектов`;
}
function setWorkshopTool(tool) {
  if (!['build', 'select', 'delete'].includes(tool)) return;
  wsTool = tool;
  for (const button of document.querySelectorAll('[data-ws-tool]'))
    button.classList.toggle('active', button.dataset.wsTool === tool);
  wsRedrawEditor();
}
function wsPoint(event) {
  const rect = $('ws-editor-canvas').getBoundingClientRect();
  return {x: (event.clientX - rect.left) * LEVEL_WIDTH / rect.width,
    y: (event.clientY - rect.top) * LEVEL_HEIGHT / rect.height};
}
function wsPointerDown(event) {
  if (screen !== 'workshop' || wsPage !== 'editor') return;
  event.preventDefault();
  const point = wsPoint(event);
  const object = findObjectAt(wsDraft, point.x, point.y);
  showWorkshopMessage('');
  if (wsTool === 'build') {
    const placed = addObject(wsDraft, wsType, Math.floor(point.x), Math.floor(point.y));
    if (!placed) {
      showWorkshopMessage('Достигнут лимит 120 объектов. Удали лишние объекты перед добавлением новых.');return;
    }
    // Player and finish are singletons: addObject moves the existing object.
    wsSelectedId = placed.id;
    saveWorkshopDraft();wsRedrawEditor();return;
  }
  if (wsTool === 'delete') {
    if (!object) {wsSelectedId = 0;wsRedrawEditor();return;}
    wsDraft.objects = wsDraft.objects.filter(o => o.id !== object.id);
    wsSelectedId = 0;saveWorkshopDraft();wsRedrawEditor();return;
  }
  wsSelectedId = object?.id || 0;
  if (object) {
    wsDrag = {pointerId: event.pointerId, id: object.id, startX: event.clientX,
      startY: event.clientY, x: object.x, y: object.y, moved: false};
    $('ws-editor-canvas').setPointerCapture?.(event.pointerId);
  }
  wsRedrawEditor();
}
function wsPointerMove(event) {
  if (!wsDrag || event.pointerId !== wsDrag.pointerId) return;
  const object = wsObject(wsDrag.id);
  if (!object) return;
  const rect = $('ws-editor-canvas').getBoundingClientRect();
  const dx = (event.clientX - wsDrag.startX) * LEVEL_WIDTH / rect.width;
  const dy = (event.clientY - wsDrag.startY) * LEVEL_HEIGHT / rect.height;
  if (Math.abs(dx) + Math.abs(dy) > .025) wsDrag.moved = true;
  object.x = Math.max(0, Math.min(LEVEL_WIDTH - object.w, wsDrag.x + dx));
  object.y = Math.max(0, Math.min(LEVEL_HEIGHT - object.h, wsDrag.y + dy));
  wsRedrawEditor();
}
function wsPointerUp(event) {
  if (!wsDrag || (event && event.pointerId !== wsDrag.pointerId)) return;
  const moved = wsDrag.moved;wsDrag = null;
  if (moved) {saveWorkshopDraft();wsRedrawEditor();}
}
function updateSelectedProperty(property, value) {
  const object = wsObject(wsSelectedId);
  if (!object) return;
  if (property === 'x' || property === 'y') {
    const n = Number(value);
    if (!Number.isFinite(n)) return;
    object[property] = Math.max(0, Math.min((property === 'x' ? LEVEL_WIDTH : LEVEL_HEIGHT) - object[property === 'x' ? 'w' : 'h'], n));
  } else if (property === 'number') {
    const n = Math.trunc(Number(value));if (!Number.isFinite(n)) return;
    object.number = Math.max(0, Math.min(9999, n));
  } else if (property === 'color') object.color = value;
  wsRedrawEditor();saveWorkshopDraft();
}
function updateSelectedTrigger(property, value) {
  const object = wsObject(wsSelectedId);
  if (!object || object.type !== 'trigger') return;
  if (property === 'targetId') object.trigger.targetId = Number(value) || 0;
  else if (property === 'value') {
    const n = Number(value);if (!Number.isFinite(n)) return;
    object.trigger.value = Math.max(-100, Math.min(100, n));
  } else object.trigger[property] = value;
  wsRedrawEditor();saveWorkshopDraft();
}
function beginNewDraft() {
  if (!confirm('Создать новый уровень вместо текущего черновика? Опубликованные уровни не затрагиваются.')) return;
  wsDraft = newDraft();wsSelectedId = 0;wsTool = 'build';wsType = 'block';
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
    const button = document.createElement('button');button.type = 'button';button.textContent = 'Играть →';
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
  $('ws-preview-back').textContent = returnPage === 'catalog' ? '← Каталог' : '← В редактор';
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
    'Клавиши A / D или ← / → для движения, пробел или ↑ для прыжка, E для действия.' :
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
      tag.textContent = 'войти →';
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
  $('plants-taken').textContent = mine === 'plants' ? 'ТВОЯ СТОРОНА ✓' : other === 'plants' ? 'Занято соперником' : 'Выбрать растения →';
  $('zombies-taken').textContent = mine === 'zombies' ? 'ТВОЯ СТОРОНА ✓' : other === 'zombies' ? 'Занято соперником' : 'Выбрать зомби →';
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
  wsType = button.dataset.wsType;setWorkshopTool('build');
  for (const other of document.querySelectorAll('[data-ws-type]'))
    other.classList.toggle('active', other === button);
});
$('ws-editor-canvas').addEventListener('pointerdown', wsPointerDown);
$('ws-editor-canvas').addEventListener('pointermove', wsPointerMove);
$('ws-editor-canvas').addEventListener('pointerup', wsPointerUp);
$('ws-editor-canvas').addEventListener('pointercancel', wsPointerUp);
$('ws-editor-canvas').addEventListener('lostpointercapture', wsPointerUp);
$('ws-delete-selected').addEventListener('click', () => {
  if (!wsObject(wsSelectedId)) return;
  wsDraft.objects = wsDraft.objects.filter(o => o.id !== wsSelectedId);
  wsSelectedId = 0;saveWorkshopDraft();wsRedrawEditor();
});
$('ws-object-x').addEventListener('change', e => updateSelectedProperty('x', e.target.value));
$('ws-object-y').addEventListener('change', e => updateSelectedProperty('y', e.target.value));
$('ws-object-number').addEventListener('change', e => updateSelectedProperty('number', e.target.value));
$('ws-object-color').addEventListener('input', e => updateSelectedProperty('color', e.target.value));
$('ws-trigger-event').addEventListener('change', e => updateSelectedTrigger('event', e.target.value));
$('ws-trigger-action').addEventListener('change', e => updateSelectedTrigger('action', e.target.value));
$('ws-trigger-target').addEventListener('change', e => updateSelectedTrigger('targetId', e.target.value));
$('ws-trigger-value').addEventListener('change', e => updateSelectedTrigger('value', e.target.value));
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
  if (screen !== 'workshop' || wsPage !== 'preview' || wsControlMode !== 'keyboard') return;
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
    try {await audio.play();$('music').textContent = '♫ Выкл. музыку';}
    catch {notice('Браузер не разрешил воспроизвести музыку.');}
  } else {audio.pause();$('music').textContent = '♫ Музыка';}
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
