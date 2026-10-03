/* Firebase Realtime Database REST adapter. No SDK, password, API key, or
 * privileged service account is embedded in the website or the APK. Demo
 * rules must permit the relevant /rooms and public-level catalog operations. */
import {isPublishedRecord, publishedRecord, validateDraft} from './workshop.js';

export const DATABASE = 'https://pvg3-ae824-default-rtdb.firebaseio.com';
const ROOM_PATH = 'rooms';
const alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
export const validId = id => typeof id === 'string' && /^[A-Z2-9]{6}$/.test(id);
export const validLevelId = id => typeof id === 'string' && /^[1-9][0-9]{0,5}$/.test(id);

class FirebaseError extends Error {
  constructor(status, message) {super(message);this.status = status;}
}

async function request(path, method = 'GET', body, ifMatch = '') {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 9000);
  try {
    const headers = {};
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    if (ifMatch) headers['If-Match'] = ifMatch;
    const [node, query] = path.split('?');
    const response = await fetch(`${DATABASE}/${node}.json${query ? `?${query}` : ''}`, {
      method, headers, body: body === undefined ? undefined : JSON.stringify(body),
      signal: controller.signal, cache: 'no-store', referrerPolicy: 'no-referrer',
    });
    if (!response.ok) {
      if (response.status === 401 || response.status === 403)
        throw new FirebaseError(response.status, 'Firebase запретил чтение или запись. Проверь правила Firebase.');
      if (response.status === 412) throw new FirebaseError(412, 'Комната уже занята. Обнови список и попробуй снова.');
      throw new FirebaseError(response.status, `Firebase: HTTP ${response.status} (${(await response.text()).slice(0, 160)})`);
    }
    if (response.status === 204) return null;
    const text = await response.text();
    return text ? JSON.parse(text) : null;
  } catch (e) {
    if (e.name === 'AbortError') throw new Error('Firebase не отвечает (таймаут). Проверь интернет.');
    if (e instanceof TypeError) throw new Error('Нет соединения с Firebase. Проверь сеть или настройки доступа.');
    throw e;
  } finally { clearTimeout(timeout); }
}

export function randomRoomId() {
  const bytes = crypto.getRandomValues(new Uint8Array(6));
  return Array.from(bytes, v => alphabet[v & 31]).join('');
}
export function randomPlayerId() {
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
}
export async function listRooms() {
  // Shallow RTDB queries contain keys but no timestamps. Taking the last N
  // keys would eventually hide new rooms behind abandoned, alphabetically
  // later ones. Read just /rooms, then show the most recently active lobbies.
  const data = await request(ROOM_PATH);
  if (!data || typeof data !== 'object' || Array.isArray(data)) return [];
  const now = Date.now();
  return Object.entries(data)
    .filter(([id, room]) => validId(id) && room?.version === 1 &&
      typeof room.host?.id === 'string' && Number.isFinite(room.host.ping) &&
      Math.abs(now - room.host.ping) < 10 * 60 * 1000 &&
      !room.guest && !room.state)
    .sort((a, b) => b[1].host.ping - a[1].host.ping)
    .slice(0, 24).map(([id, room]) => ({id, room}));
}
// RTDB does not preserve [] or arrays filled with null: those paths become
// missing nodes. Keep sentinel values in the wire format, then restore the
// exact arrays expected by the rules before either client uses a snapshot.
export function encodeState(state) {
  const nonempty = values => values.length ? values : [false];
  return {...state,
    plants: state.plants.map(p => p === null ? false : p),
    ducks: nonempty(state.ducks), peas: nonempty(state.peas),
    coins: nonempty(state.coins)};
}
export function decodeState(state) {
  if (!state || typeof state !== 'object') return state;
  if (Array.isArray(state.plants))
    state.plants = state.plants.map(p => p === false ? null : p);
  for (const key of ['ducks', 'peas', 'coins'])
    if (Array.isArray(state[key]) && state[key].length === 1 && state[key][0] === false)
      state[key] = [];
  return state;
}
export async function getRoom(id) {
  if (!validId(id)) throw new Error('Неверный код комнаты.');
  const room = await request(`${ROOM_PATH}/${id}`);
  if (room?.state) decodeState(room.state);
  return room;
}
export async function createRoom(playerId, map) {
  for (let attempt = 0; attempt < 3; attempt++) {
    const id = randomRoomId();
    try {
      const room = {version: 1, map: map === 5 ? 5 : 1,
        host: {id: playerId, role: null, ping: Date.now()}, guest: null,
        state: null, command: null};
      await request(`${ROOM_PATH}/${id}`, 'PUT', room, 'null_etag');
      return {id, room};
    } catch (e) {
      if (e.message.includes('занята')) continue;
      throw e;
    }
  }
  throw new Error('Не удалось создать уникальный код комнаты.');
}
export async function joinRoom(id, playerId) {
  const room = await getRoom(id);
  if (!room || room.version !== 1 || !room.host?.id || room.state || room.guest)
    throw new Error('Комната закрыта, уже началась или занята.');
  const guest = {id: playerId, role: null, ping: Date.now()};
  await request(`${ROOM_PATH}/${id}/guest`, 'PUT', guest, 'null_etag');
  const joined = await getRoom(id);
  if (!joined?.host?.id) throw new Error('Создатель закрыл комнату.');
  return joined;
}
export async function chooseRole(id, slot, role) {
  if (!validId(id) || !['host', 'guest'].includes(slot) || !['plants', 'zombies'].includes(role))
    throw new Error('Неверная сторона.');
  return request(`${ROOM_PATH}/${id}/${slot}/role`, 'PUT', role);
}
export async function writeState(id, state) {
  return request(`${ROOM_PATH}/${id}/state`, 'PUT', encodeState(state));
}
export async function writeCommand(id, command) {
  return request(`${ROOM_PATH}/${id}/command`, 'PUT', command);
}
export async function heartbeat(id, slot) {
  return request(`${ROOM_PATH}/${id}/${slot}/ping`, 'PUT', Date.now());
}
export async function leaveRoom(id, slot, playerId) {
  if (!validId(id) || !['host', 'guest'].includes(slot)) return;
  const room = await getRoom(id);
  if (room?.[slot]?.id !== playerId) return;
  return request(`${ROOM_PATH}/${id}${slot === 'host' ? '' : '/guest'}`, 'DELETE');
}

export async function listPublishedLevels() {
  const data = await request('levels-index');
  if (!data || typeof data !== 'object' || Array.isArray(data)) return [];
  return Object.entries(data)
    .filter(([id, entry]) => validLevelId(id) && entry?.id === id &&
      typeof entry.title === 'string' && entry.title.trim() && entry.title.length <= 80 &&
      (entry.description === undefined || typeof entry.description === 'string'))
    .map(([id, entry]) => ({id, title: entry.title, description: entry.description || '',
      updatedAt: Number.isFinite(entry.updatedAt) ? entry.updatedAt : 0}))
    .sort((a, b) => b.updatedAt - a.updatedAt || Number(b.id) - Number(a.id))
    .slice(0, 80);
}

export async function getPublishedLevel(id) {
  if (!validLevelId(id)) throw new Error('Неверный ID уровня.');
  const record = await request(`levels/${id}`);
  if (!record) throw new Error('Уровень не найден в каталоге.');
  if (!isPublishedRecord(record, id)) throw new Error('Уровень повреждён или его формат не поддерживается.');
  return record;
}

function randomLevelId() {
  const value = new Uint32Array(1);
  crypto.getRandomValues(value);
  return String(value[0] % 999999 + 1);
}

export async function publishLevel(draft) {
  const check = validateDraft(draft);
  if (!check.ok) throw new Error(check.message);
  let lastCollision;
  for (let attempt = 0; attempt < 5; attempt++) {
    const id = randomLevelId();
    const record = publishedRecord(id, draft);
    try {
      await request(`levels/${id}`, 'PUT', record, 'null_etag');
    } catch (e) {
      if (e.status === 412) {lastCollision = e;continue;}
      throw e;
    }
    const summary = {id, title: record.title, description: record.description,
      updatedAt: Date.now()};
    try {
      await request(`levels-index/${id}`, 'PUT', summary, 'null_etag');
    } catch (e) {
      const detail = e.status === 401 || e.status === 403 ?
        'Firebase запретил запись в /levels-index. Проверь правила Firebase.' : e.message;
      throw new FirebaseError(e.status || 0,
        `Уровень ${id} сохранён, но не появился в каталоге: ${detail} Повтори публикацию позже или проверь права базы.`);
    }
    return {id, record};
  }
  throw lastCollision || new Error('Не удалось подобрать свободный ID для уровня.');
}
