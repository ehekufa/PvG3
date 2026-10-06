/* Where the site talks to Firebase.
 *
 * The defaults below keep a fresh checkout working. To point the site at your
 * own database without publishing the address in git, drop an
 * online/firebase-secret.js next to this file (gitignored; CI generates it from
 * the PVG3_DATABASE_HOST and PVG3_DATABASE_AUTH secrets) that exports the same
 * two names. See firebase/README.md. */
export const DATABASE_HOST = 'pvg3-ae824-default-rtdb.firebaseio.com';
/* Optional ?auth= token appended to every request. Leave empty for the public
 * catalog: players authenticate with their own account token instead. */
export const DATABASE_AUTH = '';
