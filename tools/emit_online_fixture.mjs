/* A real browser-owned snapshot: the native parser must understand the web
 * client's exact wire format, including Firebase-safe false sentinels. */
import {newMatch, applyCommand} from '../online/rules.js';
import {encodeState} from '../online/firebase.js';

const game = newMatch(5);
if (!applyCommand(game, 'plants', {kind:'plant', row:1, col:2, type:4}) ||
    !applyCommand(game, 'plants', {kind:'plant', row:1, col:2, type:0}) ||
    !applyCommand(game, 'zombies', {kind:'spawn', row:1, type:2}))
  throw new Error('Could not build a valid web fixture');
console.log(JSON.stringify(encodeState(game)));
