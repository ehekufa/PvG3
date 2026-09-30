/* PvG3 Online: the room creator alone simulates the match; the second player
 * sends commands and draws the creator's snapshots. Pure logic, no DOM/network.
 * Only the author's duck PNG is used for all three duck types. */
export const W = 1280, H = 720, ROWS = 5, COLS = 9;
export const X = 250, Y = 120, CW = 114, CH = 112;
export const WAVE = 18;
export const PLANTS = [
  {id: 0, name: 'Горохострел', cost: 100, hp: 300, cooldown: 7.5, image: 'peashooter.png', detail: 'Стреляет по уткам в своём ряду.'},
  {id: 1, name: 'Орех', cost: 50, hp: 4000, cooldown: 30, image: 'walnut.png', detail: 'Крепкая преграда для уток.'},
  {id: 2, name: 'Подсолнух-наркоман', cost: 50, hp: 300, cooldown: 7.5, image: 'coin-sunflower.png', detail: 'Производит монеты (+25 каждые 8 секунд).'},
  {id: 3, name: 'Джампер-боец', cost: 250, hp: 300, cooldown: 20, image: 'jumper-fighter.png', detail: 'Исчезает при ударе, отбрасывая утку на три клетки.'},
  {id: 4, name: 'Кувшинка', cost: 25, hp: 300, cooldown: 7.5, image: 'lily-pad.png', detail: 'Только на водной карте: опора для посадки растений.'},
];
export const DUCKS = [
  {id: 0, name: 'Утка-зомби', cost: 50, hp: 180, cooldown: 2, detail: 'Обычная нарисованная утка-противник.'},
  {id: 2, name: 'Утка с конусом', cost: 100, hp: 420, cooldown: 3, detail: 'Конус защищает ту же утку: 420 здоровья.'},
  {id: 3, name: 'Утка в шлеме', cost: 175, hp: 750, cooldown: 5, detail: 'Шлем защищает ту же утку: 750 здоровья.'},
];
const coinValue = 25;
const cell = (r, c) => r * COLS + c;
const centerX = c => X + c * CW + CW / 2;
const centerY = r => Y + r * CH + CH / 2;
const waterRow = (s, r) => s.map === 5 && (r === 1 || r === 2);

export function newMatch(map = 1) {
  return {
    version: 1, map: map === 5 ? 5 : 1, time: 0,
    plants: Array(ROWS * COLS).fill(null), lilies: Array(ROWS * COLS).fill(false),
    ducks: [], peas: [], coins: [], mowers: Array(ROWS).fill(null).map(() => ({used: false, running: false, x: X + 30})),
    plantCash: 250, zombieCash: 175, plantCooldown: PLANTS.map(() => 0),
    duckCooldown: DUCKS.map(() => 0), zombieIncome: 0, left: WAVE,
    winner: '', nextId: 1, ackGuest: 0,
  };
}

export function validMatch(s) {
  return !!(s && s.version === 1 && [1, 5].includes(s.map) &&
    Array.isArray(s.plants) && s.plants.length === ROWS * COLS &&
    s.plants.every(p => p === null || (p && Number.isInteger(p.type) &&
      p.type >= 0 && p.type < 4 && Number.isFinite(p.hp) && Number.isFinite(p.fire))) &&
    Array.isArray(s.lilies) && s.lilies.length === ROWS * COLS &&
    s.lilies.every(p => typeof p === 'boolean') &&
    Array.isArray(s.ducks) && s.ducks.length <= 80 &&
    s.ducks.every(d => d && DUCKS.some(t => t.id === d.type) &&
      Number.isInteger(d.row) && d.row >= 0 && d.row < ROWS &&
      Number.isFinite(d.x) && Number.isFinite(d.hp) && Number.isFinite(d.maxHp) &&
      d.maxHp > 0 && Number.isFinite(d.speed) && Number.isFinite(d.anim)) &&
    Array.isArray(s.peas) && s.peas.length <= 200 &&
    s.peas.every(p => p && Number.isInteger(p.row) && p.row >= 0 && p.row < ROWS &&
      Number.isFinite(p.x) && Number.isFinite(p.y)) &&
    Array.isArray(s.coins) && s.coins.length <= 80 &&
    s.coins.every(c => c && Number.isFinite(c.x) && Number.isFinite(c.y) &&
      Number.isFinite(c.life) && Number.isSafeInteger(c.id)) &&
    Array.isArray(s.mowers) && s.mowers.length === ROWS &&
    s.mowers.every(m => m && typeof m.used === 'boolean' &&
      typeof m.running === 'boolean' && Number.isFinite(m.x)) &&
    Array.isArray(s.plantCooldown) && s.plantCooldown.length === PLANTS.length &&
    s.plantCooldown.every(Number.isFinite) &&
    Array.isArray(s.duckCooldown) && s.duckCooldown.length === DUCKS.length &&
    s.duckCooldown.every(Number.isFinite) &&
    Number.isFinite(s.time) && s.time >= 0 && Number.isFinite(s.zombieIncome) &&
    Number.isSafeInteger(s.ackGuest) && s.ackGuest >= 0 &&
    Number.isSafeInteger(s.nextId) && s.nextId >= 1 &&
    Number.isInteger(s.left) && s.left >= 0 && s.left <= WAVE &&
    Number.isFinite(s.plantCash) && s.plantCash >= 0 &&
    Number.isFinite(s.zombieCash) && s.zombieCash >= 0 &&
    (s.winner === '' || s.winner === 'plants' || s.winner === 'zombies'));
}

export function applyCommand(s, role, cmd) {
  if (!validMatch(s) || s.winner || !cmd || typeof cmd !== 'object') return false;
  if (role === 'plants') {
    if (cmd.kind === 'coin') {
      const index = s.coins.findIndex(c => c.id === (cmd.coinId ?? cmd.id));
      if (index < 0) return false;
      s.coins.splice(index, 1);
      s.plantCash += coinValue;
      return true;
    }
    if (cmd.kind !== 'plant') return false;
    const r = cmd.row, c = cmd.col, t = cmd.type;
    if (!Number.isInteger(r) || r < 0 || r >= ROWS ||
        !Number.isInteger(c) || c < 0 || c >= COLS ||
        !Number.isInteger(t) || t < 0 || t >= PLANTS.length) return false;
    const p = PLANTS[t], index = cell(r, c), water = waterRow(s, r);
    if (s.plants[index] || s.plantCash < p.cost || s.plantCooldown[t] > 0 ||
        (t === 4 ? (!water || s.lilies[index]) : (water && !s.lilies[index]))) return false;
    if (t === 4) s.lilies[index] = true;
    else s.plants[index] = {type: t, hp: p.hp, fire: t === 2 ? 5 : .4};
    s.plantCash -= p.cost;
    s.plantCooldown[t] = p.cooldown;
    return true;
  }
  if (role === 'zombies') {
    if (cmd.kind === 'finish') {
      s.left = 0;
      if (!s.ducks.length) s.winner = 'plants';
      return true;
    }
    if (cmd.kind !== 'spawn') return false;
    const t = DUCKS.findIndex(d => d.id === cmd.type), r = cmd.row;
    if (!Number.isInteger(r) || r < 0 || r >= ROWS || t < 0 ||
        s.left <= 0 || s.ducks.length >= 70 ||
        s.zombieCash < DUCKS[t].cost || s.duckCooldown[t] > 0) return false;
    const d = DUCKS[t];
    s.ducks.push({id: s.nextId++, type: d.id, row: r,
                  x: W + 45 + (s.nextId % 3) * 12,
                  hp: d.hp, maxHp: d.hp, speed: 24, anim: 0});
    s.left--;
    s.zombieCash -= d.cost;
    s.duckCooldown[t] = d.cooldown;
    return true;
  }
  return false;
}

export function stepMatch(s, dt) {
  if (!validMatch(s) || s.winner) return;
  dt = Math.min(.05, Math.max(0, Number(dt) || 0));
  s.time += dt;
  for (let i = 0; i < PLANTS.length; i++) s.plantCooldown[i] = Math.max(0, s.plantCooldown[i] - dt);
  for (let i = 0; i < DUCKS.length; i++) s.duckCooldown[i] = Math.max(0, s.duckCooldown[i] - dt);
  s.zombieIncome += dt;
  if (s.zombieIncome >= 5) {
    s.zombieIncome -= 5;
    s.zombieCash = Math.min(3000, s.zombieCash + 75);
  }

  for (let r = 0; r < ROWS; r++) for (let c = 0; c < COLS; c++) {
    const i = cell(r, c), p = s.plants[i];
    if (!p) continue;
    if (p.hp <= 0) { s.plants[i] = null; continue; }
    if (p.type === 0) {
      p.fire -= dt;
      if (p.fire <= 0 && s.ducks.some(d => d.row === r && d.x > centerX(c))) {
        if (s.peas.length < 190) s.peas.push({row: r, x: centerX(c) + 16, y: centerY(r) - 6});
        p.fire = 1.4;
      }
    } else if (p.type === 2) {
      p.fire -= dt;
      if (p.fire <= 0) {
        if (s.coins.length < 70) s.coins.push({id: s.nextId++, x: centerX(c) + 26, y: centerY(r) + 28, life: 16});
        p.fire = 8;
      }
    }
  }

  for (const pea of s.peas) {
    pea.x += 540 * dt;
    const d = s.ducks.find(z => z.row === pea.row && pea.x >= z.x - 25 && pea.x <= z.x + 21);
    if (d) { d.hp -= 20; pea.hit = true; }
  }
  s.peas = s.peas.filter(p => !p.hit && p.x < W + 20);
  s.ducks = s.ducks.filter(d => d.hp > 0);

  for (const d of s.ducks) {
    d.anim += dt * 8;
    const c = Math.min(COLS - 1, Math.max(0, Math.floor((d.x - X) / CW)));
    const i = cell(d.row, c), p = s.plants[i];
    if (d.x <= X + 70) {
      const m = s.mowers[d.row];
      if (!m.used) { m.used = true; m.running = true; }
      else if (!m.running && d.x < X - 25) { s.winner = 'zombies'; break; }
    }
    if (d.x <= centerX(c) + 28 && p?.type === 3) {
      s.plants[i] = null;
      d.x += 3 * CW;
    } else if (d.x <= centerX(c) + 28 && p) {
      p.hp -= 100 * dt;
    } else if (d.x <= centerX(c) + 28 && s.lilies[i]) {
      s.lilies[i] = false;
    } else d.x -= d.speed * dt;
  }
  for (let r = 0; r < ROWS; r++) {
    const m = s.mowers[r];
    if (!m.running) continue;
    m.x += 520 * dt;
    s.ducks = s.ducks.filter(d => !(d.row === r && d.x < m.x + 40 && d.x > m.x - 25));
    if (m.x > W + 40) m.running = false;
  }
  s.coins = s.coins.filter(c => (c.life -= dt) > 0);
  if (!s.winner && s.left === 0 && s.ducks.length === 0) s.winner = 'plants';
}
