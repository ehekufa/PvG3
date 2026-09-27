import test from 'node:test';
import assert from 'node:assert/strict';
import {validId, listRooms, createRoom, getRoom, joinRoom, chooseRole,
        encodeState, decodeState, writeState, writeCommand, leaveRoom} from '../firebase.js';
import {newMatch, validMatch, applyCommand} from '../rules.js';

// Fake the RTDB REST surface; never write fixtures to the user's real DB.
test('room creation, list, compare-and-set join, role choice, actions and exit', async () => {
  const rooms = {};
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (url, opts) => {
    const route = new URL(url);
    assert.equal(route.hostname, 'pvg3-ae824-default-rtdb.firebaseio.com');
    assert(route.pathname.endsWith('.json'));
    const segments = route.pathname.slice(1,-5).split('/');
    let parent = rooms, key = segments[0];
    for(let i=0;i<segments.length-1;i++) {
      key=segments[i];
      if (!parent[key]) parent[key]={};
      parent=parent[key];
    }
    key=segments.at(-1);
    if (segments[0] === 'rooms' && segments.length === 1) {
      assert.equal(route.searchParams.get('shallow'), null);
      return reply(200,rooms.rooms||null);
    }
    const method = opts.method || 'GET';
    if (method === 'GET') return reply(200,parent[key] ?? null);
    if (method === 'PUT') {
      if (opts.headers['If-Match'] === 'null_etag' && parent[key] != null)
        return reply(412,{error:'precondition failed'});
      // RTDB drops null children and empty arrays rather than storing them.
      const value=firebaseNormalize(JSON.parse(opts.body));
      if(value===undefined) delete parent[key];
      else parent[key]=value;
      return reply(200,parent[key]??null);
    }
    if (method === 'DELETE') {delete parent[key];return reply(200,null);}
    throw new Error('Unexpected mock method '+method);
  };
  try {
    const host='a'.repeat(32),guest='b'.repeat(32);
    const created=await createRoom(host,5);
    assert(validId(created.id));
    // Abandoned rooms whose codes sort after this one must not hide it.
    const alphabet='ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
    for (let i=0;i<32;i++) rooms.rooms[`ZZAA${alphabet[0]}${alphabet[i]}`]={
      version:1,host:{id:host,ping:Date.now()-11*60*1000},guest:null,
    };
    assert.deepEqual((await listRooms()).map(({id})=>id),[created.id]);
    assert.equal((await getRoom(created.id)).map,5);
    const joined=await joinRoom(created.id,guest);
    assert.equal(joined.guest.id,guest);
    assert.deepEqual(await listRooms(),[]); // occupied rooms are not advertised
    await assert.rejects(joinRoom(created.id,'c'.repeat(32)),/занята/);
    await chooseRole(created.id,'host','plants');
    await chooseRole(created.id,'guest','zombies');
    assert.equal((await getRoom(created.id)).host.role,'plants');
    const match=newMatch(5);
    await writeState(created.id,match);
    await writeCommand(created.id,{seq:1,kind:'spawn',type:2,row:4,id:guest});
    const active=await getRoom(created.id);
    assert(validMatch(active.state));
    assert.deepEqual(active.state.ducks,[]);
    assert.equal(active.state.plants.length,45);
    assert(active.state.plants.every(p=>p===null));
    assert.equal(active.command.type,2);
    assert(applyCommand(match,'zombies',active.command));
    match.ackGuest=1;
    await writeState(created.id,match);
    const after=await getRoom(created.id);
    assert(validMatch(after.state));
    assert.equal(after.state.ducks[0].hp,420);
    assert.equal(after.state.ackGuest,1);
    await leaveRoom(created.id,'guest',guest);
    assert.equal((await getRoom(created.id)).guest ?? null,null);
    await leaveRoom(created.id,'host',host);
    assert.equal(await getRoom(created.id),null);
  } finally {globalThis.fetch=originalFetch;}
});
test('wire encoding keeps Firebase from dropping empty arrays or null plants', () => {
  const original=newMatch();
  const wire=firebaseNormalize(encodeState(original));
  assert.equal(wire.plants.length,45);
  assert.equal(wire.ducks.length,1);
  assert.equal(wire.ducks[0],false);
  assert(validMatch(decodeState(wire)));
  assert(original.plants.every(p=>p===null)); // no mutation of the host state
});
function firebaseNormalize(value) {
  if(value===null) return undefined;
  if(Array.isArray(value)) {
    if(!value.length) return undefined;
    const list=value.map(firebaseNormalize);
    return list.every(v=>v===undefined)?undefined:list;
  }
  if(value && typeof value==='object') {
    const out={};
    for(const [key,entry] of Object.entries(value)) {
      const stored=firebaseNormalize(entry);
      if(stored!==undefined) out[key]=stored;
    }
    return Object.keys(out).length?out:undefined;
  }
  return value;
}
function reply(status, body) {
  return {ok:status<400,status,text:async()=>JSON.stringify(body)};
}
