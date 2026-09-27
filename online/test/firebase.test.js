import test from 'node:test';
import assert from 'node:assert/strict';
import {validId, listRooms, createRoom, getRoom, joinRoom, chooseRole,
        writeState, writeCommand, leaveRoom} from '../firebase.js';

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
      assert.equal(route.searchParams.get('shallow'), 'true');
      return reply(200,Object.fromEntries(Object.keys(rooms.rooms||{}).map(k=>[k,true])));
    }
    const method = opts.method || 'GET';
    if (method === 'GET') return reply(200,parent[key] ?? null);
    if (method === 'PUT') {
      if (opts.headers['If-Match'] === 'null_etag' && parent[key] != null)
        return reply(412,{error:'precondition failed'});
      parent[key]=JSON.parse(opts.body);
      return reply(200,parent[key]);
    }
    if (method === 'DELETE') {delete parent[key];return reply(200,null);}
    throw new Error('Unexpected mock method '+method);
  };
  try {
    const host='a'.repeat(32),guest='b'.repeat(32);
    const created=await createRoom(host,5);
    assert(validId(created.id));
    assert.equal((await listRooms())[0],created.id);
    assert.equal((await getRoom(created.id)).map,5);
    const joined=await joinRoom(created.id,guest);
    assert.equal(joined.guest.id,guest);
    await assert.rejects(joinRoom(created.id,'c'.repeat(32)),/занята/);
    await chooseRole(created.id,'host','plants');
    await chooseRole(created.id,'guest','zombies');
    assert.equal((await getRoom(created.id)).host.role,'plants');
    await writeState(created.id,{version:1,ackGuest:0});
    await writeCommand(created.id,{seq:1,kind:'spawn',type:2,row:4,id:guest});
    const active=await getRoom(created.id);
    assert.equal(active.state.version,1);
    assert.equal(active.command.type,2);
    await leaveRoom(created.id,'guest',guest);
    assert.equal((await getRoom(created.id)).guest ?? null,null);
    await leaveRoom(created.id,'host',host);
    assert.equal(await getRoom(created.id),null);
  } finally {globalThis.fetch=originalFetch;}
});
function reply(status, body) {
  return {ok:status<400,status,text:async()=>JSON.stringify(body)};
}
