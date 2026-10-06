/* Player accounts for the workshop: nick + password, one nick per player, and
 * the moderator tools that hang off them (official levels, comments, bans).
 *
 * There is no server of our own: Firebase only stores what the rules in
 * firebase/database.rules.json allow. A password never leaves the device — the
 * client derives a 64-hex PBKDF2 hash and exchanges it for a random session
 * token that travels with privileged writes. Nobody can read /accounts or
 * /tokens, so neither hashes nor tokens are public.
 *
 * Layout in the database:
 *   /accounts/{login}  {salt, hash, createdAt}          — write once, unreadable
 *   /tokens/{login}    {token, hash}                    — login rotates the token
 *   /admins/{login}    true                             — seeded by hand
 *   /bans/{login}      {banned, reason, at, by, tok}    — moderators only
 *   /comments/{level}/{id} {login, text, at, tok, hidden, by}
 */
import {request} from './firebase.js';

/* Lowercase Latin letters, digits and underscore: no spaces, no punctuation,
 * no emoji, so two nicks can never differ by invisible characters. */
export const LOGIN_PATTERN = /^[a-z0-9_]{3,24}$/;
export const MIN_PASSWORD = 6;
export const MAX_PASSWORD = 72;
export const MAX_COMMENT = 300;
const PBKDF2_ROUNDS = 150000;
const SESSION_KEY = 'pvg3-account-session-v1';
const ACCOUNT_CREATE_REFUSED = {
  ru: 'Аккаунт занят или Firebase отказал. Войди либо проверь правила.',
  en: 'The nickname is taken or Firebase refused registration. Sign in or check the rules.'
};
function accountCreateRefusedMessage() {
  const language = globalThis.document?.documentElement?.lang || 'ru';
  return language.toLowerCase().startsWith('en') ?
    ACCOUNT_CREATE_REFUSED.en : ACCOUNT_CREATE_REFUSED.ru;
}

export const normalizeLogin = value => String(value ?? '').trim().toLowerCase();
export const validLogin = login => LOGIN_PATTERN.test(normalizeLogin(login));
export const validPassword = password =>
  typeof password === 'string' && password.length >= MIN_PASSWORD &&
  password.length <= MAX_PASSWORD && !/[\s]/.test(password);

const bytesToHex = bytes =>
  Array.from(new Uint8Array(bytes), b => b.toString(16).padStart(2, '0')).join('');

async function sha256Hex(text) {
  const data = new TextEncoder().encode(text);
  return bytesToHex(await crypto.subtle.digest('SHA-256', data));
}

/* The salt is derived from the nick itself, so a client can compute a hash
 * without reading anything: /accounts stays unreadable. */
export async function accountSalt(login) {
  return sha256Hex(`pvg3-account:${normalizeLogin(login)}`);
}

export async function passwordHash(login, password) {
  const salt = await accountSalt(login);
  const key = await crypto.subtle.importKey('raw', new TextEncoder().encode(password),
    'PBKDF2', false, ['deriveBits']);
  const bits = await crypto.subtle.deriveBits({name: 'PBKDF2', salt:
    new TextEncoder().encode(salt), iterations: PBKDF2_ROUNDS, hash: 'SHA-256'}, key, 256);
  return bytesToHex(bits);
}

function randomToken() {
  return bytesToHex(crypto.getRandomValues(new Uint8Array(32)));
}

function isObject(value) {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

function statusOf(error) {
  return Number.isInteger(error?.status) ? error.status : 0;
}

/* ---------- session ---------- */

let session = null;
const listeners = new Set();

function store(value) {
  try {
    if (value) localStorage.setItem(SESSION_KEY, JSON.stringify(value));
    else localStorage.removeItem(SESSION_KEY);
  } catch { /* private mode: keep the session in memory only */ }
}

export function loadSession() {
  if (session) return session;
  try {
    const raw = JSON.parse(localStorage.getItem(SESSION_KEY) || 'null');
    if (isObject(raw) && validLogin(raw.login) && /^[a-f0-9]{64}$/.test(raw.token || ''))
      session = {login: normalizeLogin(raw.login), token: raw.token,
        hash: /^[a-f0-9]{64}$/.test(raw.hash || '') ? raw.hash : '',
        admin: raw.admin === true};
  } catch { /* ignore a damaged record */ }
  return session;
}

export function currentSession() {
  return loadSession();
}

export function isSignedIn() {
  return !!loadSession();
}

export function isModerator() {
  return loadSession()?.admin === true;
}

export function onSessionChange(listener) {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

function publishSession() {
  store(session);
  for (const listener of listeners) listener(session);
}

export function signOut() {
  session = null;
  publishSession();
}

/* Re-roll the token after a moderator action or a publish: a token that a
 * public record made visible stops working as soon as the next one is issued.
 * The session is only refreshed when it is still the same one — a player who
 * signed out (or signed in again) while the write was in flight keeps their
 * own choice instead of a resurrected session. */
export async function rotateToken() {
  const active = loadSession();
  if (!active || !/^[a-f0-9]{64}$/.test(active.hash || '')) return null;
  const token = randomToken();
  await request(`tokens/${active.login}`, 'PUT', {token, hash: active.hash});
  const now = loadSession();
  if (!now || now.login !== active.login || now.hash !== active.hash ||
    now.token !== active.token) return null;
  session = {...now, token};
  publishSession();
  return session;
}

/* ---------- accounts ---------- */

export async function createAccount(login, password) {
  const name = normalizeLogin(login);
  if (!validLogin(name)) throw new Error('Ник: только a–z, 0–9 и _, от 3 до 24 знаков.');
  if (!validPassword(password))
    throw new Error(`Пароль: от ${MIN_PASSWORD} до ${MAX_PASSWORD} знаков без пробелов.`);
  const salt = await accountSalt(name);
  const hash = await passwordHash(name, password);
  try {
    /* The unreadable account branch's .write rule atomically checks
     * !data.exists(), so account creation does not need an ETag precondition. */
    await request(`accounts/${name}`, 'PUT',
      {salt, hash, createdAt: Date.now()});
  } catch (error) {
    if (statusOf(error) === 412) throw new Error('Такой аккаунт уже есть.');
    if (statusOf(error) === 403)
      throw new Error(accountCreateRefusedMessage());
    throw error;
  }
  return signIn(name, password);
}

export async function signIn(login, password) {
  const name = normalizeLogin(login);
  if (!validLogin(name)) throw new Error('Ник: только a–z, 0–9 и _, от 3 до 24 знаков.');
  if (!validPassword(password)) throw new Error('Неверный ник или пароль.');
  const hash = await passwordHash(name, password);
  const token = randomToken();
  try {
    await request(`tokens/${name}`, 'PUT', {token, hash});
  } catch (error) {
    if (statusOf(error) === 403) throw new Error('Неверный ник или пароль.');
    throw error;
  }
  let admin = false;
  try {
    admin = (await request(`admins/${name}`)) === true;
  } catch { /* the branch is optional; a missing one simply means "not a moderator" */ }
  session = {login: name, token, hash, admin};
  publishSession();
  return session;
}

export async function refreshModeratorFlag() {
  const active = loadSession();
  if (!active) return null;
  let admin = false;
  try {
    admin = (await request(`admins/${active.login}`)) === true;
  } catch { /* keep the previous answer */ admin = active.admin === true; }
  session = {...active, admin};
  publishSession();
  return session;
}

/* ---------- moderation ---------- */

function requireSession() {
  const active = loadSession();
  if (!active) throw new Error('Сначала войди в аккаунт.');
  return active;
}

export const authorFields = () => {
  const active = requireSession();
  return {author: {login: active.login, tok: active.token}};
};

export async function setLevelOfficial(levelId, official = true) {
  const active = requireSession();
  if (!active.admin) throw new Error('Только модератор делает уровень официальным.');
  const record = await request(`levels/${levelId}`);
  if (!isObject(record)) throw new Error('Уровень не найден в каталоге.');
  record.author = {login: active.login, tok: active.token};
  if (official) record.official = true;
  else delete record.official;
  try {
    await request(`levels/${levelId}`, 'PUT', record);
  } catch (error) {
    /* A failed response may still follow a committed write. */
    await rotateToken().catch(() => {});
    throw error;
  }
  const summary = {id: String(levelId), title: record.title,
    description: record.description || '', updatedAt: Date.now(),
    author: active.login};
  if (official) summary.official = true;
  try {
    await request(`levels-index/${levelId}`, 'PUT', summary);
  } finally {
    /* The level record briefly carries this moderator's live token. */
    await rotateToken().catch(() => {});
  }
  return record;
}

export async function banAccount(login, reason = '') {
  const active = requireSession();
  const name = normalizeLogin(login);
  if (!validLogin(name)) throw new Error('Ник: только a–z, 0–9 и _, от 3 до 24 знаков.');
  await request(`bans/${name}`, 'PUT', {banned: true,
    reason: String(reason || '').slice(0, 140), at: Date.now(),
    by: active.login, tok: active.token});
  await rotateToken().catch(() => {});
}

export async function unbanAccount(login, reason = '') {
  const active = requireSession();
  const name = normalizeLogin(login);
  if (!validLogin(name)) throw new Error('Ник: только a–z, 0–9 и _, от 3 до 24 знаков.');
  await request(`bans/${name}`, 'PUT', {banned: false,
    reason: String(reason || '').slice(0, 140), at: Date.now(),
    by: active.login, tok: active.token});
  await rotateToken().catch(() => {});
}

export async function loadBans() {
  const data = await request('bans');
  const result = {};
  if (!isObject(data)) return result;
  for (const [login, entry] of Object.entries(data))
    if (isObject(entry) && entry.banned === true) result[login] = entry;
  return result;
}

export const isBanned = (bans, login) => !!bans?.[normalizeLogin(login)];

/* ---------- comments ---------- */

export async function loadComments(levelId) {
  const data = await request(`comments/${levelId}`);
  if (!isObject(data)) return [];
  return Object.entries(data)
    .filter(([id, entry]) => /^[a-z0-9]{8,24}$/.test(id) && isObject(entry) &&
      typeof entry.login === 'string' && typeof entry.text === 'string')
    .map(([id, entry]) => ({id, login: entry.login, text: entry.text,
      at: Number(entry.at) || 0, hidden: entry.hidden === true}))
    .sort((a, b) => a.at - b.at);
}

function commentId() {
  return bytesToHex(crypto.getRandomValues(new Uint8Array(8)));
}

export async function postComment(levelId, text) {
  const active = requireSession();
  const body = String(text ?? '').trim().slice(0, MAX_COMMENT);
  if (!body) throw new Error('Пустое сообщение.');
  const id = commentId();
  await request(`comments/${levelId}/${id}`, 'PUT',
    {login: active.login, text: body, at: Date.now(), tok: active.token});
  return id;
}

export async function hideComment(levelId, id) {
  const active = requireSession();
  const current = await request(`comments/${levelId}/${id}`);
  if (!isObject(current)) throw new Error('Сообщение уже удалено.');
  await request(`comments/${levelId}/${id}`, 'PUT',
    {...current, hidden: true, by: active.login, tok: active.token});
  await rotateToken().catch(() => {});
}
