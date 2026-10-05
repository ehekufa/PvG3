import test from 'node:test';
import assert from 'node:assert/strict';
import {LEVEL_TYPES, TRIGGER_KINDS, WORLD_LIMIT, MAX_LEVEL_OBJECTS,
        MAX_OBJECT_WIDTH, MAX_OBJECT_HEIGHT, newDraft, addObject, findObjectAt,
        moveObjects, resizeObjects, rotateObjects, flipObjects, panCamera, copyObjects, pasteObjects, validateDraft,
        publishedRecord, isPublishedRecord, draftFromPublished, resolveControlMode,
        setTriggerKind, createTouchButtonState, createPreviewState, stepPreview,
        DEFAULT_PARTICLE_EMITTER, normalizeParticleEmitter, sampleParticleEmitter,
        drawParticleEmitterPreview, drawEditorCanvas, drawPreviewCanvas} from '../workshop.js';

function recordingCanvas() {
  const images = [];
  const outlines = [];
  const scales = [];
  const labels = [];
  const fills = [];
  const rectFills = [];
  let pathPoints = 0;
  const context = {
    clearRect() {}, fillRect(...bounds) {rectFills.push({style:this.fillStyle, bounds});}, beginPath() {pathPoints = 0;},
    moveTo() {pathPoints++;}, lineTo() {pathPoints++;},
    closePath() {pathPoints++;}, fill() {fills.push({style:this.fillStyle, alpha:this.globalAlpha ?? 1});},
    stroke() {if (pathPoints >= 4) outlines.push({type: 'polygon'});},
    arc() {}, ellipse() {},
    save() {}, restore() {}, translate() {}, rotate() {},
    scale(...value) {scales.push(value);},
    strokeRect(...bounds) {outlines.push(bounds);},
    fillText(text) {labels.push(String(text));},
    drawImage(image, ...bounds) {images.push({image, bounds});},
  };
  return {canvas: {width: 1280, height: 720, getContext: () => context},
    images, outlines, scales, labels, fills, rectFills};
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

test('the 20,000-object ceiling is combined across blocks, triggers, coins and every other type', () => {
  const level = newDraft('draft-limit');
  const additionalTypes = ['block', 'ground', 'hazard', 'coin', 'enemy', 'trigger', 'slope',
    'orb-yellow', 'orb-orange', 'particle'];
  level.objects = Array.from({length:MAX_LEVEL_OBJECTS}, (_, index) => {
    const type = index === 0 ? 'player' : index === 1 ? 'goal' : index === 2 ? 'ground' :
      additionalTypes[(index - 3) % additionalTypes.length];
    const object = {id:index + 1, type, name:'Объект', x:index % 100, y:Math.floor(index / 100) / 100,
      w:1, h:1, angle:0, flipX:false, flipY:false, color:'#55c8ea',
      number:index % 10000, visible:true};
    if (type === 'trigger') object.trigger = {kind:'move', event:'touch', action:'move',
      targetId:2, valueX:0, valueY:0, value:0, color:'#ffc54e'};
    return object;
  });
  assert.equal(level.objects.length, 20_000);
  assert.equal(validateDraft(level).ok, true);
  const wireRecord = publishedRecord('654321', level);
  assert.equal(wireRecord.project.objects.length, 20_000);
  assert.equal(isPublishedRecord(wireRecord, '654321'), true);
  assert.equal(addObject(level, 'coin', 1, 1), null);
  level.objects.push({...level.objects[3], id:20_001});
  assert.equal(validateDraft(level).ok, false);
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
    ['move', 'rotate', 'forever', 'invisibility', 'no-collision', 'gravity']);
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
  const slope = addObject(level, 'slope', 8, 6);
  const emitter = addObject(level, 'particle', 11, 4);
  emitter.emitter = {...emitter.emitter, continuous:false, gravityEnabled:true,
    rate:13, lifetime:2.4, speed:177, spread:72, size:7, direction:-45, gravity:140};
  const trigger = addObject(level, 'trigger', 4, 7);
  trigger.trigger = {event:'start',action:'recolor',targetId:block.id,value:0,color:'#ffcc44'};
  const record = publishedRecord('104', level);
  assert.equal(record.format, 'PVG3-PUBLISHED-LEVEL');
  assert.equal(record.project.format, 'PVG3-MAKER');
  assert(isPublishedRecord(record, '104'));
  assert(!isPublishedRecord(record, '105'));
  const restored = draftFromPublished(record);
  assert.equal(restored.title, level.title);
  assert.equal(restored.objects.find(o => o.id === slope.id).type, 'slope');
  assert.equal(restored.objects.find(o => o.id === emitter.id).type, 'particle');
  assert.deepEqual(restored.objects.find(o => o.id === emitter.id).emitter, emitter.emitter);
  assert.deepEqual(record.project.objects.find(o => o.id === emitter.id).emitter,
    emitter.emitter);
  assert.equal(restored.objects.find(o => o.id === trigger.id).trigger.action, 'recolor');
  assert.equal(restored.objects.find(o => o.id === trigger.id).trigger.event, 'start');
  assert.equal(validateDraft(restored).ok, true);
  assert.throws(() => publishedRecord('0', level), /ID/);
});

test('particle emitters round-trip, stay non-solid and draw P markers plus orb sparks', () => {
  const level = newDraft('particle-effects');
  const emitter = addObject(level, 'particle', 6, 4);
  const yellow = addObject(level, 'orb-yellow', 9, 4);
  const orange = addObject(level, 'orb-orange', 11, 4);
  assert.equal(emitter.w, 1);assert.equal(emitter.h, 1);
  assert.deepEqual(emitter.emitter, DEFAULT_PARTICLE_EMITTER);
  const record = publishedRecord('712', level);
  assert.equal(isPublishedRecord(record, '712'), true);
  const restored = draftFromPublished(record);
  assert.deepEqual(restored.objects.map(object => object.type).slice(-3),
    ['particle', 'orb-yellow', 'orb-orange']);
  const restoredEmitter = restored.objects.find(object => object.id === emitter.id);
  assert.deepEqual(restoredEmitter.emitter, DEFAULT_PARTICLE_EMITTER);
  const legacyRecord = structuredClone(record);
  delete legacyRecord.project.objects.find(object => object.type === 'particle').emitter;
  assert.equal(isPublishedRecord(legacyRecord, '712'), true,
    'legacy particle records without settings remain readable');
  assert.deepEqual(draftFromPublished(legacyRecord).objects.find(o => o.type === 'particle').emitter,
    DEFAULT_PARTICLE_EMITTER);
  const invalidEmitter = structuredClone(level);
  invalidEmitter.objects.find(object => object.type === 'particle').emitter.speed = 301;
  assert.equal(validateDraft(invalidEmitter).ok, false);

  const editor = recordingCanvas();
  drawEditorCanvas(editor.canvas, level, emitter.id);
  assert(editor.labels.includes('P'), 'the art-free emitter is selectable by its P marker');

  const renderState = createPreviewState(level);renderState.time = .25;
  const preview = recordingCanvas();
  drawPreviewCanvas(preview.canvas, renderState);
  const renderedColors = new Set(preview.fills.map(fill => fill.style));
  assert(renderedColors.has(emitter.color), 'the particle object emits its configured color');
  assert(renderedColors.has('#fff86b'), 'yellow orbs emit yellow sparks');
  assert(renderedColors.has('#ffbb66'), 'orange orbs emit orange sparks');
  assert(renderedColors.has(emitter.color), 'particle settings still drive the level trail');

  const canvasPreview = recordingCanvas();
  canvasPreview.canvas.width = canvasPreview.canvas.height = 320;
  const previewCount = drawParticleEmitterPreview(canvasPreview.canvas,
    emitter.emitter, emitter.color, .5);
  assert(previewCount > 0, 'the black settings preview animates configured particles');
  assert(canvasPreview.labels.includes('P'), 'the preview marks the emitter with P');
  assert(canvasPreview.rectFills.some(fill => fill.style === '#03070b'),
    'the preview stage uses a black background');

  const baseParticle = sampleParticleEmitter(DEFAULT_PARTICLE_EMITTER, 17, .5, 0, 54, 38);
  const stoppedParticle = sampleParticleEmitter({...DEFAULT_PARTICLE_EMITTER, speed:0},
    17, .5, 0, 54, 38);
  const gravityParticle = sampleParticleEmitter({...DEFAULT_PARTICLE_EMITTER,
    gravityEnabled:true, gravity:300}, 17, .5, 0, 54, 38);
  const largerParticle = sampleParticleEmitter({...DEFAULT_PARTICLE_EMITTER, size:10},
    17, .5, 0, 54, 38);
  assert(baseParticle && stoppedParticle && gravityParticle && largerParticle);
  assert.notEqual(baseParticle.y, stoppedParticle.y, 'speed changes the trajectory');
  assert(gravityParticle.y > baseParticle.y, 'the gravity switch bends particles downward');
  assert(largerParticle.size > baseParticle.size, 'the size slider changes the particle radius');
  assert.equal(sampleParticleEmitter({...DEFAULT_PARTICLE_EMITTER, enabled:false},
    17, .5, 0, 54, 38), null, 'disabled emitters stop emitting');

  const emptyLevel = newDraft('particle-no-collision');
  emptyLevel.objects.find(object => object.type === 'ground').y = 30;
  const withParticle = structuredClone(emptyLevel);
  addObject(withParticle, 'particle', 1, 7);
  const baseline = createPreviewState(emptyLevel);
  const particleState = createPreviewState(withParticle);
  stepPreview(baseline, {}, .05);stepPreview(particleState, {}, .05);
  for (const property of ['x', 'y', 'vx', 'vy'])
    assert.equal(particleState[property], baseline[property], `emitter must not collide (${property})`);
  assert.equal(yellow.type, 'orb-yellow');assert.equal(orange.type, 'orb-orange');
});

test('gravity triggers round-trip a signed level setting and change preview acceleration', () => {
  const weak = newDraft('gravity-weak');
  weak.objects.find(object => object.type === 'ground').y = 30;
  const trigger = addObject(weak, 'trigger', 100, 100, 'gravity');
  trigger.trigger.event = 'start';trigger.trigger.value = -100;
  assert.equal(validateDraft(weak).ok, true);
  const weakRecord = publishedRecord('711', weak);
  assert.deepEqual(weakRecord.project.objects.find(object => object.id === trigger.id).trigger,
    {kind:'gravity', event:'start', action:'set-gravity', color:'#ffc54e', value:-100});
  assert.equal(isPublishedRecord(weakRecord, '711'), true);
  const weakDraft = draftFromPublished(weakRecord);
  const weakState = createPreviewState(weakDraft);
  assert.equal(weakState.gravity, 450);
  const switchedBack = structuredClone(weakDraft);
  assert.equal(setTriggerKind(switchedBack, trigger.id, 'move'), true);
  assert.equal(validateDraft(switchedBack).ok, true,
    'switching away from a level-wide trigger restores a valid target group');

  const strong = structuredClone(weakDraft);
  strong.objects.find(object => object.type === 'trigger').trigger.value = 100;
  assert.equal(validateDraft(strong).ok, true);
  const strongState = createPreviewState(strong);
  assert.equal(strongState.gravity, 2450);
  for (let i = 0; i < 10; i++) {
    stepPreview(weakState, {}, .05);
    stepPreview(strongState, {}, .05);
  }
  assert(strongState.y > weakState.y + 100,
    'positive slider values accelerate downward more strongly than negative values');
});

test('orbs have no solid body and one jump press in range activates them; orange is stronger', () => {
  const launch = type => {
    const level = newDraft(`orb-${type}`);
    level.objects.find(object => object.type === 'ground').y = 30;
    const player = level.objects.find(object => object.type === 'player');
    player.x = 3;player.y = 4;
    addObject(level, type, 3, 4);
    assert.equal(validateDraft(level).ok, true);
    const state = createPreviewState(level);
    const gravity = state.gravity;
    const spawnY = state.y;

    const outside = createPreviewState(level);
    outside.x += 500;outside.y += 500;
    stepPreview(outside, {jump:true}, 0);
    assert.equal(outside.orbActivated, false,
      'a jump press outside the orb activation circle has no orb effect');
    assert.equal(outside.vy, 0);

    stepPreview(state, {}, .05);
    assert(state.y > spawnY && state.vy > 0,
      'the player falls through the orb without a press; the orb is not solid');
    assert.equal(state.grounded, false);
    assert.equal(state.orbActivated, false);

    stepPreview(state, {jump:true}, 0);
    const launchSpeed = state.vy;
    assert.equal(launchSpeed, type === 'orb-orange' ? -1050 : -650,
      'a single jump press activates the orb while the player is in its circle');
    assert.equal(state.orbActivated, true);
    assert.equal(state.gravity, gravity, 'an orb must not modify the gravity setting');
    const launchY = state.y;
    stepPreview(state, {jump:true}, .05);
    assert(state.vy > launchSpeed && state.vy < 0,
      'holding the jump button does not repeatedly activate the orb');
    assert(state.y < launchY, 'the orb impulse launches the player upward');
    return {state, launchSpeed};
  };
  const yellow = launch('orb-yellow');
  const orange = launch('orb-orange');
  assert.equal(yellow.launchSpeed, -650);
  assert.equal(orange.launchSpeed, -1050);
  assert(orange.state.vy < yellow.state.vy);
  assert(orange.state.y < yellow.state.y);
});

test('control mode defaults to buttons on touch devices and can be chosen explicitly', () => {
  assert.equal(resolveControlMode('', true), 'buttons');
  assert.equal(resolveControlMode('', false), 'keyboard');
  assert.equal(resolveControlMode('unknown', true), 'buttons');
  assert.equal(resolveControlMode('buttons', false), 'buttons');
});

test('workshop editor and preview use the supplied level artwork and tile wide platforms', () => {
  const level = newDraft();
  addObject(level, 'slope', 5, 6);
  const art = Object.fromEntries(LEVEL_TYPES.map(type => [type,
    {type, naturalWidth: 100, naturalHeight: type === 'ground' ? 50 : 100}]));
  const editor = recordingCanvas();
  drawEditorCanvas(editor.canvas, level, 0, 'build', art);
  assert(editor.images.some(call => call.image.type === 'player'));
  assert(editor.images.some(call => call.image.type === 'goal'));
  assert(editor.images.some(call => call.image.type === 'slope'),
    'the slope uses the supplied slope drawing');
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
  const standingPlayer = standing.objects.find(object => object.type === 'player');
  assert(Math.abs(standing.y + standingPlayer.h * 72 * .96 - 8 * 72) < 2,
    'the visible bottom of the player can stand on a visually hidden block');
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

test('start and action events do not need contact, while touch events do', () => {
  const startLevel = newDraft('trigger-start-event');
  const startBlock = addObject(startLevel, 'block', 6, 6);
  startBlock.number = 42;
  const startTrigger = addObject(startLevel, 'trigger', 100, 100, 'invisibility');
  startTrigger.trigger = {kind: 'invisibility', event: 'start', action: 'invisible',
    targetId: startBlock.id, groupId: 42, color: '#ffc54e'};
  assert.equal(validateDraft(startLevel).ok, true);
  const started = createPreviewState(startLevel);
  assert(started.invisible.includes(startBlock.id),
    'start triggers fire immediately when the preview is created');

  const manualLevel = newDraft('trigger-manual-anywhere');
  const manualBlock = addObject(manualLevel, 'block', 6, 6);
  manualBlock.number = 42;
  const manualTrigger = addObject(manualLevel, 'trigger', 100, 100, 'no-collision');
  manualTrigger.trigger = {kind: 'no-collision', event: 'manual', action: 'no-collision',
    targetId: manualBlock.id, groupId: 42, color: '#ffc54e'};
  const manual = createPreviewState(manualLevel);
  stepPreview(manual, {trigger: true}, .01);
  assert(manual.noCollision.includes(manualBlock.id),
    'the action button activates its event without touching the trigger');

  const touchLevel = newDraft('trigger-touch-contact');
  const touchBlock = addObject(touchLevel, 'block', 6, 6);
  touchBlock.number = 42;
  const touchTrigger = addObject(touchLevel, 'trigger', 100, 100, 'invisibility');
  touchTrigger.trigger = {kind: 'invisibility', event: 'touch', action: 'invisible',
    targetId: touchBlock.id, groupId: 42, color: '#ffc54e'};
  const touched = createPreviewState(touchLevel);
  stepPreview(touched, {}, .01);
  assert(!touched.invisible.includes(touchBlock.id),
    'a touch trigger does not fire while it is far away');
  const runtimeTrigger = touched.objects.find(object => object.id === touchTrigger.id);
  runtimeTrigger.x = 1;runtimeTrigger.y = 7;
  stepPreview(touched, {}, .01);
  assert(touched.invisible.includes(touchBlock.id),
    'the touch trigger fires once the player contacts it');
});

test('character and spike hitboxes follow opaque artwork instead of transparent canvas corners', () => {
  const level = newDraft('alpha-hitbox');
  const player = level.objects.find(object => object.type === 'player');
  assert.equal(findObjectAt(level, player.x + player.w * .1,
    player.y + player.h * .5), null,
  'transparent sides of the character PNG are not selectable');
  assert.equal(findObjectAt(level, player.x + player.w * .5,
    player.y + player.h * .5)?.id, player.id);

  const hazard = addObject(level, 'hazard', 4, 4);
  assert.equal(findObjectAt(level, 4.05, 4.10), null,
    'transparent corners around the spike triangle are not selectable');
  assert.equal(findObjectAt(level, 4.5, 4.5)?.id, hazard.id);

  const wallLevel = newDraft('alpha-player-wall');
  const wallPlayer = wallLevel.objects.find(object => object.type === 'player');
  wallPlayer.x = 1;wallPlayer.y = 7;
  addObject(wallLevel, 'block', 2, 7);
  const state = createPreviewState(wallLevel);
  for (let i = 0; i < 3; i++) stepPreview(state, {axis: 1}, .05);
  assert(state.x > 112,
    'the player reaches the wall only when the visible character reaches it');

  const spikeLevel = newDraft('alpha-spike-graze');
  const grazer = spikeLevel.objects.find(object => object.type === 'player');
  grazer.x = 2.53;grazer.y = 4.033;
  addObject(spikeLevel, 'hazard', 3, 4);
  const graze = createPreviewState(spikeLevel);
  stepPreview(graze, {axis: 1}, .001);
  assert(graze.x > graze.spawn.x + .1,
    'transparent top-left area around the spike does not reset the player');
});

test('the safe triangular slope matches its artwork and can be climbed', () => {
  const level = newDraft('climbable-slope');
  const player = level.objects.find(object => object.type === 'player');
  player.x = 1;player.y = 7;
  const slope = addObject(level, 'slope', 3, 7);
  assert.equal(validateDraft(level).ok, true);
  assert.equal(findObjectAt(level, 3.5, 7.5)?.id, slope.id);
  assert.equal(findObjectAt(level, 3.1, 7.2), null,
    'transparent space outside the triangle is not selectable');
  slope.angle = 90;
  assert.equal(findObjectAt(level, 3.3, 7.6)?.id, slope.id,
    'selection follows the triangle when it is rotated');
  assert.equal(findObjectAt(level, 3.8, 7.1), null);
  slope.angle = 0;

  const state = createPreviewState(level);
  const spawnY = state.y;
  let highestY = spawnY;
  for (let i = 0; i < 24; i++) {
    stepPreview(state, {axis: 1}, .05);
    highestY = Math.min(highestY, state.y);
  }
  assert(state.x > 240, 'the player gets past the slope instead of hitting a wall');
  assert(highestY < spawnY - 35, 'the player rises while walking up the triangle');
  assert(!state.won, 'the slope is not treated as a damaging spike');

  slope.flipX = true;player.x = 5;
  assert.equal(findObjectAt(level, 3.1, 7.2)?.id, slope.id,
    'horizontal mirroring moves the solid triangle into the opposite corner');
  const mirrored = createPreviewState(level);
  const mirroredSpawnY = mirrored.y;
  let mirroredHighestY = mirroredSpawnY;
  for (let i = 0; i < 24; i++) {
    stepPreview(mirrored, {axis: -1}, .05);
    mirroredHighestY = Math.min(mirroredHighestY, mirrored.y);
  }
  assert(mirrored.x < 3 * 72,
    'the player can traverse the horizontally mirrored slope in reverse');
  assert(mirroredHighestY < mirroredSpawnY - 35,
    'collision follows the mirrored hypotenuse instead of an unflipped triangle');

  slope.flipX = false;slope.angle = 90;
  const rotated = createPreviewState(level);
  const rotatedSpawnY = rotated.y;
  let rotatedHighestY = rotatedSpawnY;
  for (let i = 0; i < 24; i++) {
    stepPreview(rotated, {axis: -1}, .05);
    rotatedHighestY = Math.min(rotatedHighestY, rotated.y);
  }
  assert(rotated.x < 3 * 72 && rotatedHighestY < rotatedSpawnY - 35,
    'rotated slope collision follows its drawn hypotenuse and remains climbable');
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

test('half-block movement and mirror flips persist and match editor hit-testing and artwork', () => {
  const level = newDraft('half-mirror');
  const block = addObject(level, 'block', 4, 4);
  const hazard = addObject(level, 'hazard', 8, 4);
  const enemy = addObject(level, 'enemy', 11, 4);
  assert.equal(moveObjects(level, [block.id], .5, -.5), 1);
  assert.deepEqual([block.x, block.y], [4.5, 3.5]);
  assert.equal(flipObjects(level, [block.id], 'x'), 1);
  assert.equal(flipObjects(level, [block.id], 'y'), 1);
  assert.equal(flipObjects(level, [hazard.id], 'z'), 0);
  assert.equal(block.flipX, true);assert.equal(block.flipY, true);

  const cornerX = hazard.x + hazard.w * .45;
  const cornerY = hazard.y + hazard.h * .1;
  assert.equal(findObjectAt(level, cornerX, cornerY), null,
    'the upper-left corner of the normal spike is transparent');
  assert.equal(flipObjects(level, [hazard.id], 'y'), 1);
  assert.equal(findObjectAt(level, cornerX, cornerY)?.id, hazard.id,
    'mirroring vertically moves the visible triangle under the pointer');

  const enemyFarSideX = enemy.x + enemy.w * .95;
  const enemyCenterY = enemy.y + enemy.h * .5;
  assert.equal(findObjectAt(level, enemyFarSideX, enemyCenterY), null,
    'the default enemy art and hitbox share its left-facing orientation');
  assert.equal(flipObjects(level, [enemy.id], 'x'), 1);
  assert.equal(findObjectAt(level, enemyFarSideX, enemyCenterY)?.id, enemy.id,
    'mirroring the enemy also mirrors its visible hitbox');

  const art = {block: {naturalWidth: 100}, hazard: {naturalWidth: 100},
    enemy: {naturalWidth: 100}};
  const canvas = recordingCanvas();
  drawEditorCanvas(canvas.canvas, level,
    new Set([block.id, hazard.id, enemy.id]), 'select', art);
  assert(canvas.scales.some(scale => scale[0] === -1 && scale[1] === -1));
  assert(canvas.scales.some(scale => scale[0] === 1 && scale[1] === -1));
  assert(canvas.scales.some(scale => scale[0] === 1 && scale[1] === 1));

  const published = publishedRecord('996', level);
  const roundTrip = draftFromPublished(published);
  const mirroredBlock = roundTrip.objects.find(object => object.id === block.id);
  assert.equal(mirroredBlock.flipX, true);assert.equal(mirroredBlock.flipY, true);
  assert.equal(roundTrip.objects.find(object => object.id === hazard.id).flipY, true);
  assert.equal(roundTrip.objects.find(object => object.id === enemy.id).flipX, true);
  const legacy = structuredClone(published);
  for (const object of legacy.project.objects) {
    delete object.flipX;delete object.flipY;
  }
  assert.equal(isPublishedRecord(legacy), true,
    'older level records without mirror fields remain compatible');
  const legacyDraft = draftFromPublished(legacy);
  assert.equal(legacyDraft.objects.every(object => !object.flipX && !object.flipY), true);
  const invalid = structuredClone(level);invalid.objects[3].flipX = 'yes';
  assert.equal(validateDraft(invalid).ok, false);
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
