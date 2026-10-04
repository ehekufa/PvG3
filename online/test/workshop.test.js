import test from 'node:test';
import assert from 'node:assert/strict';
import {LEVEL_TYPES, TRIGGER_KINDS, WORLD_LIMIT, MAX_LEVEL_OBJECTS,
        MAX_OBJECT_WIDTH, MAX_OBJECT_HEIGHT, newDraft, addObject, findObjectAt,
        moveObjects, resizeObjects, rotateObjects, panCamera, copyObjects, pasteObjects, validateDraft,
        publishedRecord, isPublishedRecord, draftFromPublished, resolveControlMode,
        setTriggerKind, createTouchButtonState, createPreviewState, stepPreview,
        drawEditorCanvas, drawPreviewCanvas} from '../workshop.js';

function recordingCanvas() {
  const images = [];
  const outlines = [];
  const context = {
    clearRect() {}, fillRect() {}, beginPath() {}, moveTo() {}, lineTo() {},
    closePath() {}, fill() {}, stroke() {}, arc() {}, ellipse() {},
    save() {}, restore() {}, translate() {}, rotate() {},
    strokeRect(...bounds) {outlines.push(bounds);}, fillText() {},
    drawImage(image, ...bounds) {images.push({image, bounds});},
  };
  return {canvas: {width: 1280, height: 720, getContext: () => context}, images, outlines};
}

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
  const outside = structuredClone(level);outside.objects[0].x = WORLD_LIMIT;
  assert.equal(validateDraft(outside).ok, false);
  const trigger = addObject(level, 'trigger', 5, 6);
  trigger.trigger.action = 'launch-website';
  assert.match(validateDraft(level).message, /триггер/);
  assert.equal(addObject({...level, objects:Array.from({length:MAX_LEVEL_OBJECTS},(_,i)=>({...level.objects[0],id:i+10}))}, 'coin', 1, 1), null);
});

test('world placement is scrollable across large positive and negative coordinates', () => {
  const level = newDraft();
  const west = addObject(level, 'block', -WORLD_LIMIT, 24);
  const east = addObject(level, 'block', WORLD_LIMIT, -WORLD_LIMIT);
  assert.equal(west.x, -WORLD_LIMIT);
  assert.equal(east.x, WORLD_LIMIT - east.w);
  assert.equal(east.y, -WORLD_LIMIT);
  assert.equal(validateDraft(level).ok, true);
  for (const kind of TRIGGER_KINDS)
    assert.equal(addObject(level, 'trigger', kind === 'rotate' ? 8 : 7, 5, kind).trigger.kind, kind);
  assert.deepEqual(TRIGGER_KINDS,
    ['move', 'rotate', 'forever', 'invisibility', 'no-collision']);
  assert.equal(validateDraft(level).ok, true);
});

test('map arrow controls pan the viewport independently and clamp to the world edges', () => {
  assert.deepEqual(panCamera({x: 0, y: 0}, 4, -3), {x: 4, y: -3});
  assert.deepEqual(panCamera({x: WORLD_LIMIT - 16, y: WORLD_LIMIT - 10}, 4, 3),
    {x: WORLD_LIMIT - 16, y: WORLD_LIMIT - 10});
  assert.deepEqual(panCamera({x: -WORLD_LIMIT, y: -WORLD_LIMIT}, -2, -5),
    {x: -WORLD_LIMIT, y: -WORLD_LIMIT});
  assert.deepEqual(panCamera({x: 1, y: 2}, NaN, Infinity), {x: 1, y: 2});
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

test('workshop editor and preview use the supplied level artwork and tile wide platforms', () => {
  const level = newDraft();
  const art = Object.fromEntries(LEVEL_TYPES.map(type => [type,
    {type, naturalWidth: 100, naturalHeight: type === 'ground' ? 50 : 100}]));
  const editor = recordingCanvas();
  drawEditorCanvas(editor.canvas, level, 0, 'build', art);
  assert(editor.images.some(call => call.image.type === 'player'));
  assert(editor.images.some(call => call.image.type === 'goal'));
  assert.equal(editor.images.filter(call => call.image.type === 'ground').length, 16);

  const preview = recordingCanvas();
  drawPreviewCanvas(preview.canvas, createPreviewState(level), 'keyboard', art);
  assert(preview.images.some(call => call.image.type === 'player'));
  assert(preview.images.some(call => call.image.type === 'goal'));
  assert.equal(preview.images.filter(call => call.image.type === 'ground').length, 16);
});

test('typed trigger settings persist per object and apply group movement, rotation and forever actions', () => {
  const level = newDraft('trigger-config');
  const first = addObject(level, 'block', 6, 6);
  const second = addObject(level, 'block', 8, 6);
  first.number = 42;second.number = 42;
  const move = addObject(level, 'trigger', 1, 7, 'move');
  move.trigger = {kind: 'move', event: 'manual', action: 'move',
    targetId: first.id, groupId: 42, valueX: 12, valueY: -7, value: 12, color: '#ffc54e'};
  const rotate = addObject(level, 'trigger', 1, 7, 'rotate');
  rotate.trigger = {kind: 'rotate', event: 'manual', action: 'rotate',
    targetId: first.id, groupId: 42, duration: 2, color: '#ffc54e'};
  const forever = addObject(level, 'trigger', 1, 7, 'forever');
  forever.trigger = {kind: 'forever', event: 'manual', action: 'unactivate',
    targetId: 0, groupId: 42, color: '#ffc54e'};

  assert.equal(validateDraft(level).ok, true);
  const record = publishedRecord('314', level);
  assert.deepEqual(record.project.objects.find(o => o.id === move.id).trigger,
    {kind: 'move', event: 'manual', action: 'move', targetId: first.id,
      groupId: 42, color: '#ffc54e', valueX: 12, valueY: -7, value: 12});
  assert.deepEqual(record.project.objects.find(o => o.id === rotate.id).trigger,
    {kind: 'rotate', event: 'manual', action: 'rotate', targetId: first.id,
      groupId: 42, color: '#ffc54e', duration: 2});
  assert.equal(record.project.objects.find(o => o.id === forever.id).trigger.action, 'unactivate');
  const restored = draftFromPublished(record);
  assert.equal(restored.objects.find(o => o.id === forever.id).trigger.groupId, 42);
  assert.equal(validateDraft(restored).ok, true);

  const state = createPreviewState(restored);
  stepPreview(state, {trigger: true}, 1 / 60);
  const moved = state.objects.filter(o => o.id === first.id || o.id === second.id);
  assert.deepEqual(moved.map(o => [o.x, o.y, o.angle, o.visible]),
    [[18, -1, 6, false], [20, -1, 6, false]]);
  for (let i = 0; i < 30; i++) stepPreview(state, {}, .05);
  assert(Math.abs(moved[0].angle - 186) < .01);
  for (let i = 0; i < 10; i++) stepPreview(state, {}, .05);
  assert(Math.min(moved[0].angle, 360 - moved[0].angle) < .01);
  for (let i = 0; i < 30; i++) stepPreview(state, {}, .05);
  assert(Math.min(moved[0].angle, 360 - moved[0].angle) < .01);
  assert.deepEqual(moved.map(o => o.visible), [false, false]);
});

test('invisibility and no-collision variants round-trip and change grouped targets', () => {
  const level = newDraft('visibility-collision');
  const first = addObject(level, 'block', 6, 6);
  const second = addObject(level, 'block', 8, 6);
  first.number = second.number = 42;
  const invisible = addObject(level, 'trigger', 1, 7, 'invisibility');
  invisible.trigger = {kind: 'invisibility', event: 'manual', action: 'invisible',
    targetId: first.id, groupId: 42, color: '#ffc54e'};
  const noCollision = addObject(level, 'trigger', 1, 7, 'no-collision');
  noCollision.trigger = {kind: 'no-collision', event: 'manual', action: 'no-collision',
    targetId: first.id, groupId: 42, color: '#ffc54e'};

  assert.equal(validateDraft(level).ok, true);
  const record = publishedRecord('316', level);
  assert(isPublishedRecord(record, '316'));
  assert.equal(record.project.objects.find(object => object.id === invisible.id)
    .trigger.kind, 'invisibility');
  assert.equal(record.project.objects.find(object => object.id === noCollision.id)
    .trigger.action, 'no-collision');
  const restored = draftFromPublished(record);
  const state = createPreviewState(restored);
  stepPreview(state, {trigger: true}, 1 / 60);
  assert.deepEqual(state.objects.filter(object => object.id === first.id ||
    object.id === second.id).map(object => object.visible), [true, true]);
  assert.deepEqual(state.invisible, [first.id, second.id]);
  assert.deepEqual(state.noCollision, [first.id, second.id]);

  const reconfigured = structuredClone(restored);
  assert.equal(setTriggerKind(reconfigured, invisible.id, 'rotate'), true);
  const reconfiguredTrigger = reconfigured.objects.find(object => object.id === invisible.id).trigger;
  assert.equal(reconfiguredTrigger.kind, 'rotate');
  assert.equal(reconfiguredTrigger.action, 'rotate');
  assert.equal(reconfiguredTrigger.duration, 3);
  assert.equal(setTriggerKind(reconfigured, invisible.id, 'invisibility'), true);
  assert.equal(reconfigured.objects.find(object => object.id === invisible.id).trigger.action,
    'invisible');
  assert.equal(setTriggerKind(reconfigured, invisible.id, 'not-a-trigger'), false);

  const invisibleFloorLevel = newDraft('invisible-floor');
  const invisibleFloor = invisibleFloorLevel.objects.find(object => object.type === 'ground');
  invisibleFloor.y = 20;invisibleFloor.h = 1;
  const invisibleBlock = addObject(invisibleFloorLevel, 'block', 1, 8);
  invisibleBlock.number = 55;
  const hideBlock = addObject(invisibleFloorLevel, 'trigger', 1, 7, 'invisibility');
  hideBlock.trigger = {kind: 'invisibility', event: 'manual', action: 'invisible',
    targetId: invisibleBlock.id, groupId: 55, color: '#ffc54e'};
  const standing = createPreviewState(invisibleFloorLevel);
  stepPreview(standing, {trigger: true}, .05);
  for (let i = 0; i < 20; i++) stepPreview(standing, {}, .05);
  assert(standing.invisible.includes(invisibleBlock.id));
  assert.equal(standing.objects.find(object => object.id === invisibleBlock.id).visible, true);
  assert.equal(standing.grounded, true);
  assert(Math.abs(standing.y - (8 * 72 - .85 * 72)) < 2,
    'the player can stand on a visually hidden block');
  const preview = recordingCanvas();
  drawPreviewCanvas(preview.canvas, standing, 'keyboard', {
    block: {type: 'block', naturalWidth: 100},
  });
  assert(!preview.images.some(call => call.image.type === 'block'),
    'the hidden block is not drawn even though it remains solid');

  const fallLevel = newDraft('no-collision-fall');
  const floor = fallLevel.objects.find(object => object.type === 'ground');
  floor.y = 20;floor.h = 1;
  const platform = addObject(fallLevel, 'block', 1, 8);
  platform.number = 55;
  const passThrough = addObject(fallLevel, 'trigger', 1, 7, 'no-collision');
  passThrough.trigger = {kind: 'no-collision', event: 'manual', action: 'no-collision',
    targetId: platform.id, groupId: 55, color: '#ffc54e'};
  const falling = createPreviewState(fallLevel);
  stepPreview(falling, {trigger: true}, .05);
  for (let i = 0; i < 20; i++) stepPreview(falling, {}, .05);
  assert(falling.noCollision.includes(platform.id));
  assert(falling.y > 9 * 72, 'the player falls through the collision-disabled block');
});

test('rotated collision follows the drawn block and play preview hides all trigger art', () => {
  const level = newDraft('rotated-collision');
  const player = level.objects.find(object => object.type === 'player');
  const floor = level.objects.find(object => object.type === 'ground');
  player.x = 4.5;player.y = 4.5;floor.y = 12;floor.h = 1;
  const block = addObject(level, 'block', 4, 6);
  block.w = 2;block.h = 1;block.angle = 90;
  const trigger = addObject(level, 'trigger', 10, 1, 'invisibility');
  const state = createPreviewState(level);
  for (let i = 0; i < 120; i++) stepPreview(state, {}, 1 / 60);
  assert.equal(state.grounded, true);
  assert(state.y > 315 && state.y < 345,
    `the rotated block's top surface should support the player (y=${state.y})`);

  const art = {triggerInvisibility: {type: 'triggerInvisibility', naturalWidth: 100},
    block: {type: 'block', naturalWidth: 100}, ground: {type: 'ground', naturalWidth: 100},
    player: {type: 'player', naturalWidth: 100}, goal: {type: 'goal', naturalWidth: 100}};
  const editor = recordingCanvas();
  drawEditorCanvas(editor.canvas, level, 0, 'build', art);
  assert(editor.images.some(call => call.image.type === 'triggerInvisibility'),
    'the icon remains available in the editor');
  const preview = recordingCanvas();
  drawPreviewCanvas(preview.canvas, createPreviewState(level), 'keyboard', art);
  assert(!preview.images.some(call => call.image.type.startsWith('trigger')),
    'triggers are invisible in the playable preview');
  assert.equal(trigger.trigger.kind, 'invisibility');
});

test('movement offsets accept the full requested range and reject values beyond it', () => {
  const level = newDraft();
  const trigger = addObject(level, 'trigger', 4, 7, 'move');
  trigger.trigger.valueX = 9999;trigger.trigger.valueY = -9999;
  assert.equal(validateDraft(level).ok, true);
  trigger.trigger.valueX = 10000;
  assert.equal(validateDraft(level).ok, false);
  trigger.trigger.valueX = 0;trigger.trigger.kind = 'rotate';
  trigger.trigger.action = 'rotate';trigger.trigger.duration = 9999;
  assert.equal(validateDraft(level).ok, true);
  trigger.trigger.duration = 10000;
  assert.equal(validateDraft(level).ok, false);
  trigger.trigger.duration = 0;
  assert.equal(validateDraft(level).ok, false);
  delete trigger.trigger.duration;trigger.trigger.degrees = 361;
  assert.equal(validateDraft(level).ok, false);
});

test('rotation variants rotate objects and legacy forever loops remain bounded', () => {
  const level = newDraft();
  const target = addObject(level, 'block', 6, 6);
  const first = addObject(level, 'trigger', 10, 7, 'move');
  first.trigger = {kind: 'move', event: 'manual', action: 'move',
    targetId: target.id, value: 1, color: '#ffc54e'};
  const rotating = addObject(level, 'trigger', 1, 7, 'rotate');
  rotating.trigger = {kind: 'rotate', event: 'manual', action: 'move',
    targetId: target.id, value: 90, color: '#ffc54e'};
  const forever = addObject(level, 'trigger', 12, 0, 'forever');
  forever.trigger = {kind: 'forever', event: 'touch', action: 'move',
    targetId: target.id, value: 0, color: '#ffc54e'};
  assert.equal(validateDraft(level).ok, true);
  const migrated = draftFromPublished(publishedRecord('315', level));
  const migratedRotate = migrated.objects.find(object => object.id === rotating.id).trigger;
  assert.equal(migratedRotate.duration, 3);
  assert.equal(migratedRotate.groupId, target.number);
  assert.equal('degrees' in migratedRotate, false);
  const state = createPreviewState(level);
  stepPreview(state, {trigger: true}, 1 / 60);
  assert.equal(state.objects.find(object => object.id === target.id).angle, 90);
  const before = state.objects.find(object => object.id === target.id).x;
  for (let i = 0; i < 12; i++) stepPreview(state, {}, .05);
  assert.equal(state.objects.find(object => object.id === target.id).x, before + 1);
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


test('multi-object transforms clamp geometry, preserve independent angles and hit rotated bounds', () => {
  const level = newDraft();
  const first = addObject(level, 'block', 4, 3);
  const second = addObject(level, 'coin', WORLD_LIMIT - 1, WORLD_LIMIT - 1);
  first.angle = 350;second.angle = 10;
  assert.equal(moveObjects(level, new Set([first.id, second.id]), 2, -1), 2);
  assert.deepEqual([first.x, first.y], [6, 2]);
  assert.deepEqual([second.x, second.y], [WORLD_LIMIT - second.w, WORLD_LIMIT - 1 - 1]);
  assert.equal(resizeObjects(level, [first.id, second.id], 'width', .25), 2);
  assert.equal(first.w, 1.25);assert.equal(second.w, .8);
  assert.equal(resizeObjects(level, [first.id], 'height', -100), 1);
  assert.equal(first.h, .1);
  assert.equal(resizeObjects(level, [first.id], 'width', 100), 1);
  assert.equal(first.w, MAX_OBJECT_WIDTH);
  assert.equal(resizeObjects(level, [first.id], 'height', 100), 1);
  assert.equal(first.h, MAX_OBJECT_HEIGHT);
  assert.equal(rotateObjects(level, [first.id, second.id], 20), 2);
  assert.equal(first.angle, 10);assert.equal(second.angle, 30);
  assert.equal(findObjectAt(level, first.x + first.w / 2, first.y + first.h / 2)?.id, first.id);
  assert.equal(validateDraft(level).ok, true);
});

test('copy and paste duplicate a selection independently, preserve group data, and skip singleton objects', () => {
  const level = newDraft();
  const block = addObject(level, 'block', 5, 4);
  const trigger = addObject(level, 'trigger', 7, 4, 'rotate');
  block.number = 42;
  trigger.trigger.groupId = 42;
  trigger.angle = 37.5;
  const clipboard = copyObjects(level, [block.id, trigger.id, 1, 2]);
  assert.equal(clipboard.length, 2);
  assert.deepEqual(clipboard.map(object => object.type), ['block', 'trigger']);
  const pasted = pasteObjects(level, clipboard);
  assert.equal(pasted.length, 2);
  assert.deepEqual(pasted.map(object => [object.x, object.y]), [[6, 5], [8, 5]]);
  assert.notEqual(pasted[0].id, block.id);
  assert.equal(pasted[0].number, 42);
  assert.equal(pasted[1].trigger.groupId, 42);
  assert.equal(pasted[1].angle, 37.5);
  pasted[0].x = 200;
  assert.equal(block.x, 5);
  assert.equal(validateDraft(level).ok, true);

  const full = {...level, objects: Array.from({length:MAX_LEVEL_OBJECTS - 1}, (_, i) =>
    ({...level.objects[0], id: i + 1000}))};
  assert.deepEqual(pasteObjects(full, clipboard), []);
  assert.equal(full.objects.length, MAX_LEVEL_OBJECTS - 1);
});

test('editor outlines every item in a multi-selection and rotation persists in the wire record', () => {
  const level = newDraft();
  const a = addObject(level, 'block', 4, 4);
  const b = addObject(level, 'hazard', 6, 4);
  a.angle = 22.5;b.angle = 90;
  const canvas = recordingCanvas();
  drawEditorCanvas(canvas.canvas, level, new Set([a.id, b.id]), 'select');
  assert.equal(canvas.outlines.length, 2);
  const roundTrip = draftFromPublished(publishedRecord('998', level));
  assert.equal(roundTrip.objects.find(object => object.id === a.id).angle, 22.5);
  assert.equal(roundTrip.objects.find(object => object.id === b.id).angle, 90);
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
