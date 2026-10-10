import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {request} from '../firebase.js';
import {LOGIN_PATTERN, normalizeLogin, validLogin, validPassword, accountSalt,
        passwordHash, createAccount, signIn, signOut, currentSession, isModerator,
        loadBans, isBanned, loadComments, postComment, hideComment, banAccount,
        unbanAccount, setLevelOfficial, rotateToken} from '../accounts.js';
import {newDraft, addObject, publishedRecord} from '../workshop.js';
import {DATABASE_HOST as CONFIGURED_HOST} from '../firebase-config.js';

function reply(status, body) {
  return new Response(JSON.stringify(body), {status,
    headers: {'Content-Type': 'application/json'}});
}

/* A tiny stand-in for firebase/database.rules.json: it repeats the decisions the
 * real rules make, so the client flow is checked against the same contract. */
function fakeDatabase({admins = ['qwertyuiopaj1234']} = {}) {
  const db = {accounts: {}, tokens: {}, bans: {}, comments: {}, levels: {},
    'levels-index': {}, admins: Object.fromEntries(admins.map(login => [login, true]))};
  const node = segments => {
    let parent = db;
    for (const key of segments.slice(0, -1)) {
      if (parent[key] == null || typeof parent[key] !== 'object') parent[key] = {};
      parent = parent[key];
    }
    return {parent, key: segments.at(-1)};
  };
  const deny = {status: 403, body: {error: 'Permission denied'}};
  const check = (login, tok) =>
    db.tokens[login] && db.tokens[login].token === tok ? null : deny;
  const isAdmin = (login, tok) => db.admins[login] === true && !check(login, tok);

  globalThis.fetch = async (url, options = {}) => {
    const route = new URL(url);
    assert.equal(route.hostname, CONFIGURED_HOST);
    const segments = route.pathname.slice(1, -5).split('/');
    const {parent, key} = node(segments);
    const method = options.method || 'GET';
    if (method === 'GET') return reply(200, parent[key] ?? null);
    const body = options.body ? JSON.parse(options.body) : null;
    if (segments[0] === 'accounts') {
      db.lastAccountIfMatch = options.headers?.['If-Match'] || '';
      if (!LOGIN_PATTERN.test(key) || !body?.salt || !body?.hash) return reply(...[deny.status, deny.body]);
      if (parent[key] && options.headers['If-Match'] === 'null_etag')
        return reply(412, {error: 'precondition failed'});
      if (parent[key] && (!body.proof || body.proof !== db.tokens[key]?.token))
        return reply(...[deny.status, deny.body]);
      parent[key] = {...body};
      return reply(200, parent[key]);
    }
    if (segments[0] === 'tokens') {
      if (!LOGIN_PATTERN.test(key) || body?.hash !== db.accounts[key]?.hash ||
        db.bans[key]?.banned === true)
        return reply(...[deny.status, deny.body]);
      parent[key] = {...body};
      return reply(200, parent[key]);
    }
    if (segments[0] === 'bans') {
      if (body?.banned !== true && body?.banned !== false) return reply(...[deny.status, deny.body]);
      if (!isAdmin(body.by, body.tok)) return reply(...[deny.status, deny.body]);
      parent[key] = {...body};
      return reply(200, parent[key]);
    }
    if (segments[0] === 'comments') {
      const [, level, id] = segments;
      if (segments.length === 3 && !parent[key]) {
        if (check(body.login, body.tok)) return reply(...[deny.status, deny.body]);
        if (db.bans[body.login]?.banned === true) return reply(...[deny.status, deny.body]);
        if (typeof body.text !== 'string' || body.text.length > 300)
          return reply(...[deny.status, deny.body]);
      } else if (body?.hidden === true) {
        const untouched = body.login === parent[key]?.login && body.text === parent[key]?.text;
        const mine = untouched && body.by === parent[key]?.login && !check(body.by, body.tok);
        if (!untouched || (!mine && !isAdmin(body.by, body.tok)))
          return reply(...[deny.status, deny.body]);
      } else return reply(...[deny.status, deny.body]);
      parent[key] = {...(parent[key] || {}), ...body};
      return reply(200, parent[key]);
    }
    if (segments[0] === 'levels') {
      if (!LOGIN_PATTERN.test(body?.author?.login || '') ||
          check(body.author.login, body.author.tok))
        return reply(...[deny.status, deny.body]);
      if (body?.official === true && !isAdmin(body.author.login, body.author.tok))
        return reply(...[deny.status, deny.body]);
      if (parent[key] && !isAdmin(body.author.login, body.author.tok) &&
          parent[key].author?.login !== body.author.login)
        return reply(...[deny.status, deny.body]);
      if (db.bans[body.author.login]?.banned === true)
        return reply(...[deny.status, deny.body]);
      parent[key] = {...body};
      return reply(200, parent[key]);
    }
    if (segments[0] === 'levels-index') {
      const record = db.levels[key], levelAuthor = record?.author;
      if (levelAuthor == null || typeof levelAuthor !== 'object' ||
          body?.author !== levelAuthor.login ||
          check(levelAuthor.login, levelAuthor.tok))
        return reply(...[deny.status, deny.body]);
      const rootHasOfficial = Object.hasOwn(record, 'official');
      const indexHasOfficial = Object.hasOwn(body || {}, 'official');
      if (rootHasOfficial !== indexHasOfficial ||
          (rootHasOfficial && body.official !== record.official))
        return reply(...[deny.status, deny.body]);
      parent[key] = {...body};
      return reply(200, parent[key]);
    }
    if (segments[0] === 'admins') return reply(...[deny.status, deny.body]);
    throw new Error(`Unexpected mock write: ${segments.join('/')}`);
  };
  return db;
}

function withFake(options, body) {
  const original = globalThis.fetch;
  const db = fakeDatabase(options);
  return Promise.resolve(body(db)).finally(() => {globalThis.fetch = original;});
}

test('the shipped rules file covers every branch the clients use', () => {
  const rules = JSON.parse(readFileSync(new URL('../../firebase/database.rules.json',
    import.meta.url), 'utf8')).rules;
  for (const branch of ['rooms', 'levels', 'levels-index', 'level-stats',
    'comments', 'accounts', 'tokens', 'admins', 'bans'])
    assert.ok(rules[branch], `rules describe /${branch}`);
  assert.equal(rules.admins['.write'], false, 'admins are seeded, never client-written');
  assert.equal(rules.accounts['.read'], false, 'password hashes stay private');
  assert.equal(rules.tokens['.read'], false, 'session tokens stay private');
  assert.ok(rules.accounts.$login['.write'].includes('[a-z0-9_]{3,24}'),
    'nicks allow no spaces and no punctuation');
  assert.ok(rules.levels.$id['.write'].includes("newData.child('author/login')"),
    'a published level must have an account login');
  assert.ok(rules.levels.$id['.write'].includes("newData.child('author/tok')"),
    'a published level must prove a live account session');
  assert.ok(rules['levels-index'].$id['.write'].includes("root.child('tokens')"),
    'the catalog index also requires an authenticated level author');
  assert.equal(rules['level-stats']['.read'], true,
    'catalog statistics are public');
  const likeRule = rules['level-stats'].$levelId.likes.$clientId['.write'];
  const dislikeRule = rules['level-stats'].$levelId.dislikes.$clientId['.write'];
  assert.ok(likeRule.includes('data.exists()') && likeRule.includes("child('dislikes')"),
    'likes can be toggled only while this installation has no dislike');
  assert.ok(dislikeRule.includes('data.exists()') && dislikeRule.includes("child('likes')"),
    'dislikes can be toggled only while this installation has no like');
  assert.equal(rules['level-stats'].$levelId.downloads, undefined,
    'the old download counter is no longer writable or displayed');
  assert.ok(rules.comments.$level.$cid['.write'].includes('300'), 'comments are bounded');
  assert.ok(rules.tokens.$login['.write'].includes('bans'),
    'a banned nick cannot even take a session token');
  assert.equal((rules.comments.$level.$cid['.write']
    .match(/newData\.child\('text'\)\.val\(\) === data\.child\('text'\)\.val\(\)/g) || []).length, 2,
    'hiding a comment may never rewrite its text');
});

test('nicks reject spaces, punctuation and duplicate-looking spellings', () => {
  for (const bad of ['', 'ab', 'a b', 'иван', 'QWE RTY', 'nick@name', 'ник!',
    'a'.repeat(25), '₽убль', 'a-b', 'a+b', 'ник'])
    assert.equal(validLogin(bad), false, `rejected: ${bad}`);
  for (const good of ['qwertyuiopaj1234', 'abc', 'a_b', 'a'.repeat(24), 'QWERTY', ' Player_1 '])
    assert.equal(validLogin(good), true, `accepted: ${good}`);
  assert.equal(normalizeLogin(' QwErTy '), 'qwerty');
  assert.equal(validPassword('short'), false);
  assert.equal(validPassword('a b c d e'), false);
  assert.equal(validPassword('1234567'), true);
});

test('the password hash depends on the nick and only on the password', async () => {
  const salt = await accountSalt('qwertyuiopaj1234');
  assert.match(salt, /^[a-f0-9]{64}$/);
  assert.equal(salt, await accountSalt('QWERTYUIOPAJ1234'), 'case is folded away');
  const hash = await passwordHash('qwertyuiopaj1234', 'secret-password');
  assert.match(hash, /^[a-f0-9]{64}$/);
  assert.equal(hash, await passwordHash('qwertyuiopaj1234', 'secret-password'));
  assert.notEqual(hash, await passwordHash('qwertyuiopaj1234', 'other-password'));
  assert.notEqual(hash, await passwordHash('someone_else', 'secret-password'));
});

test('create, sign in, refuse a duplicate nick and a wrong password', async () => {
  await withFake({}, async db => {
    signOut();
    const session = await createAccount('qwertyuiopaj1234', 'my-password');
    assert.equal(session.login, 'qwertyuiopaj1234');
    assert.equal(session.admin, true, 'the seeded admin list is respected');
    assert.equal(isModerator(), true);
    assert.match(db.accounts.qwertyuiopaj1234.hash, /^[a-f0-9]{64}$/);
    assert.equal(db.lastAccountIfMatch, '',
      'registration does not require read access to the private account branch');

    await assert.rejects(createAccount('qwertyuiopaj1234', 'another-one'),
      /Аккаунт занят/, 'the same nick cannot be taken twice');
    const savedDocument = globalThis.document;
    globalThis.document = {documentElement: {lang: 'en'}};
    try {
      await assert.rejects(createAccount(' QWERTYUIOPAJ1234 ', 'another-one'),
        /The nickname is taken/,
        'duplicate-looking nicks report the localized error in English too');
    } finally {
      if (savedDocument === undefined) delete globalThis.document;
      else globalThis.document = savedDocument;
    }
    await assert.rejects(createAccount('bad nick', 'another-one'), /a–z/);

    signOut();
    assert.equal(currentSession(), null);
    await signIn('qwertyuiopaj1234', 'my-password');
    assert.equal(currentSession().login, 'qwertyuiopaj1234');
    const rotated = currentSession().token;
    await assert.rejects(signIn('qwertyuiopaj1234', 'wrong-password'), /Неверный ник или пароль/);
    await assert.rejects(signIn('no_such_player', 'my-password'), /Неверный ник или пароль/);
    signOut();
    await signIn('qwertyuiopaj1234', 'my-password');
    assert.notEqual(currentSession().token, rotated, 'each sign-in issues a new token');
    signOut();
  });
});

test('a moderator marks a level official and bans a nick; players cannot', async () => {
  await withFake({}, async db => {
    const level = newDraft('moderated');
    addObject(level, 'block', 4, 4);
    const id = '4242';
    db.levels[id] = publishedRecord(id, level);

    await assert.rejects(setLevelOfficial(id, true), /войди/,
      'a guest cannot touch the official flag');
    await createAccount('qwertyuiopaj1234', 'my-password');
    signOut();
    await createAccount('mod_helper', 'helper-password');
    assert.equal(isModerator(), false);
    await assert.rejects(setLevelOfficial(id, true), /Только модератор/);

    signOut();
    await signIn('qwertyuiopaj1234', 'my-password');
    await setLevelOfficial(id, true);
    assert.equal(db.levels[id].official, true);
    assert.equal(db['levels-index'][id].official, true);
    await setLevelOfficial(id, false);
    assert.equal('official' in db.levels[id], false);

    await banAccount('rude_player', 'плохое сообщение');
    assert.equal(db.bans.rude_player.banned, true);
    assert.equal(isBanned(await loadBans(), 'rude_player'), true);
    await unbanAccount('rude_player', 'исправился');
    assert.equal(isBanned(await loadBans(), 'rude_player'), false);

    signOut();
    await signIn('mod_helper', 'helper-password');
    await assert.rejects(banAccount('someone', 'я так хочу'), /Permission denied|Firebase/);
    signOut();
  });
});

test('comments are posted under a nick, hidden by a moderator and blocked by a ban', async () => {
  await withFake({}, async db => {
    await createAccount('listener', 'listener-password');
    await assert.rejects(postComment('77', '   '), /Пустое сообщение/);
    await postComment('77', 'Отличный уровень!');
    let comments = await loadComments('77');
    assert.equal(comments.length, 1);
    assert.equal(comments[0].login, 'listener');
    assert.equal(comments[0].text, 'Отличный уровень!');
    await postComment('77', 'x'.repeat(400));
    const long = (await loadComments('77')).find(c => c.text.startsWith('xxxxxx'));
    assert.equal(long.text.length, 300, 'the client trims an over-long message');

    signOut();
    await createAccount('qwertyuiopaj1234', 'my-password');
    await postComment('77', 'Проверка модератора.');
    const mine = (await loadComments('77')).find(c => c.login === 'qwertyuiopaj1234');
    await hideComment('77', mine.id);
    comments = await loadComments('77');
    assert.equal(comments.find(c => c.id === mine.id).hidden, true);

    await banAccount('listener', 'непроходимый уровень');
    signOut();
    await assert.rejects(signIn('listener', 'listener-password'), /Неверный ник или пароль/,
      'a banned nick cannot take a session token any more');
    await assert.rejects(postComment('77', 'я всё равно напишу'), /войди/);
    signOut();
  });
});

test('hiding a comment keeps its text, and a ban locks the nick out', async () => {
  await withFake({}, async db => {
    await createAccount('qwertyuiopaj1234', 'my-password');
    await postComment('31', 'первое сообщение');
    const [first] = await loadComments('31');
    await hideComment('31', first.id);
    assert.equal(db.comments['31'][first.id].text, 'первое сообщение');
    assert.equal(db.comments['31'][first.id].hidden, true);
    await assert.rejects(request('comments/31/' + first.id, 'PUT',
      {login: first.login, text: 'подменённый текст', at: first.at, hidden: true,
        by: 'qwertyuiopaj1234', tok: currentSession().token}),
      /Permission denied|Firebase/, 'a moderator edits nothing but the hidden flag');

    await banAccount('spammer', 'плохое сообщение');
    assert.equal(isBanned(await loadBans(), 'spammer'), true);
    signOut();
  });
});

test('a rotation started before the player signed out does not bring them back', async () => {
  await withFake({}, async db => {
    await createAccount('listener', 'listener-password');
    const rotating = rotateToken();
    signOut();
    await rotating;
    assert.equal(currentSession(), null, 'a closed session stays closed');
    assert.equal(db.tokens.listener.token.length, 64, 'the token was still retired');
    signOut();
  });
});

test('Firebase rules reject direct guest writes to published levels', async () => {
  await withFake({admins: []}, async db => {
    signOut();
    const level = newDraft('anonymous');
    addObject(level, 'block', 3, 3);
    const record = publishedRecord('909', level);
    await assert.rejects(request('levels/909', 'PUT', record),
      /Permission denied|Firebase/);
    assert.equal(db.levels['909'], undefined,
      'a guest cannot bypass the interface with a direct REST write');
  });
});
