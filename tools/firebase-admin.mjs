#!/usr/bin/env node
/* Manage the PvG3 Firebase database from the command line.
 *
 * The database secret never lives in the repository: it is read from the
 * environment, so nothing private can be committed by accident.
 *
 *   export PVG3_DATABASE_HOST='<адрес базы>'
 *   export PVG3_DATABASE_SECRET='…'   # Project settings → Service accounts →
 *                                     # Database secrets (Realtime Database)
 *
 *   node tools/firebase-admin.mjs rules          # publish firebase/database.rules.json
 *   node tools/firebase-admin.mjs show-rules     # print the rules now deployed
 *   node tools/firebase-admin.mjs admin qwertyuiopaj1234
 *   node tools/firebase-admin.mjs unadmin <nick>
 *   node tools/firebase-admin.mjs list-admins
 *   node tools/firebase-admin.mjs ban <nick> 'плохие сообщения'
 *   node tools/firebase-admin.mjs unban <nick>
 *   node tools/firebase-admin.mjs list-accounts
 *
 * Requests made with the database secret bypass the security rules, which is
 * exactly what is needed to seed the first moderator.
 */
import {readFileSync} from 'node:fs';

const host = process.env.PVG3_DATABASE_HOST || '';
const secret = process.env.PVG3_DATABASE_SECRET || '';
const rulesPath = new URL('../firebase/database.rules.json', import.meta.url);

function die(message) {
  console.error(`\n${message}\n`);
  process.exit(1);
}

async function call(path, method = 'GET', body) {
  const url = `https://${host}/${path}.json?auth=${encodeURIComponent(secret)}`;
  const response = await fetch(url, {method,
    headers: {'Content-Type': 'application/json'},
    body: body === undefined ? undefined : JSON.stringify(body)});
  const text = await response.text();
  if (!response.ok) die(`Firebase ${response.status}: ${text.slice(0, 400)}`);
  return text && text !== 'null' ? JSON.parse(text) : null;
}

const [command, ...rest] = process.argv.slice(2);

if (!command || ['-h', '--help', 'help'].includes(command)) {
  console.log(readFileSync(new URL(import.meta.url), 'utf8')
    .split('*/')[0].replace(/^#!.*\n/, '').replace(/^\/\*!?\s*/, '')
    .replace(/^ \* ?/gm, '').trim() + '\n');
  process.exit(0);
}
if (!host) die('Set PVG3_DATABASE_HOST to your database host first,\n'
  + 'for example: export PVG3_DATABASE_HOST=\'<project>-default-rtdb.firebaseio.com\'');
if (!secret) die('Set PVG3_DATABASE_SECRET first (Project settings → Service '
  + 'accounts → Database secrets).');

const nick = rest[0] || '';
const reason = rest.slice(1).join(' ').slice(0, 140);

switch (command) {
  case 'rules': {
    const rules = JSON.parse(readFileSync(rulesPath, 'utf8'));
    await call('.settings/rules', 'PUT', rules);
    console.log('Правила опубликованы: firebase/database.rules.json');
    break;
  }
  case 'show-rules': {
    const rules = await call('.settings/rules');
    console.log(JSON.stringify(rules, null, 2));
    break;
  }
  case 'admin':
  case 'unadmin': {
    if (!nick) die('Укажи ник: node tools/firebase-admin.mjs admin <ник>');
    await call(`admins/${nick}`, command === 'admin' ? 'PUT' : 'DELETE',
      command === 'admin' ? true : undefined);
    console.log(`${command === 'admin' ? 'Модератор добавлен' : 'Модератор снят'}: ${nick}`);
    break;
  }
  case 'list-admins': {
    const admins = await call('admins') || {};
    const names = Object.entries(admins).filter(([, value]) => value === true)
      .map(([login]) => login);
    console.log(names.length ? names.join('\n') : 'Модераторов нет.');
    break;
  }
  case 'ban':
  case 'unban': {
    if (!nick) die('Укажи ник: node tools/firebase-admin.mjs ban <ник> причина');
    await call(`bans/${nick}`, 'PUT', {banned: command === 'ban',
      reason: reason || (command === 'ban' ? 'нарушение правил' : ''),
      at: Date.now(), by: 'console'});
    console.log(`${command === 'ban' ? 'Забанен' : 'Разбанен'}: ${nick}`);
    break;
  }
  case 'list-accounts': {
    const accounts = await call('accounts') || {};
    const names = Object.keys(accounts);
    console.log(`${names.length} аккаунтов:`);
    for (const login of names)
      console.log(`  ${login}  (создан ${new Date(Number(accounts[login].createdAt) || 0).toISOString()})`);
    break;
  }
  default:
    die(`Неизвестная команда: ${command}`);
}
