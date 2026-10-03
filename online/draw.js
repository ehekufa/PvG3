/* Canvas view of the same user-drawn PvG3 maps, plants, duck, and mower. */
import {W, H, ROWS, COLS, X, Y, CW, CH, PLANTS, DUCKS, WAVE} from './rules.js';
const art = {};
const pictures = [
  'lawn-map.png','water-map.png','lawnmower.png','coin-token.png',
  ...DUCKS.map(d => d.image), ...PLANTS.map(p => p.image),
];
export function preloadArtwork() {
  return Promise.all([...new Set(pictures)].map(file => new Promise(resolve => {
    const image = new Image();
    image.onload = resolve; image.onerror = resolve;
    image.src = `../assets/art/${file}`;
    art[file] = image;
  })));
}
function image(ctx, file, x, y, w, h, flip = false) {
  const pic = art[file];
  if (!pic?.naturalWidth) return false;
  if (flip) { ctx.save(); ctx.translate(x + w, y); ctx.scale(-1, 1); ctx.drawImage(pic, 0, 0, w, h); ctx.restore(); }
  else ctx.drawImage(pic, x, y, w, h);
  return true;
}
function crop(ctx, file, sx, sy, sw, sh, x, y, w, h) {
  const pic = art[file];
  if (pic?.naturalWidth) ctx.drawImage(pic, sx, sy, sw, sh, x, y, w, h);
}
function box(ctx, x, y, w, h, color) {ctx.fillStyle = color;ctx.fillRect(x, y, w, h);}
function outline(ctx, x, y, w, h, width = 2) {
  ctx.strokeStyle = '#000';ctx.lineWidth = width;ctx.strokeRect(x + width/2, y + width/2, w - width, h - width);
}
function label(ctx, text, x, y, size = 20, color = '#f5f5f5', align = 'left') {
  ctx.fillStyle = color; ctx.textAlign = align; ctx.textBaseline = 'top';
  ctx.font = `bold ${size}px PTSans, sans-serif`;ctx.fillText(text, x, y);
}
function duck(ctx, x, y, size, type, flip = true) {
  const file = DUCKS.find(d => d.id === type)?.image || 'zombie-duck.png';
  image(ctx, file, x, y, size, size, flip);
}
function background(ctx, s) {
  box(ctx, 0, 0, W, H, '#d8e9bf');
  crop(ctx, 'lawn-map.png', 0, 0, 246, 500, 0, 0, X - 7, H);
  if (s.map === 5) {
    crop(ctx, 'water-map.png', 274, 0, 226, 100, X, Y, COLS * CW, CH);
    crop(ctx, 'water-map.png', 274, 128, 226, 132, X, Y + CH, COLS * CW, 2 * CH);
    crop(ctx, 'water-map.png', 274, 290, 226, 210, X, Y + 3 * CH, COLS * CW, 2 * CH);
  } else crop(ctx, 'lawn-map.png', 274, 0, 226, 500, X, Y, COLS * CW, ROWS * CH);
  if (!art[s.map === 5 ? 'water-map.png' : 'lawn-map.png']?.naturalWidth) {
    box(ctx, X, Y, COLS * CW, ROWS * CH, '#76a843');
    if (s.map === 5) box(ctx, X, Y + CH, COLS * CW, 2 * CH, '#54adba');
  }
  ctx.strokeStyle = '#000';ctx.lineWidth = 2;
  for (let c = 0; c <= COLS; c++) {ctx.beginPath();ctx.moveTo(X + c*CW, Y);ctx.lineTo(X + c*CW, Y + ROWS*CH);ctx.stroke();}
  for (let r = 0; r <= ROWS; r++) {ctx.beginPath();ctx.moveTo(X, Y + r*CH);ctx.lineTo(W, Y + r*CH);ctx.stroke();}
  box(ctx, X - 7, 0, 7, H, '#242424');
}
function cards(ctx, s, role, selected, pending) {
  box(ctx, 0, 0, X - 7, 130, '#242424');
  outline(ctx, 0, 0, X - 7, 130, 2);
  label(ctx, role === 'plants' ? 'МОНЕТЫ РАСТЕНИЙ' : 'МОНЕТЫ ЗОМБИ', 14, 17, 20);
  image(ctx, 'coin-token.png', 8, 50, 46, 46);
  label(ctx, String(role === 'plants' ? s.plantCash : s.zombieCash),
    62, 56, 45, '#fff');
  const defs = role === 'plants' ? PLANTS : DUCKS;
  defs.forEach((d, i) => {
    const y = role === 'plants' ? 137 + i*107 : 169 + i*155;
    const h = role === 'plants' ? 98 : 136;
    const available = (role === 'plants' ? s.plantCash >= d.cost && s.plantCooldown[i] <= 0 && (i !== 4 || s.map === 5)
      : s.zombieCash >= d.cost && s.duckCooldown[i] <= 0 && s.left > 0) && !pending;
    box(ctx, 14, y, 219, h, '#000');
    box(ctx, 18, y+4, 211, h-8, selected === i ? '#bdbdbd' : '#fff');
    outline(ctx, 14, y, 219, h, selected === i ? 5 : 3);
    if (role === 'plants') image(ctx, d.image, 167, y+8, 54, 54);
    else duck(ctx, 148, y+15, 74, d.id);
    let text = d.name.toUpperCase();
    if (text.length > 17) text = text.slice(0, 16) + '…';
    label(ctx, text, 32, y+12, role === 'plants' ? 16 : 18, '#111');
    image(ctx, 'coin-token.png', 31, y+h-34, 22, 22);
    label(ctx, `${d.cost}  ·  ${role === 'plants' ? d.hp : d.hp+' HP'}`,
      59, y+h-33, 17, '#111');
    const cool = role === 'plants' ? s.plantCooldown[i] : s.duckCooldown[i];
    if (!available) box(ctx, 20, y+5, 207, h-10, '#444444aa');
    if (cool > 0) label(ctx, `${cool.toFixed(1)} с`, 32, y+40, 18, '#fff');
  });
  if (role === 'zombies') label(ctx, 'НАЖМИ НА РЯД ПОСЛЕ ВЫБОРА УТКИ', 19, 657, 16, '#e5e5e5');
}
export function drawGame(canvas, s, {role, selected, pending = false, id = '', liveDelay = 0} = {}) {
  if (!s) return;
  const ctx = canvas.getContext('2d', {alpha: false});
  ctx.clearRect(0, 0, W, H);
  background(ctx, s);
  for (let r = 0; r < ROWS; r++) {
    const m = s.mowers[r];
    if (!m.used || m.running) image(ctx, 'lawnmower.png', m.x-44, Y+r*CH+31, 88, 86);
    for (let c = 0; c < COLS; c++) {
      const i = r*COLS+c, x = X+c*CW+CW/2, y = Y+r*CH+CH/2;
      if (s.lilies[i]) image(ctx, 'lily-pad.png', x-51, y-1, 102, 54);
      const p = s.plants[i];
      if (p) image(ctx, PLANTS[p.type]?.image, x-43, y-48, 86, 86);
    }
    for (const d of s.ducks) if (d.row === r) {
      const position = d.x - (liveDelay ? d.speed * Math.min(liveDelay,.4) : 0);
      duck(ctx, position-46, Y+r*CH+CH/2-50+Math.sin(d.anim)*2, 92, d.type);
      if (d.hp < d.maxHp) {
        box(ctx, position-33, Y+r*CH+8, 66, 6, '#000');
        box(ctx, position-31, Y+r*CH+10, 62, 2, '#d98a76');
        box(ctx, position-31, Y+r*CH+10, 62*Math.max(0,d.hp/d.maxHp), 2, '#f3bc6c');
        outline(ctx, position-33, Y+r*CH+8, 66, 6, 1);
      }
    }
  }
  ctx.fillStyle='#a8e664';for (const p of s.peas){ctx.beginPath();ctx.arc(p.x,p.y,9,0,7);ctx.fill();}
  for (const c of s.coins)
    image(ctx, 'coin-token.png', c.x - 22, c.y - 22, 44, 44);
  box(ctx, X, 0, W-X, Y-1, '#242424');
  outline(ctx, X, 0, W-X, Y-1, 2);
  label(ctx, `ОНЛАЙН • ${id}`, 271, 19, 25, '#fff');
  label(ctx, role === 'plants' ? 'ТЫ: РАСТЕНИЯ' : 'ТЫ: ЗОМБИ-УТКИ', 635, 22, 23);
  label(ctx, `УТОК ОСТАЛОСЬ: ${s.left + s.ducks.length} / ${WAVE}`, 275, 72, 20, '#d0d0d0');
  box(ctx, 924, 26, 154, 66, '#ffffff');
  outline(ctx, 924, 26, 154, 66, 3);
  label(ctx, 'КНИГА', 1001, 47, 23, '#111','center');
  box(ctx, 1099, 26, 165, 66, '#000');
  outline(ctx, 1099, 26, 165, 66, 3);
  label(ctx, 'ВЫЙТИ', 1181, 47, 23, '#fff','center');
  cards(ctx, s, role, selected, pending);
  if (s.winner) {
    box(ctx, 243, 119, W-243, H-119, '#111111cc');
    label(ctx, s.winner === role ? 'ПОБЕДА!' : 'ПОРАЖЕНИЕ', 770, 290, 64,
      '#fff','center');
    label(ctx, s.winner === 'plants' ? 'ВСЕ УТКИ ОСТАНОВЛЕНЫ' : 'УТКИ ПРОРВАЛИ ЗАЩИТУ',
      770, 380, 28, '#d0d0d0','center');
  }
}
