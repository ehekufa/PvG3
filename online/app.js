import {PLANTS, DUCKS, W, H, X, Y, CW, CH, ROWS, COLS,
        newMatch, validMatch, applyCommand, stepMatch} from './rules.js';
import {DATABASE, validId, randomPlayerId, listRooms, getRoom, createRoom,
        joinRoom, chooseRole, writeState, writeCommand, heartbeat, leaveRoom} from './firebase.js';
import {preloadArtwork, drawGame} from './draw.js';

const $ = id => document.getElementById(id);
const canvas = $('battle');
const sections = {rooms: $('rooms-screen'), lobby: $('lobby-screen'), match: $('match-screen')};
let screen = 'rooms', roomId = '', slot = '', room = null, state = null;
let chosenPlant = -1, chosenDuck = -1, mapChoice = 1, pendingSeq = 0;
let seq = 0, lastSnapshot = 0, lastPublish = 0, writing = false;
let lastFrame = performance.now(), lastPing = 0, generation = 0;
let toastTimer;
// A browser tab keeps its seat after reload, while a second tab is a second
// player. A shared localStorage ID would incorrectly identify both as one.
const savedId = sessionStorage.getItem('pvg3-online-player');
const playerId = savedId && /^[0-9a-f]{32}$/.test(savedId) ? savedId : randomPlayerId();
sessionStorage.setItem('pvg3-online-player', playerId);

function show(name) {
  screen = name;
  for (const [key, el] of Object.entries(sections)) el.classList.toggle('hidden', key !== name);
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
    try {await writeCommand(roomId, {...cmd, id: playerId, seq: pendingSeq});}
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
    const pic = document.createElement('div');pic.className = 'book-duck-art';
    const img = document.createElement('img');img.src = '../assets/art/zombie-duck.png';img.alt = '';
    pic.append(img);
    if (d.id === 2 || d.id === 3) {
      const hat = document.createElement('span');hat.className = d.id === 3 ? 'hat helmet' : 'hat';
      hat.textContent = d.id === 2 ? '▲' : '●';pic.append(hat);
    }
    const text = document.createElement('div');
    const name = document.createElement('strong');name.textContent = d.name;
    const detail = document.createElement('small');detail.textContent = `${d.cost} монет · ${d.hp} HP. ${d.detail}`;
    text.append(name, detail);item.append(pic, text);$('book-ducks').append(item);
  }
}

$('refresh').addEventListener('click', refreshRooms);
$('create').addEventListener('click', create);
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
