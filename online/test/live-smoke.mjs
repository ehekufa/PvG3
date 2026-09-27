/* Opt-in live connectivity check. This is NOT part of the offline unit tests.
 * Writes one uniquely named, non-game room into the supplied Firebase RTDB,
 * reads it back and deletes it, without touching existing records. Run from
 * GitHub Actions manually with firebase_smoke=true (runner has network access).
 */
import {randomBytes, randomUUID} from 'node:crypto';
import {chooseRole, getRoom, writeCommand, writeState} from '../firebase.js';
import {newMatch, applyCommand, validMatch} from '../rules.js';

const origins = ['https://ehekufa.github.io', 'https://raw.githack.com'];
const origin = origins.at(-1); // The APK's temporary browser host.
const base = 'https://pvg3-ae824-default-rtdb.firebaseio.com/rooms';
const alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
const id = 'Q' + Array.from(randomBytes(5), n => alphabet[n & 31]).join('');
const url = `${base}/${id}.json`;
const marker = randomUUID();
const assert = (ok, message) => {if (!ok) throw new Error(message);};

let owned = false;
try {
  for (const browserOrigin of origins) {
    const list = await fetch(`${base}.json`, {headers:{Origin:browserOrigin}});
    console.log(`GET /rooms (${browserOrigin}): HTTP ${list.status}`);
    assert(list.ok, 'Firebase /rooms read is denied or the database is unavailable.');
    assert(['*', browserOrigin].includes(list.headers.get('access-control-allow-origin')),
           'GET lacks Access-Control-Allow-Origin: browser requests would fail.');

    const preflight = await fetch(url, {method:'OPTIONS', headers:{
      Origin: browserOrigin, 'Access-Control-Request-Method':'PUT',
      'Access-Control-Request-Headers':'content-type,if-match',
    }});
    console.log(`OPTIONS /rooms/${id} (${browserOrigin}): HTTP ${preflight.status}`);
    assert(preflight.ok && ['*', browserOrigin].includes(preflight.headers.get('access-control-allow-origin')),
           'CORS preflight rejected: creating a room from a browser would fail.');
    const allowed = (preflight.headers.get('access-control-allow-headers') || '').toLowerCase();
    assert(allowed === '*' || (allowed.includes('content-type') && allowed.includes('if-match')),
           'CORS preflight does not allow Content-Type and If-Match.');
    console.log(`Browser GET and conditional PUT allowed for ${browserOrigin}`);
  }

  if (!process.argv.includes('--write')) {
    console.log('Write check skipped; pass --write explicitly to create and remove one test room.');
  } else {
    const put = await fetch(url, {method:'PUT', headers:{
      Origin:origin, 'Content-Type':'application/json', 'If-Match':'null_etag',
    }, body:JSON.stringify({version:0, smoke:true, marker,
      host:{id:'a'.repeat(32), role:null, ping:Date.now()}})});
    console.log(`PUT /rooms/${id}: HTTP ${put.status}`);
    assert(put.ok, 'Firebase denied room creation; configure /rooms rules before playing.');
    owned = true;
    const get = await fetch(url, {headers:{Origin:origin, 'X-Firebase-ETag':'true'}});
    assert(get.ok, 'Room was created but cannot be read.');
    const saved = await get.json();
    assert(saved?.marker === marker, 'Test room changed unexpectedly; refusing to delete another value.');
    console.log('Temporary room created and read back successfully.');

    // Exercise the same REST fields used in a real two-player match. Keep
    // version:0 so this disposable record never appears as a playable room.
    const guestId = 'b'.repeat(32);
    const seat = await fetch(`${base}/${id}/guest.json`, {method:'PUT', headers:{
      Origin:origin, 'Content-Type':'application/json', 'If-Match':'null_etag',
    }, body:JSON.stringify({id:guestId, role:null, ping:Date.now()})});
    assert(seat.ok, `Second player could not claim a free seat: HTTP ${seat.status}`);
    await chooseRole(id, 'host', 'plants');
    await chooseRole(id, 'guest', 'zombies');
    const state = newMatch(5);
    await writeState(id, state);
    await writeCommand(id, {id:guestId, seq:1, kind:'spawn', type:2, row:1});
    const room = await getRoom(id);
    assert(room.marker === marker && room.host.role === 'plants' &&
      room.guest.id === guestId && room.guest.role === 'zombies' &&
      validMatch(room.state) && room.command.seq === 1,
      'Two-player room fields did not round-trip.');
    assert(applyCommand(state, room.guest.role, room.command),
      'Host rejected guest duck command unexpectedly.');
    state.ackGuest = 1;
    await writeState(id, state);
    const updated = await getRoom(id);
    assert(updated.state.ackGuest === 1 && updated.state.ducks[0].hp === 420,
      'Guest command was not reflected in the host snapshot.');
    console.log('Two-player roles, duck command and acknowledgement round-trip succeeded.');
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
