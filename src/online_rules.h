#ifndef PVG3_ONLINE_RULES_H
#define PVG3_ONLINE_RULES_H

/* Native counterpart of online/rules.js. The browser and APK use the same
 * Firebase room/state/command format, so players can meet across platforms.
 * Online state is separate from the saved offline campaign and Zen Garden. */
#define ON_ROWS 5
#define ON_COLS 9
#define ON_CELLS (ON_ROWS * ON_COLS)
#define ON_DUCK_CAP 80
#define ON_PEA_CAP 200
#define ON_COIN_CAP 80
#define ON_WAVE 18
#define ON_BOARD_X 250
#define ON_BOARD_Y 120
#define ON_CELL_W 114
#define ON_CELL_H 112

/* Web-compatible plant IDs 0..4; duck IDs 0, 2, 3. */
enum { ON_PEA_PLANT, ON_WALL, ON_SUNFLOWER, ON_JUMPER, ON_LILY, ON_PLANT_TYPES };
enum { ON_DUCK = 0, ON_CONE = 2, ON_HELMET = 3 };
enum { ON_NO_ROLE, ON_ROLE_PLANTS, ON_ROLE_ZOMBIES };
enum { ON_NO_COMMAND, ON_CMD_PLANT, ON_CMD_COIN, ON_CMD_SPAWN, ON_CMD_FINISH };
enum { ON_NO_WINNER, ON_WIN_PLANTS, ON_WIN_ZOMBIES };

typedef struct { int type; float hp, fire; } OnPlant; /* type -1: empty */
typedef struct { int id, type, row; float x, hp, max_hp, speed, anim; } OnDuck;
typedef struct { int row; float x, y; } OnPea;
typedef struct { int id; float x, y, life; } OnCoin;
typedef struct { int used, running; float x; } OnMower;
typedef struct {
    int kind, row, col, type, id, seq;
    char player_id[33];
} OnCommand;
typedef struct {
    int version, map;
    float time;
    OnPlant plants[ON_CELLS];
    unsigned char lilies[ON_CELLS];
    OnDuck ducks[ON_DUCK_CAP]; int duck_count;
    OnPea peas[ON_PEA_CAP]; int pea_count;
    OnCoin coins[ON_COIN_CAP]; int coin_count;
    OnMower mowers[ON_ROWS];
    int plant_cash, zombie_cash;
    float plant_cooldown[ON_PLANT_TYPES], duck_cooldown[3], zombie_income;
    int left, winner, next_id, ack_guest;
} OnMatch;

extern const int on_plant_cost[ON_PLANT_TYPES];
extern const int on_duck_type[3], on_duck_cost[3], on_duck_hp[3];
void on_match_new(OnMatch *s, int map);
int on_match_valid(const OnMatch *s);
int on_match_apply(OnMatch *s, int role, const OnCommand *cmd);
void on_match_step(OnMatch *s, float dt);

#endif
