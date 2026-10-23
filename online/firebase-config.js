/* Where the site talks to Firebase.
 *
 * The address that ships with the sources is stored ENCODED (every byte XOR a
 * key, written as hex), so a plain-text read of the repository, of a fork or of
 * a search index no longer shows it. Be honest about what that buys: the key
 * sits next to the value, so this is hiding, not cryptography — anyone who
 * opens the built script can decode it. Real secrecy comes from
 * online/firebase-secret.js (gitignored; CI generates it from the
 * PVG3_DATABASE_HOST and PVG3_DATABASE_AUTH secrets), which exports the same
 * two names and replaces both values at load time. See firebase/README.md.
 *
 * Regenerate the pair with:
 *   sh tools/write_online_config.sh --encode <host> <key>
 */
const ENCODED_HOST =
  '2a0e53625a56374c006d5d5d280857230e3f47412e1c567f115e20115038035c240118350d26';
const HOST_KEY = 'Zx4Qw7Rt2Yp9Mn6VbKj3';

const decodeHost = (hex, key) => {
  let out = '';
  for (let i = 0; i + 1 < hex.length; i += 2) {
    const byte = Number.parseInt(hex.slice(i, i + 2), 16);
    if (!Number.isInteger(byte)) return '';
    out += String.fromCharCode(byte ^ key.charCodeAt((i / 2) % key.length));
  }
  return out;
};

export const DATABASE_HOST = decodeHost(ENCODED_HOST, HOST_KEY);
/* Optional ?auth= token appended to every request. Leave empty for the public
 * catalog: players authenticate with their own account token instead. */
export const DATABASE_AUTH = '';
