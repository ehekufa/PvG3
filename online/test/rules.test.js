import test from 'node:test';
import assert from 'node:assert/strict';
import {newMatch, validMatch, applyCommand, stepMatch, PLANTS, DUCKS, WAVE, X} from '../rules.js';

test('author artwork has five plants, three duck variants, increasing real HP', () => {
  assert.equal(PLANTS.length, 5);
  assert.deepEqual(DUCKS.map(d => d.hp), [180, 420, 750]);
  assert.deepEqual(DUCKS.map(d => d.id), [0, 2, 3]);
  assert(validMatch(newMatch()));
});

test('the water map rejects plants until a pad supports them', () => {
  const s = newMatch(5);
  assert(!applyCommand(s, 'plants', {kind:'plant', type:0, row:1, col:2}));
  assert.equal(s.plantCash, 250);
  assert(!applyCommand(s, 'plants', {kind:'plant', type:4, row:0, col:0}));
  assert(applyCommand(s, 'plants', {kind:'plant', type:4, row:1, col:2}));
  assert.equal(s.plantCash, 225);
  assert(s.lilies[1*9+2]);
  assert(applyCommand(s, 'plants', {kind:'plant', type:0, row:1, col:2}));
  assert.equal(s.plants[1*9+2].hp, 300);
  assert.equal(s.plantCash, 125);
  assert(!applyCommand(s, 'plants', {kind:'plant', type:1, row:1, col:2}));
});

test('same drawn duck gets actual cone and helmet HP; invalid moves are free', () => {
  const s = newMatch();
  assert(!applyCommand(s, 'zombies', {kind:'spawn', type:999, row:0}));
  assert(!applyCommand(s, 'zombies', {kind:'spawn', type:2, row:9}));
  assert.equal(s.zombieCash, 175);
  assert(applyCommand(s, 'zombies', {kind:'spawn', type:2, row:3}));
  assert.equal(s.ducks[0].hp, 420);
  assert.equal(s.left, WAVE-1);
  assert.equal(s.zombieCash, 75);
  assert(!applyCommand(s, 'zombies', {kind:'spawn', type:3, row:3}));
  for (let i=0;i<101;i++) stepMatch(s, .05);
  assert.equal(s.zombieCash, 150); // 75 passive coins after five seconds
  assert(applyCommand(s, 'zombies', {kind:'spawn', type:0, row:4}));
  assert.equal(s.ducks[1].hp, 180);
  assert.equal(s.zombieCash, 100);
});

test('coins are collected, not sky income; wave ends only after all ducks die', () => {
  const s = newMatch();
  assert(applyCommand(s, 'plants', {kind:'plant', type:2, row:0, col:1}));
  for (let i=0;i<101;i++) stepMatch(s,.05);
  assert.equal(s.plantCash,200);
  assert.equal(s.coins.length,1);
  // Guest's `id` is the player identifier; the coin number uses coinId.
  assert(applyCommand(s,'plants',{kind:'coin',id:'b'.repeat(32),
                                  coinId:s.coins[0].id}));
  assert.equal(s.plantCash,225);
  assert.equal(s.coins.length,0);
  assert(applyCommand(s,'zombies',{kind:'spawn',type:0,row:4}));
  s.ducks[0].hp=0;stepMatch(s,.05);
  assert.equal(s.winner,''); // more ducks still scheduled
  assert(applyCommand(s,'zombies',{kind:'finish'}));
  assert.equal(s.winner,'plants');
});

test('mower defends its lane once; the second breach wins for zombies', () => {
  const s = newMatch();
  assert(applyCommand(s,'zombies',{kind:'spawn',type:0,row:1}));
  const first=s.ducks[0];first.x=X+60;
  stepMatch(s,.05);
  assert(s.mowers[1].used && s.mowers[1].running);
  // Drive the used mower past the field without waiting for real time.
  s.ducks=[];s.mowers[1].running=false;
  for(let i=0;i<50;i++) stepMatch(s,.05);
  assert(applyCommand(s,'zombies',{kind:'spawn',type:0,row:1}));
  s.ducks[0].x=X-30;
  stepMatch(s,.05);
  assert.equal(s.winner,'zombies');
});
