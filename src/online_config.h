#ifndef PVG3_ONLINE_CONFIG_H_INCLUDED
#define PVG3_ONLINE_CONFIG_H_INCLUDED

/* Where the game talks to Firebase. Both values can be replaced at build time
 * without touching the sources: drop a src/online_config_secret.h next to this
 * header (see online_config_secret.h.example, the file itself is gitignored) or
 * let CI generate it from the PVG3_DATABASE_HOST / PVG3_DATABASE_AUTH secrets.
 * The defaults below keep a fresh checkout buildable. */
#if defined(__has_include)
#if __has_include("online_config_secret.h")
#include "online_config_secret.h"
#endif
#endif

#ifndef PVG3_DATABASE_HOST
#define PVG3_DATABASE_HOST "pvg3-ae824-default-rtdb.firebaseio.com"
#endif

/* Optional ?auth= token appended to every request. Leave empty for the public
 * catalog: accounts authenticate with their own per-account token instead. */
#ifndef PVG3_DATABASE_AUTH
#define PVG3_DATABASE_AUTH ""
#endif

#endif /* PVG3_ONLINE_CONFIG_H_INCLUDED */
