/* Opt-in live connectivity check. This is NOT part of the offline unit tests.
 * Writes one uniquely named, non-game room into the supplied Firebase RTDB,
 * reads it back and deletes it, without touching existing records. Run from
 * GitHub Actions manually with firebase_smoke=true (runner has network access).
 */
import {randomBytes, randomUUID} from 'node:crypto';

const origin = 'https://ehekufa.github.io';
const base = 'https://pvg3-ae824-default-rtdb.firebaseio.com/rooms';
const alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
const id = 'Q' + Array.from(randomBytes(5), n => alphabet[n & 31]).join('');
const url = `${base}/${id}.json`;
const marker = randomUUID();
const assert = (ok, message) => {if (!ok) throw new Error(message);};

let owned = false;
try {
  const list = await fetch(`${base}.json?shallow=true`, {headers:{Origin:origin}});
  console.log(`GET /rooms: HTTP ${list.status}`);
  assert(list.ok, 'Firebase /rooms read is denied or the database is unavailable.');
  assert(['*', origin].includes(list.headers.get('access-control-allow-origin')),
         'GET lacks Access-Control-Allow-Origin: browser requests would fail.');
  console.log('GET /rooms: CORS permitted');

  const preflight = await fetch(url, {method:'OPTIONS', headers:{
    Origin: origin, 'Access-Control-Request-Method':'PUT',
    'Access-Control-Request-Headers':'content-type,if-match',
  }});
  console.log(`OPTIONS /rooms/${id}: HTTP ${preflight.status}`);
  assert(preflight.ok && ['*', origin].includes(preflight.headers.get('access-control-allow-origin')),
         'CORS preflight rejected: creating a room from a browser would fail.');
  const allowed = (preflight.headers.get('access-control-allow-headers') || '').toLowerCase();
  assert(allowed === '*' || (allowed.includes('content-type') && allowed.includes('if-match')),
         'CORS preflight does not allow Content-Type and If-Match.');
  console.log('OPTIONS: browser PUT + If-Match permitted');

  if (!process.argv.includes('--write')) {
    console.log('Write check skipped; pass --write explicitly to create and remove one test room.');
  } else {
    const put = await fetch(url, {method:'PUT', headers:{
      Origin:origin, 'Content-Type':'application/json', 'If-Match':'null_etag',
    }, body:JSON.stringify({version:0, smoke:true, marker})});
    console.log(`PUT /rooms/${id}: HTTP ${put.status}`);
    assert(put.ok, 'Firebase denied room creation; configure /rooms rules before playing.');
    owned = true;
    const get = await fetch(url, {headers:{Origin:origin, 'X-Firebase-ETag':'true'}});
    assert(get.ok, 'Room was created but cannot be read.');
    const saved = await get.json();
    assert(saved?.marker === marker, 'Test room changed unexpectedly; refusing to delete another value.');
    console.log('Temporary room created and read back successfully.');
  }
} finally {
  if (owned) {
    const get = await fetch(url, {headers:{'X-Firebase-ETag':'true'}});
    const saved = get.ok ? await get.json() : null;
    if (saved?.marker !== marker) {
      console.error(`Test room ${id} changed; leaving it intact instead of deleting someone else's data.`);
      process.exitCode = 1;
    } else {
      const etag = get.headers.get('etag');
      if (!etag) {console.error(`No ETag for ${id}; refusing unsafe deletion.`);process.exitCode = 1;}
      else {
        const result = await fetch(url, {method:'DELETE', headers:{'If-Match':etag}});
        console.log(`DELETE /rooms/${id}: HTTP ${result.status}`);
        assert(result.ok, `Could not remove temporary room ${id}.`);
      }
    }
  }
}
