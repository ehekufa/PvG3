import test from 'node:test';
import assert from 'node:assert/strict';
import {
  MAX_OBJECT_ID, OBJECT_TYPES, TEMPLATES, createProject, makeObject, nextObjectId,
  parseProject, serializeProject,
} from '../maker-core.js';

test('maker offers platformer templates and starts with a playable classic level', () => {
  assert.deepEqual(TEMPLATES.map(item => item.id), ['classic', 'puzzle', 'adventure']);
  assert(TEMPLATES.every(item => item.title.toLowerCase().includes('платформер')));
  const project = createProject('classic');
  assert.equal(project.width, 16);
  assert.equal(project.height, 10);
  assert(project.objects.some(item => item.type === 'player'));
  assert(project.objects.some(item => item.type === 'goal'));
  assert(project.objects.some(item => item.type === 'ground'));
  assert(!TEMPLATES.some(item => /geometry|dash/i.test(item.title)));
});

test('TXT project round-trips IDs, colors, transforms, numbers, and trigger settings', () => {
  const project = createProject('puzzle');
  const bridge = project.objects.find(item => item.name === 'Скрытый мост');
  bridge.color = '#52a878';bridge.angle = 17;bridge.number = 42;
  const trigger = project.objects.find(item => item.type === 'trigger');
  trigger.trigger.action = 'recolor';trigger.trigger.targetId = bridge.id;
  trigger.trigger.color = '#df5570';
  const restored = parseProject(serializeProject(project));
  const restoredBridge = restored.objects.find(item => item.id === bridge.id);
  const restoredTrigger = restored.objects.find(item => item.id === trigger.id);
  assert.equal(restoredBridge.color, '#52a878');
  assert.equal(restoredBridge.angle, 17);
  assert.equal(restoredBridge.number, 42);
  assert.equal(restoredBridge.visible, false);
  assert.equal(restoredTrigger.trigger.action, 'recolor');
  assert.equal(restoredTrigger.trigger.targetId, bridge.id);
  assert.equal(restoredTrigger.trigger.color, '#df5570');
});

test('IDs are unique, editable, and new block/trigger defaults are stable', () => {
  const project = createProject('adventure');
  assert.equal(nextObjectId(project), 12);
  const block = makeObject('block', nextObjectId(project), 4, 3);
  assert.equal(block.name, OBJECT_TYPES.block.name);
  assert.equal(block.color, OBJECT_TYPES.block.color);
  const trigger = makeObject('trigger', nextObjectId(project) + 1, 5, 4);
  assert.equal(trigger.trigger.event, 'touch');
  assert.equal(trigger.trigger.action, 'toggle');
  assert.equal(trigger.number, 0);
});

test('normalization keeps large objects inside the playfield and reuses free IDs', () => {
  const project = createProject('classic');
  project.objects[0].id = MAX_OBJECT_ID;
  const player = project.objects.find(item => item.type === 'player');
  player.x = 15.9;player.y = 9.8;player.w = 4;player.h = 2;
  const restored = parseProject(serializeProject(project));
  const normalizedPlayer = restored.objects.find(item => item.type === 'player');
  assert.equal(normalizedPlayer.x, 12);
  assert.equal(normalizedPlayer.y, 8);
  assert.equal(nextObjectId(project), 1);
  assert.throws(() => makeObject('block', MAX_OBJECT_ID + 1), /ID должен быть/);
});

test('imports reject malformed or unsafe project data', () => {
  assert.throws(() => parseProject('not a project'), /прочитать TXT/i);
  const project = createProject('classic');
  project.objects[1].id = project.objects[0].id;
  assert.throws(() => parseProject(JSON.stringify(project)), /свой целый ID/i);
  const unknown = createProject('classic');
  unknown.objects[0].type = 'script';
  assert.throws(() => parseProject(JSON.stringify(unknown)), /неизвестный тип/i);
  assert.throws(() => parseProject(JSON.stringify({...createProject('classic'), version:999})), /версия не поддерживается/i);
});
