import {
  MAX_OBJECTS, OBJECT_TYPES, TEMPLATES, createProject, duplicateProject, makeObject,
  nextObjectId, parseProject, serializeProject,
} from './maker-core.js';

const $ = id => document.getElementById(id);
const STORAGE_KEY = 'pvg3-maker-project-v1';
const STORAGE_PLACE = window.PvG3Native ? 'в приложении' : 'в этом браузере';
const TILE = 60;
const canvas = $('stage');
const ctx = canvas.getContext('2d');
const dialog = $('template-dialog');
let project = null;
let activeTab = 'level';
let activeTool = 'select';
let selectedId = null;
let selectedTemplate = 'classic';
let dragging = null;
let isPlaying = false;
let playObjects = null;
let player = null;
let playWon = false;
let lastFrame = performance.now();
let saveTimer = 0;
let toastTimer = 0;
let manualPressed = false;
const keys = new Set();
const firedTriggers = new Set();
if (window.PvG3Native) {
  const backLink = document.querySelector('.back-link');
  if (backLink) backLink.textContent = '← В игру';
}

const TYPE_GLYPHS = {
  block: '▦', ground: '▰', hazard: '▲', coin: '●', enemy: '●',
  player: '◆', goal: '⚑', trigger: '⚡',
};
const TAB_HINTS = {
  level: 'Ставь блоки на сетку, выбирай объект и редактируй его справа.',
  objects: 'Перетаскивай объекты свободно; меняй размер, цвет и угол поворота.',
  triggers: 'Поставь триггер, выбери цель по ID и настрой событие с действием.',
};

function toast(message, duration = 3200) {
  const el = $('maker-toast');
  el.textContent = message;
  el.classList.remove('hidden');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.add('hidden'), duration);
}

function saveLabel(message, error = false) {
  $('save-label').textContent = message;
  $('save-state').classList.toggle('error', error);
}

function persistNow() {
  if (!project) return;
  clearTimeout(saveTimer);
  try {
    localStorage.setItem(STORAGE_KEY, serializeProject(project));
    saveLabel(`Сохранено ${STORAGE_PLACE} · ${new Date().toLocaleTimeString('ru-RU', {hour:'2-digit', minute:'2-digit'})}`);
  } catch {
    saveLabel('Автосохранение недоступно — скачай TXT-файл', true);
  }
}

function scheduleSave() {
  if (!project) return;
  saveLabel('Сохраняем…');
  clearTimeout(saveTimer);
  saveTimer = setTimeout(persistNow, 180);
}

function readLocalProject() {
  try {
    const text = localStorage.getItem(STORAGE_KEY);
    return text ? parseProject(text) : null;
  } catch (error) {
    saveLabel('Не удалось восстановить автосохранение', true);
    return null;
  }
}

function activeObjects() {
  return isPlaying && playObjects ? playObjects : project?.objects || [];
}

function selectedObject() {
  return project?.objects.find(item => item.id === selectedId) || null;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function roundStep(value, step) {
  return Math.round(value / step) * step;
}

function updateProjectHeader() {
  if (!project) return;
  $('project-title').value = project.title;
  $('template-name').textContent = TEMPLATES.find(item => item.id === project.templateId)?.title || 'Платформер';
  $('stage-kicker').textContent = `СЦЕНА · ${project.width} × ${project.height}`;
}

function syncToolButtons() {
  document.querySelectorAll('[data-tool]').forEach(button =>
    button.classList.toggle('active', button.dataset.tool === activeTool));
}

function setTool(tool) {
  activeTool = tool;
  syncToolButtons();
  if (tool === 'erase') $('palette-note').textContent = 'Щёлкни объект на сцене, чтобы удалить его.';
  else if (tool === 'select') $('palette-note').textContent = activeTab === 'objects'
    ? 'Выбери и перетаскивай объект свободно. Вращение и размер — в инспекторе.'
    : 'Выбери объект на сцене, чтобы двигать его по сетке.';
  else $('palette-note').textContent = `Щёлкай по уровню, чтобы ставить: ${OBJECT_TYPES[tool]?.name || 'объект'}.`;
}

function setTab(tab) {
  if (activeTab === 'triggers' && tab !== 'triggers' && activeTool === 'trigger') activeTool = 'select';
  activeTab = tab;
  document.querySelectorAll('.maker-tabs [data-tab]').forEach(button => {
    const active = button.dataset.tab === tab;
    button.classList.toggle('active', active);
    button.setAttribute('aria-selected', String(active));
  });
  $('tab-hint').textContent = TAB_HINTS[tab];
  $('palette-label').textContent = tab === 'triggers' ? 'ЛОГИКА УРОВНЯ' : 'ОБЪЕКТЫ УРОВНЯ';
  document.querySelectorAll('.object-tool').forEach(button => {
    button.classList.toggle('hidden', tab === 'triggers' && button.dataset.tool !== 'trigger');
  });
  $('list-wrap').classList.toggle('hidden', tab === 'level');
  $('list-title').textContent = tab === 'triggers' ? 'ТРИГГЕРЫ' : 'ОБЪЕКТЫ';
  $('stage-title').textContent = tab === 'triggers' ? 'Схема логики' : tab === 'objects' ? 'Свободная расстановка' : 'Поле уровня';
  if (tab === 'triggers' && activeTool !== 'select' && activeTool !== 'erase' && activeTool !== 'trigger')
    setTool('select');
  else setTool(activeTool);
  updateObjectList();
  renderInspector();
  draw();
}

function openTemplateDialog() {
  const current = project?.templateId || selectedTemplate;
  selectedTemplate = TEMPLATES.some(item => item.id === current) ? current : 'classic';
  document.querySelectorAll('[data-template]').forEach(card =>
    card.classList.toggle('active', card.dataset.template === selectedTemplate));
  if (!dialog.open) dialog.showModal();
}

function beginNewProject(templateId) {
  project = createProject(templateId);
  selectedTemplate = templateId;
  selectedId = null;
  activeTab = 'level';
  activeTool = 'select';
  setTab('level');
  updateProjectHeader();
  renderInspector();
  updateObjectList();
  scheduleSave();
  if (dialog.open) dialog.close();
  toast(`Создан шаблон «${$('template-name').textContent}». Теперь можно менять всё содержимое.`);
  draw();
}

function renderObjectList() {
  const target = $('object-list');
  const visible = (project?.objects || []).slice().sort((a, b) => a.id - b.id);
  target.replaceChildren();
  if (!visible.length) {
    const empty = document.createElement('p');
    empty.className = 'palette-note';
    empty.textContent = activeTab === 'triggers' ? 'Триггеров пока нет — выбери ⚡ и поставь его на сцену.' : 'Объектов пока нет.';
    target.append(empty);
    return;
  }
  const items = activeTab === 'triggers' ? visible.filter(item => item.type === 'trigger') : visible;
  if (!items.length) {
    const empty = document.createElement('p');
    empty.className = 'palette-note';empty.textContent = 'Триггеров пока нет — выбери ⚡ и поставь его на сцену.';
    target.append(empty);return;
  }
  for (const item of items) {
    const row = document.createElement('button');
    row.type = 'button';row.className = `object-row${item.id === selectedId ? ' selected' : ''}`;
    row.innerHTML = '';
    const dot = document.createElement('i');dot.className = 'list-type-dot';dot.style.background = item.color;
    const name = document.createElement('span');name.textContent = item.name || OBJECT_TYPES[item.type].name;
    const id = document.createElement('small');id.textContent = `#${item.id}`;
    row.append(dot, name, id);
    row.addEventListener('click', () => selectObject(item.id));
    target.append(row);
  }
}

function renderInspector() {
  const item = selectedObject();
  $('empty-inspector').classList.toggle('hidden', !!item);
  $('inspector-form').classList.toggle('hidden', !item);
  if (!item) {
    updateObjectList();draw();return;
  }
  $('selected-type').textContent = OBJECT_TYPES[item.type]?.name || item.type;
  $('selected-type-icon').textContent = TYPE_GLYPHS[item.type] || '▦';
  $('object-name').value = item.name;
  $('object-id').value = item.id;
  $('object-number').value = item.number ?? 0;
  $('object-color').value = item.color;
  $('color-value').textContent = item.color.toUpperCase();
  $('object-x').value = Number(item.x.toFixed(2));
  $('object-y').value = Number(item.y.toFixed(2));
  $('object-w').value = Number(item.w.toFixed(2));
  $('object-h').value = Number(item.h.toFixed(2));
  $('object-angle').value = Math.round(item.angle) % 360;
  $('rotation-value').textContent = `${Math.round(item.angle)}°`;
  const settings = $('trigger-settings');
  settings.classList.toggle('hidden', item.type !== 'trigger');
  if (item.type === 'trigger') {
    $('trigger-event').value = item.trigger.event;
    $('trigger-action').value = item.trigger.action;
    $('trigger-target').value = item.trigger.targetId;
    $('trigger-value').value = item.trigger.value;
    $('trigger-color').value = item.trigger.color;
  }
  updateObjectList();
  draw();
}

function updateObjectList() {
  if (!project) return;
  $('list-wrap').classList.toggle('hidden', activeTab === 'level');
  renderObjectList();
}

function selectObject(id) {
  selectedId = id;
  setTool('select');
  renderInspector();
}

function removeObject(id) {
  if (!project) return;
  project.objects = project.objects.filter(item => item.id !== id);
  for (const item of project.objects) {
    if (item.type === 'trigger' && item.trigger.targetId === id) item.trigger.targetId = 0;
  }
  if (selectedId === id) selectedId = null;
  renderInspector();updateObjectList();scheduleSave();draw();
}

function updateSelected(key, value) {
  const item = selectedObject();
  if (!item) return;
  if (key === 'id') {
    const id = Math.trunc(Number(value));
    if (!Number.isSafeInteger(id) || id < 1 || id > 1000000 || project.objects.some(other => other.id === id && other !== item)) {
      toast('ID должен быть уникальным целым числом от 1 до 1000000.');
      renderInspector();return;
    }
    const previous = item.id;
    item.id = id;selectedId = id;
    for (const other of project.objects)
      if (other.type === 'trigger' && other.trigger.targetId === previous) other.trigger.targetId = id;
  } else if (key === 'name') item.name = String(value).slice(0, 48);
  else if (key === 'color') {item.color = value; $('color-value').textContent = value.toUpperCase();}
  else if (key === 'number') item.number = Math.trunc(clamp(Number(value) || 0, 0, 9999));
  else if (key === 'x') item.x = clamp(Number(value) || 0, 0, project.width - item.w);
  else if (key === 'y') item.y = clamp(Number(value) || 0, 0, project.height - item.h);
  else if (key === 'w') item.w = clamp(Number(value) || 0.25, 0.25, project.width - item.x);
  else if (key === 'h') item.h = clamp(Number(value) || 0.25, 0.25, project.height - item.y);
  else if (key === 'angle') item.angle = ((Number(value) % 360) + 360) % 360;
  if (key === 'angle') {
    $('object-angle').value = Math.round(item.angle);
    $('rotation-value').textContent = `${Math.round(item.angle)}°`;
  }
  if (key === 'x') $('object-x').value = Number(item.x.toFixed(2));
  if (key === 'y') $('object-y').value = Number(item.y.toFixed(2));
  if (key === 'w') $('object-w').value = Number(item.w.toFixed(2));
  if (key === 'h') $('object-h').value = Number(item.h.toFixed(2));
  updateObjectList();scheduleSave();draw();
}

function updateTrigger(key, value) {
  const item = selectedObject();
  if (!item || item.type !== 'trigger') return;
  if (key === 'targetId') item.trigger.targetId = Math.trunc(clamp(Number(value) || 0, 0, 1000000));
  else if (key === 'value') item.trigger.value = clamp(Number(value) || 0, -100, 100);
  else if (key === 'color') item.trigger.color = value;
  else item.trigger[key] = value;
  scheduleSave();draw();
}

function canvasPoint(event) {
  const rect = canvas.getBoundingClientRect();
  return {
    x: clamp((event.clientX - rect.left) * canvas.width / rect.width / TILE, 0, project.width - 0.001),
    y: clamp((event.clientY - rect.top) * canvas.height / rect.height / TILE, 0, project.height - 0.001),
  };
}

function snapObjectPosition(x, y, item) {
  const step = activeTab === 'objects' ? 0.25 : 1;
  return {x: clamp(roundStep(x, step), 0, project.width - item.w),
          y: clamp(roundStep(y, step), 0, project.height - item.h)};
}

function findAt(x, y) {
  const list = project?.objects || [];
  for (let i = list.length - 1; i >= 0; i--) {
    const item = list[i];
    if (x >= item.x && x <= item.x + item.w && y >= item.y && y <= item.y + item.h) return item;
  }
  return null;
}

function onPointerDown(event) {
  if (!project || isPlaying) return;
  const point = canvasPoint(event);
  $('coordinates').textContent = `x: ${point.x.toFixed(2)} · y: ${point.y.toFixed(2)}`;
  if (activeTool === 'erase') {
    const item = findAt(point.x, point.y);
    if (item) removeObject(item.id);
    return;
  }
  if (activeTool === 'select') {
    const item = findAt(point.x, point.y);
    if (!item) {selectedId = null;renderInspector();return;}
    selectedId = item.id;
    dragging = {id: item.id, offsetX: point.x - item.x, offsetY: point.y - item.y};
    renderInspector();
  } else {
    if (project.objects.length >= MAX_OBJECTS) {toast(`В одном проекте может быть не больше ${MAX_OBJECTS} объектов.`);return;}
    const item = makeObject(activeTool, nextObjectId(project), 0, 0);
    if (activeTab === 'objects') {
      item.x = clamp(point.x - item.w / 2, 0, project.width - item.w);
      item.y = clamp(point.y - item.h / 2, 0, project.height - item.h);
    } else {
      item.x = clamp(Math.floor(point.x), 0, project.width - item.w);
      item.y = clamp(Math.floor(point.y), 0, project.height - item.h);
    }
    project.objects.push(item);
    selectedId = item.id;
    dragging = {id: item.id, offsetX: point.x - item.x, offsetY: point.y - item.y};
    renderInspector();scheduleSave();
  }
  canvas.setPointerCapture?.(event.pointerId);
  event.preventDefault();
}

function onPointerMove(event) {
  if (!project || isPlaying) return;
  const point = canvasPoint(event);
  $('coordinates').textContent = `x: ${point.x.toFixed(2)} · y: ${point.y.toFixed(2)}`;
  if (!dragging) return;
  const item = project.objects.find(other => other.id === dragging.id);
  if (!item) return;
  const snapped = snapObjectPosition(point.x - dragging.offsetX, point.y - dragging.offsetY, item);
  item.x = snapped.x;item.y = snapped.y;
  $('object-x').value = Number(item.x.toFixed(2));$('object-y').value = Number(item.y.toFixed(2));
  updateObjectList();draw();
}

function onPointerUp() {
  if (!dragging) return;
  dragging = null;scheduleSave();
}

function drawGrid() {
  ctx.fillStyle = '#17241e';ctx.fillRect(0, 0, canvas.width, canvas.height);
  for (let y = 0; y < project.height; y++) {
    ctx.fillStyle = y % 2 ? '#1b2a22' : '#1d2d24';
    ctx.fillRect(0, y * TILE, canvas.width, TILE);
  }
  ctx.strokeStyle = '#d8e4c218';ctx.lineWidth = 1;
  for (let x = 0; x <= project.width; x++) {
    ctx.beginPath();ctx.moveTo(x * TILE + .5, 0);ctx.lineTo(x * TILE + .5, canvas.height);ctx.stroke();
  }
  for (let y = 0; y <= project.height; y++) {
    ctx.beginPath();ctx.moveTo(0, y * TILE + .5);ctx.lineTo(canvas.width, y * TILE + .5);ctx.stroke();
  }
  ctx.fillStyle = '#e9f0dd66';ctx.font = '10px PTSans, sans-serif';ctx.textAlign = 'left';
  for (let x = 0; x < project.width; x++) ctx.fillText(String(x), x * TILE + 4, 12);
  ctx.textAlign = 'right';
  for (let y = 1; y < project.height; y++) ctx.fillText(String(y), canvas.width - 4, y * TILE + 13);
}

function roundedRect(context, x, y, w, h, radius, fill, stroke) {
  const r = Math.min(radius, w / 2, h / 2);
  context.beginPath();
  context.moveTo(x + r, y);context.arcTo(x + w, y, x + w, y + h, r);
  context.arcTo(x + w, y + h, x, y + h, r);context.arcTo(x, y + h, x, y, r);
  context.arcTo(x, y, x + w, y, r);context.closePath();
  if (fill) {context.fillStyle = fill;context.fill();}
  if (stroke) {context.strokeStyle = stroke;context.stroke();}
}

function drawObject(item, selected = false) {
  if (isPlaying && item.type === 'player') return;
  const x = item.x * TILE, y = item.y * TILE, w = item.w * TILE, h = item.h * TILE;
  ctx.save();
  if (!item.visible) ctx.globalAlpha = isPlaying ? 0 : 0.34;
  ctx.translate(x + w / 2, y + h / 2);
  ctx.rotate((item.angle || 0) * Math.PI / 180);
  const left = -w / 2, top = -h / 2;
  const fill = item.color;
  ctx.lineWidth = Math.max(1.5, Math.min(3, TILE * 0.045));
  switch (item.type) {
    case 'block':
      roundedRect(ctx, left, top, w, h, 7, fill, '#e9d1a288');
      ctx.fillStyle = '#ffffff20';ctx.fillRect(left + 4, top + 4, Math.max(0, w - 8), Math.max(3, h * .14));
      ctx.strokeStyle = '#3b291c88';ctx.strokeRect(left + 4, top + 4, w - 8, h - 8);
      break;
    case 'ground':
      ctx.fillStyle = fill;ctx.fillRect(left, top, w, h);
      ctx.fillStyle = '#b7d475';ctx.fillRect(left, top, w, Math.min(9, h));
      ctx.fillStyle = '#355438';ctx.fillRect(left, top + Math.min(9, h), w, Math.max(0, h - 9));
      ctx.strokeStyle = '#d2e58b';ctx.strokeRect(left + 1, top + 1, w - 2, h - 2);
      break;
    case 'hazard': {
      ctx.fillStyle = '#572f31';ctx.fillRect(left, top + h * .68, w, h * .32);
      const teeth = Math.max(1, Math.floor(w / 18));
      ctx.fillStyle = fill;ctx.strokeStyle = '#ffd0a2';ctx.beginPath();
      for (let i = 0; i < teeth; i++) {
        const x1 = left + i * w / teeth, x2 = left + (i + .5) * w / teeth, x3 = left + (i + 1) * w / teeth;
        ctx.moveTo(x1, top + h * .72);ctx.lineTo(x2, top + h * .08);ctx.lineTo(x3, top + h * .72);
      }
      ctx.closePath();ctx.fill();ctx.stroke();break;
    }
    case 'coin':
      ctx.fillStyle = '#774d1d';ctx.beginPath();ctx.ellipse(0, h * .08, w * .43, h * .43, 0, 0, Math.PI * 2);ctx.fill();
      ctx.fillStyle = fill;ctx.strokeStyle = '#fff0a1';ctx.beginPath();ctx.arc(0, 0, Math.min(w, h) * .42, 0, Math.PI * 2);ctx.fill();ctx.stroke();
      ctx.fillStyle = '#96621b';ctx.font = `bold ${Math.max(10, Math.min(25, h * .7))}px PTSans`;
      ctx.textAlign = 'center';ctx.textBaseline = 'middle';ctx.fillText('●', 0, 0);break;
    case 'enemy':
      ctx.fillStyle = fill;ctx.strokeStyle = '#ffe1ce';ctx.beginPath();ctx.ellipse(0, 0, w * .45, h * .45, 0, 0, Math.PI * 2);ctx.fill();ctx.stroke();
      ctx.fillStyle = '#fff7df';ctx.beginPath();ctx.arc(-w * .14, -h * .05, Math.max(3, w * .09), 0, Math.PI * 2);ctx.arc(w * .14, -h * .05, Math.max(3, w * .09), 0, Math.PI * 2);ctx.fill();
      ctx.fillStyle = '#29352d';ctx.beginPath();ctx.arc(-w * .14, -h * .05, Math.max(1.5, w * .035), 0, Math.PI * 2);ctx.arc(w * .14, -h * .05, Math.max(1.5, w * .035), 0, Math.PI * 2);ctx.fill();
      break;
    case 'player':
      roundedRect(ctx, left + w * .1, top + h * .04, w * .8, h * .9, 10, fill, '#d9f1f6');
      ctx.fillStyle = '#f9f5dc';ctx.beginPath();ctx.arc(-w * .12, -h * .1, Math.max(3, w * .08), 0, Math.PI * 2);ctx.arc(w * .12, -h * .1, Math.max(3, w * .08), 0, Math.PI * 2);ctx.fill();
      ctx.fillStyle = '#203431';ctx.beginPath();ctx.arc(-w * .12, -h * .1, Math.max(1.5, w * .035), 0, Math.PI * 2);ctx.arc(w * .12, -h * .1, Math.max(1.5, w * .035), 0, Math.PI * 2);ctx.fill();
      break;
    case 'goal':
      ctx.strokeStyle = '#f2e3b7';ctx.lineWidth = 4;ctx.beginPath();ctx.moveTo(left + w * .22, top + h);ctx.lineTo(left + w * .22, top);ctx.stroke();
      ctx.fillStyle = fill;ctx.beginPath();ctx.moveTo(left + w * .25, top + h * .08);ctx.lineTo(left + w * .94, top + h * .26);ctx.lineTo(left + w * .25, top + h * .5);ctx.closePath();ctx.fill();
      ctx.fillStyle = '#fff2c2';ctx.font = 'bold 11px PTSans';ctx.textAlign = 'center';ctx.fillText('ФИНИШ', left + w * .6, top + h * .34);break;
    case 'trigger':
      ctx.fillStyle = `${fill}44`;ctx.fillRect(left, top, w, h);
      ctx.setLineDash([6, 4]);ctx.strokeStyle = fill;ctx.strokeRect(left + 2, top + 2, w - 4, h - 4);ctx.setLineDash([]);
      ctx.fillStyle = '#d8ffff';ctx.font = `bold ${Math.max(18, Math.min(34, h * .65))}px PTSans`;ctx.textAlign = 'center';ctx.textBaseline = 'middle';ctx.fillText('⚡', 0, 0);break;
  }
  if (selected) {
    ctx.setLineDash([5, 3]);ctx.lineWidth = 2;ctx.strokeStyle = '#fff2bd';ctx.strokeRect(left - 3, top - 3, w + 6, h + 6);ctx.setLineDash([]);
  }
  ctx.restore();
  if ((!isPlaying && ($('show-ids').checked || selected)) || (isPlaying && item.visible && item.number)) {
    const text = item.number ? `#${item.id} · №${item.number}` : `ID ${item.id}`;
    ctx.font = 'bold 10px PTSans, sans-serif';
    const width = ctx.measureText(text).width + 10;
    const bx = clamp(x, 0, canvas.width - width), by = clamp(y - 16, 0, canvas.height - 16);
    roundedRect(ctx, bx, by, width, 15, 5, '#14231fe8', '#f3d38399');
    ctx.fillStyle = '#ffedba';ctx.textAlign = 'left';ctx.textBaseline = 'middle';ctx.fillText(text, bx + 5, by + 7.5);
  }
}

function drawPlayerSprite() {
  if (!player) return;
  const w = player.w, h = player.h;
  roundedRect(ctx, player.x + 3, player.y + 1, w - 6, h - 2, 10, '#77b7d8', '#e1f3f6');
  ctx.fillStyle = '#f9f3d9';ctx.beginPath();ctx.arc(player.x + w * .35, player.y + h * .32, 4, 0, Math.PI * 2);ctx.arc(player.x + w * .67, player.y + h * .32, 4, 0, Math.PI * 2);ctx.fill();
  ctx.fillStyle = '#233630';ctx.beginPath();ctx.arc(player.x + w * .35, player.y + h * .32, 2, 0, Math.PI * 2);ctx.arc(player.x + w * .67, player.y + h * .32, 2, 0, Math.PI * 2);ctx.fill();
}

function draw() {
  if (!project) {
    ctx.fillStyle = '#17241e';ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = '#f0d88f';ctx.textAlign = 'center';ctx.textBaseline = 'middle';ctx.font = 'bold 26px PTSans';
    ctx.fillText('Выбери платформерный шаблон, чтобы начать', canvas.width / 2, canvas.height / 2);
    return;
  }
  drawGrid();
  const objects = activeObjects();
  for (const item of objects) drawObject(item, !isPlaying && item.id === selectedId);
  if (isPlaying) {
    drawPlayerSprite();
    ctx.fillStyle = '#14231fc4';ctx.fillRect(8, canvas.height - 34, 212, 24);
    ctx.fillStyle = '#cfe7cb';ctx.font = '12px PTSans';ctx.textAlign = 'left';ctx.textBaseline = 'middle';
    ctx.fillText('← → или A / D · ПРОБЕЛ — прыжок', 16, canvas.height - 22);
    if (playWon) {
      ctx.fillStyle = '#13251ddc';ctx.fillRect(canvas.width / 2 - 185, canvas.height / 2 - 38, 370, 76);
      ctx.fillStyle = '#ffe6a4';ctx.textAlign = 'center';ctx.font = 'bold 26px PTSans';ctx.fillText('УРОВЕНЬ ПРОЙДЕН!', canvas.width / 2, canvas.height / 2);
    }
  }
}

function selectLabel(targetId) {
  const target = project?.objects.find(item => item.id === targetId);
  return target ? `${target.name} (#${target.id})` : 'Не выбрана';
}

function updatePhysics(dt) {
  if (!isPlaying || !player || !playObjects || playWon) return;
  dt = clamp(dt, 0, 0.04);
  const right = keys.has('ArrowRight') || keys.has('d') || keys.has('D');
  const left = keys.has('ArrowLeft') || keys.has('a') || keys.has('A');
  player.vx = (right ? 1 : 0) - (left ? 1 : 0);
  player.vx *= 235;
  if ((keys.has(' ') || keys.has('ArrowUp') || keys.has('w') || keys.has('W')) && player.grounded) {
    player.vy = -560;player.grounded = false;
  }
  player.vy = Math.min(760, player.vy + 1250 * dt);
  const solids = playObjects.filter(item => item.visible && (item.type === 'block' || item.type === 'ground'));
  player.x += player.vx * dt;
  player.x = clamp(player.x, 0, canvas.width - player.w);
  for (const solid of solids) {
    const box = {x:solid.x*TILE, y:solid.y*TILE, w:solid.w*TILE, h:solid.h*TILE};
    if (!overlaps(player, box)) continue;
    if (player.vx > 0) player.x = box.x - player.w;
    else if (player.vx < 0) player.x = box.x + box.w;
  }
  const beforeY = player.y;
  player.y += player.vy * dt;
  player.grounded = false;
  for (const solid of solids) {
    const box = {x:solid.x*TILE, y:solid.y*TILE, w:solid.w*TILE, h:solid.h*TILE};
    if (!overlaps(player, box)) continue;
    if (player.vy >= 0 && beforeY + player.h <= box.y + 6) {
      player.y = box.y - player.h;player.vy = 0;player.grounded = true;
    } else if (player.vy < 0 && beforeY >= box.y + box.h - 4) {
      player.y = box.y + box.h;player.vy = 0;
    }
  }
  if (player.y > canvas.height + 80) resetPlayer('Упс! Вернул тебя к старту.');
  const body = {x:player.x, y:player.y, w:player.w, h:player.h};
  for (const item of playObjects) {
    if (!item.visible) continue;
    const box = objectRect(item);
    if (item.type === 'hazard' && overlaps(body, box)) resetPlayer('Опасность! Попробуй ещё раз.');
    else if (item.type === 'enemy' && overlaps(body, box)) resetPlayer('Противник коснулся игрока — возврат к старту.');
    else if (item.type === 'coin' && overlaps(body, box)) {
      item.visible = false;
      fireTriggers('coin');
      toast('Монета собрана!');
    } else if (item.type === 'goal' && overlaps(body, box)) {
      playWon = true;toast('Финиш! Уровень пройден.', 5000);
    }
  }
  if (manualPressed) fireTriggers('manual');
  fireTriggers('touch');
  manualPressed = false;
}

function objectRect(item) {
  return {x:item.x*TILE, y:item.y*TILE, w:item.w*TILE, h:item.h*TILE};
}

function overlaps(a, b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

function fireTriggers(event) {
  const body = {x:player.x, y:player.y, w:player.w, h:player.h};
  for (const trigger of playObjects) {
    if (!trigger.visible || trigger.type !== 'trigger' || trigger.trigger.event !== event || firedTriggers.has(trigger.id)) continue;
    if (event !== 'coin' && !overlaps(body, objectRect(trigger))) continue;
    const target = playObjects.find(item => item.id === trigger.trigger.targetId);
    if (!target) {firedTriggers.add(trigger.id);toast(`Триггер #${trigger.id}: выбери существующую цель по ID.`);continue;}
    switch (trigger.trigger.action) {
      case 'toggle': target.visible = !target.visible;break;
      case 'move': target.x = clamp(target.x + trigger.trigger.value, 0, project.width - target.w);break;
      case 'recolor': target.color = trigger.trigger.color;break;
      case 'number': target.number = Math.trunc(clamp(trigger.trigger.value, 0, 9999));break;
    }
    firedTriggers.add(trigger.id);
    toast(`Триггер #${trigger.id} сработал → ${selectLabel(target.id)}.`);
  }
}

function resetPlayer(message) {
  if (!player || !playObjects) return;
  const start = playObjects.find(item => item.type === 'player');
  if (start) {
    player.x = start.x * TILE;player.y = start.y * TILE;
    player.vx = 0;player.vy = 0;player.grounded = false;
  }
  toast(message, 1800);
}

function startTest() {
  if (!project) {toast('Сначала создай или открой проект.');return;}
  const start = project.objects.find(item => item.type === 'player');
  if (!start) {toast('Поставь на уровень объект «Старт игрока».');return;}
  playObjects = duplicateProject(project).objects;
  const spawn = playObjects.find(item => item.type === 'player');
  player = {x:spawn.x*TILE, y:spawn.y*TILE, w:Math.max(24, spawn.w*TILE), h:Math.max(34, spawn.h*TILE), vx:0, vy:0, grounded:false};
  firedTriggers.clear();keys.clear();playWon = false;isPlaying = true;
  $('test-hud').classList.remove('hidden');$('playtest').textContent = '■ Остановить тест';
  $('playtest').classList.add('is-stop');
  toast('Тестовый режим: ← → / A D — движение, ПРОБЕЛ — прыжок, E — триггер.');
  draw();
}

function stopTest() {
  isPlaying = false;playObjects = null;player = null;playWon = false;
  keys.clear();firedTriggers.clear();manualPressed = false;
  $('test-hud').classList.add('hidden');$('playtest').textContent = '▶ Проверить уровень';
  $('playtest').classList.remove('is-stop');draw();
}

function exportProject() {
  if (!project) {toast('Сначала создай или открой проект.');return;}
  const text = serializeProject(project);
  const safeName = project.title.normalize('NFKD').replace(/[^\p{L}\p{N}-]+/gu, '-').replace(/^-+|-+$/g, '').slice(0, 50) || 'pvg3-level';
  if (window.PvG3Native?.saveTxt) {
    window.PvG3Native.saveTxt(`${safeName}.txt`, text);
    saveLabel('Выбери место для TXT-файла.');
    toast('Выбери папку и подтверди сохранение TXT-файла.');
    return;
  }
  const blob = new Blob([text], {type:'text/plain;charset=utf-8'});
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;link.download = `${safeName}.txt`;
  document.body.append(link);link.click();link.remove();setTimeout(() => URL.revokeObjectURL(url), 1000);
  saveLabel('TXT-файл скачан — сохрани его отдельно от браузера.');
  toast('Проект скачан как TXT. Его можно хранить или перенести на другое устройство.');
}

async function importProject(file) {
  if (!file) return;
  try {
    const loaded = parseProject(await file.text());
    project = loaded;selectedId = null;selectedTemplate = loaded.templateId;
    stopTest();updateProjectHeader();renderInspector();updateObjectList();scheduleSave();draw();
    toast(`Открыт проект «${project.title}». Автосохранение обновлено.`);
  } catch (error) {
    toast(error.message || 'Не удалось открыть этот TXT-файл.', 5500);
  } finally {
    $('project-file').value = '';
  }
}

function handleShortcut(event) {
  if (dialog.open) return;
  const target = event.target;
  const typing = target instanceof HTMLElement && (target.isContentEditable || ['INPUT','TEXTAREA','SELECT'].includes(target.tagName));
  if (isPlaying) {
    if (['ArrowLeft','ArrowRight','ArrowUp',' ','a','A','d','D','w','W'].includes(event.key)) {
      keys.add(event.key);event.preventDefault();
    } else if (event.key === 'e' || event.key === 'E') {
      manualPressed = true;event.preventDefault();
    } else if (event.key === 'Escape') stopTest();
    return;
  }
  if (typing) return;
  const shortcuts = {b:'block',p:'ground',h:'hazard',c:'coin',e:'enemy',s:'player',f:'goal',t:'trigger',v:'select'};
  if (shortcuts[event.key.toLowerCase()]) setTool(shortcuts[event.key.toLowerCase()]);
  else if ((event.key === 'Delete' || event.key === 'Backspace') && selectedId != null) removeObject(selectedId);
}

function bindInspector() {
  $('inspector-form').addEventListener('submit', event => event.preventDefault());
  $('object-name').addEventListener('input', event => updateSelected('name', event.target.value));
  $('object-id').addEventListener('change', event => updateSelected('id', event.target.value));
  $('object-number').addEventListener('change', event => updateSelected('number', event.target.value));
  $('object-color').addEventListener('input', event => updateSelected('color', event.target.value));
  for (const key of ['x','y','w','h']) $( `object-${key}`).addEventListener('change', event => updateSelected(key, event.target.value));
  $('object-angle').addEventListener('input', event => updateSelected('angle', event.target.value));
  document.querySelectorAll('[data-rotate]').forEach(button => button.addEventListener('click', () => {
    const item = selectedObject();if (item) updateSelected('angle', item.angle + Number(button.dataset.rotate));
  }));
  $('reset-rotation').addEventListener('click', () => updateSelected('angle', 0));
  $('delete-selected').addEventListener('click', () => selectedId != null && removeObject(selectedId));
  $('trigger-event').addEventListener('change', event => updateTrigger('event', event.target.value));
  $('trigger-action').addEventListener('change', event => updateTrigger('action', event.target.value));
  $('trigger-target').addEventListener('change', event => updateTrigger('targetId', event.target.value));
  $('trigger-value').addEventListener('change', event => updateTrigger('value', event.target.value));
  $('trigger-color').addEventListener('input', event => updateTrigger('color', event.target.value));
}

for (const button of document.querySelectorAll('.maker-tabs [data-tab]'))
  button.addEventListener('click', () => setTab(button.dataset.tab));
for (const button of document.querySelectorAll('[data-tool]'))
  button.addEventListener('click', () => setTool(button.dataset.tool));
for (const card of document.querySelectorAll('[data-template]')) {
  card.addEventListener('click', () => {
    selectedTemplate = card.dataset.template;
    document.querySelectorAll('[data-template]').forEach(item => item.classList.toggle('active', item === card));
  });
}

$('create-project').addEventListener('click', () => beginNewProject(selectedTemplate));
dialog.addEventListener('cancel', event => {
  if (!project) {event.preventDefault();toast('Выбери шаблон — без проекта редактору нечего показывать.');}
});
$('new-project').addEventListener('click', () => {
  if (project && !confirm('Создать новый проект? Текущий останется в автосохранении только до следующего сохранения. Скачай его TXT, если хочешь оставить копию.')) return;
  openTemplateDialog();
});
$('close-template-dialog').addEventListener('click', () => {
  if (!project) {toast('Выбери шаблон — без проекта редактору нечего показывать.');return;}
  dialog.close();
});
$('download-project').addEventListener('click', exportProject);
$('project-file').addEventListener('change', event => importProject(event.target.files?.[0]));
$('project-title').addEventListener('input', event => {
  if (!project) return;
  project.title = event.target.value.slice(0, 80);
  scheduleSave();
});
$('show-ids').addEventListener('change', draw);
$('playtest').addEventListener('click', () => isPlaying ? stopTest() : startTest());
$('stop-test').addEventListener('click', stopTest);
canvas.addEventListener('pointerdown', onPointerDown);
canvas.addEventListener('pointermove', onPointerMove);
canvas.addEventListener('pointerup', onPointerUp);
canvas.addEventListener('pointercancel', onPointerUp);
window.addEventListener('keydown', handleShortcut);
window.addEventListener('keyup', event => keys.delete(event.key));
window.addEventListener('blur', () => keys.clear());
window.addEventListener('beforeunload', persistNow);
bindInspector();

project = readLocalProject();
if (project) {
  selectedTemplate = project.templateId;updateProjectHeader();
  saveLabel('Восстановлен локальный проект · пароль не нужен');
  renderInspector();setTab('level');
} else {
  selectedTemplate = 'classic';setTab('level');openTemplateDialog();
  saveLabel('Выбери шаблон и скачай TXT для резервной копии');
}
draw();

function frame(now) {
  const dt = Math.min(.04, Math.max(0, (now - lastFrame) / 1000));
  lastFrame = now;
  if (isPlaying) updatePhysics(dt);
  draw();
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
