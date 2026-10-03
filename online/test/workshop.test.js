import test from 'node:test';
import assert from 'node:assert/strict';
import {LEVEL_TYPES, MAX_LEVEL_OBJECTS, newDraft, addObject, validateDraft,
        publishedRecord, isPublishedRecord, draftFromPublished, resolveControlMode,
        createTouchButtonState, createPreviewState, stepPreview} from '../workshop.js';

test('new drafts are valid and object placement keeps player and finish unique', () => {
  const level = newDraft('draft-test');
  assert.equal(level.title, 'Новый уровень');
  assert.equal(level.width, 16);assert.equal(level.height, 10);
  assert.equal(validateDraft(level).ok, true);
  const player = addObject(level, 'player', 5, 5);
  const goal = addObject(level, 'goal', 12, 5);
  assert.equal(level.objects.filter(o => o.type === 'player').length, 1);
  assert.equal(level.objects.filter(o => o.type === 'goal').length, 1);
  assert.equal(level.objects.find(o => o.type === 'player').id, player.id);
  assert.equal(player.x, 5);assert.equal(goal.x, 12);
  for (const type of LEVEL_TYPES) assert(LEVEL_TYPES.includes(type));
});

test('validation rejects missing required objects, out-of-bounds geometry and malformed triggers', () => {
  const level = newDraft();
  const noGoal = structuredClone(level);noGoal.objects = noGoal.objects.filter(o => o.type !== 'goal');
  assert.match(validateDraft(noGoal).message, /игрок и финиш/);
  const outside = structuredClone(level);outside.objects[0].x = 16;
  assert.equal(validateDraft(outside).ok, false);
  const trigger = addObject(level, 'trigger', 5, 6);
  trigger.trigger.action = 'launch-website';
  assert.match(validateDraft(level).message, /триггер/);
  assert.equal(addObject({...level, objects:Array.from({length:MAX_LEVEL_OBJECTS},(_,i)=>({...level.objects[0],id:i+10}))}, 'coin', 1, 1), null);
});

test('published records round-trip through the browser/native wire schema', () => {
  const level = newDraft();level.title = 'Острова над рекой';level.description = 'Монеты и тайный мост';
  const block = addObject(level, 'block', 6, 6);
  const trigger = addObject(level, 'trigger', 4, 7);
  trigger.trigger = {event:'manual',action:'recolor',targetId:block.id,value:0,color:'#ffcc44'};
  const record = publishedRecord('104', level);
  assert.equal(record.format, 'PVG3-PUBLISHED-LEVEL');
  assert.equal(record.project.format, 'PVG3-MAKER');
  assert(isPublishedRecord(record, '104'));
  assert(!isPublishedRecord(record, '105'));
  const restored = draftFromPublished(record);
  assert.equal(restored.title, level.title);
  assert.equal(restored.objects.find(o => o.id === trigger.id).trigger.action, 'recolor');
  assert.equal(validateDraft(restored).ok, true);
  assert.throws(() => publishedRecord('0', level), /ID/);
});

test('control mode defaults to buttons on touch devices and can be chosen explicitly', () => {
  assert.equal(resolveControlMode('', true), 'buttons');
  assert.equal(resolveControlMode('', false), 'keyboard');
  assert.equal(resolveControlMode('unknown', true), 'buttons');
  assert.equal(resolveControlMode('buttons', false), 'buttons');
});

test('multitouch keeps forward/back pointers independent for movement plus jump', () => {
  const controls = createTouchButtonState();
  assert(controls.press(4, 'forward'));
  assert(controls.press(9, 'back'));
  assert.equal(controls.axis, 0);
  assert.equal(controls.release(9), true);
  assert.equal(controls.axis, 1);
  assert(controls.press(12, 'back'));
  assert.equal(controls.axis, 0);
  assert.equal(controls.release(4), true);
  assert.equal(controls.axis, -1);
  assert.equal(controls.release(12), true);
  assert.equal(controls.axis, 0);
  assert(controls.press(20, 'forward'));
  assert(controls.press(21, 'forward'));
  assert.equal(controls.release(20), true);
  assert.equal(controls.axis, 1);
  assert.equal(controls.release(21), true);
  assert.equal(controls.axis, 0);
  controls.clear();
  assert.equal(controls.axis, 0);
  assert.equal(controls.press(1, 'invalid'), false);
});

test('preview physics lands, jumps, collects coins, activates triggers and reaches the goal', () => {
  const level = newDraft();
  const coin = addObject(level, 'coin', 1, 7);
  const target = addObject(level, 'block', 5, 6);
  const trigger = addObject(level, 'trigger', 1, 7);
  trigger.trigger = {event:'touch',action:'recolor',targetId:target.id,value:0,color:'#ffcc44'};
  const state = createPreviewState(level);
  stepPreview(state, {}, 1 / 60);
  assert(state.collected.includes(coin.id));
  assert.equal(state.coins, 1);
  assert.equal(state.objects.find(o => o.id === target.id).color, '#ffcc44');
  for (let i = 0; i < 90; i++) stepPreview(state, {}, 1 / 60);
  assert.equal(state.grounded, true);
  stepPreview(state, {jump:true}, 1 / 60);
  assert(state.vy < 0);
  const goal = state.objects.find(o => o.type === 'goal');
  goal.x = 1;goal.y = 7;
  stepPreview(state, {}, 1 / 60);
  assert.equal(state.won, true);
});
