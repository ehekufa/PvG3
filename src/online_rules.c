#include "online_rules.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

const int on_plant_cost[ON_PLANT_TYPES] = {100, 50, 50, 250, 25};
const int on_duck_type[3] = {ON_DUCK, ON_CONE, ON_BUCKET};
const int on_duck_cost[3] = {50, 100, 175};
const int on_duck_hp[3] = {180, 420, 750};
static const int plant_hp[ON_PLANT_TYPES] = {300, 4000, 300, 300, 300};
static const float plant_delay[ON_PLANT_TYPES] = {7.5f, 30, 7.5f, 20, 7.5f};
static const float duck_delay[3] = {2, 3, 5};

static int pos(int row, int col) { return row * ON_COLS + col; }
static int cx(int col) { return ON_BOARD_X + col * ON_CELL_W + ON_CELL_W / 2; }
static int cy(int row) { return ON_BOARD_Y + row * ON_CELL_H + ON_CELL_H / 2; }
static int water(const OnMatch *s, int row) { return s->map == 5 && (row == 1 || row == 2); }
static float max_zero(float n) { return n > 0 ? n : 0; }

void on_match_new(OnMatch *s, int map) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->version = 1;
    s->map = map == 5 ? 5 : 1;
    s->plant_cash = 250;
    s->zombie_cash = 175;
    s->left = ON_WAVE;
    s->next_id = 1;
    for (int i = 0; i < ON_CELLS; i++) s->plants[i].type = -1;
    for (int i = 0; i < ON_ROWS; i++) s->mowers[i].x = ON_BOARD_X + 30;
}

int on_match_valid(const OnMatch *s) {
    if (!s || s->version != 1 || (s->map != 1 && s->map != 5) ||
        !isfinite(s->time) || s->time < 0 ||
        s->duck_count < 0 || s->duck_count > ON_DUCK_CAP ||
        s->pea_count < 0 || s->pea_count > ON_PEA_CAP ||
        s->coin_count < 0 || s->coin_count > ON_COIN_CAP ||
        s->plant_cash < 0 || s->zombie_cash < 0 ||
        s->left < 0 || s->left > ON_WAVE ||
        s->next_id < 1 || s->ack_guest < 0 ||
        s->winner < ON_NO_WINNER || s->winner > ON_WIN_ZOMBIES ||
        !isfinite(s->zombie_income)) return 0;
    for (int i = 0; i < ON_CELLS; i++) {
        OnPlant p = s->plants[i];
        if (p.type < -1 || p.type > ON_JUMPER ||
            (p.type != -1 && (!isfinite(p.hp) || !isfinite(p.fire))) ||
            s->lilies[i] > 1) return 0;
    }
    for (int i = 0; i < ON_PLANT_TYPES; i++)
        if (!isfinite(s->plant_cooldown[i])) return 0;
    for (int i = 0; i < 3; i++)
        if (!isfinite(s->duck_cooldown[i])) return 0;
    for (int i = 0; i < s->duck_count; i++) {
        OnDuck d = s->ducks[i];
        if ((d.type != ON_DUCK && d.type != ON_CONE && d.type != ON_BUCKET) ||
            d.row < 0 || d.row >= ON_ROWS || !isfinite(d.x) ||
            !isfinite(d.hp) || !isfinite(d.max_hp) || d.max_hp <= 0 ||
            !isfinite(d.speed) || !isfinite(d.anim) || d.id < 1) return 0;
    }
    for (int i = 0; i < s->pea_count; i++)
        if (s->peas[i].row < 0 || s->peas[i].row >= ON_ROWS ||
            !isfinite(s->peas[i].x) || !isfinite(s->peas[i].y)) return 0;
    for (int i = 0; i < s->coin_count; i++)
        if (s->coins[i].id < 1 || !isfinite(s->coins[i].x) ||
            !isfinite(s->coins[i].y) || !isfinite(s->coins[i].life)) return 0;
    for (int i = 0; i < ON_ROWS; i++)
        if (s->mowers[i].used < 0 || s->mowers[i].used > 1 ||
            s->mowers[i].running < 0 || s->mowers[i].running > 1 ||
            !isfinite(s->mowers[i].x)) return 0;
    return 1;
}

int on_match_apply(OnMatch *s, int role, const OnCommand *c) {
    if (!on_match_valid(s) || s->winner || !c) return 0;
    if (role == ON_ROLE_PLANTS) {
        if (c->kind == ON_CMD_COIN) {
            for (int i = 0; i < s->coin_count; i++) if (s->coins[i].id == c->id) {
                memmove(&s->coins[i], &s->coins[i + 1],
                        (size_t)(--s->coin_count - i) * sizeof(s->coins[0]));
                s->plant_cash += 25;
                return 1;
            }
            return 0;
        }
        if (c->kind != ON_CMD_PLANT || c->row < 0 || c->row >= ON_ROWS ||
            c->col < 0 || c->col >= ON_COLS ||
            c->type < 0 || c->type >= ON_PLANT_TYPES) return 0;
        int index = pos(c->row, c->col), type = c->type, wet = water(s, c->row);
        if (s->plants[index].type != -1 || s->plant_cash < on_plant_cost[type] ||
            s->plant_cooldown[type] > 0 ||
            (type == ON_LILY ? (!wet || s->lilies[index]) : (wet && !s->lilies[index])))
            return 0;
        if (type == ON_LILY) s->lilies[index] = 1;
        else {
            s->plants[index].type = type;
            s->plants[index].hp = (float)plant_hp[type];
            s->plants[index].fire = type == ON_SUNFLOWER ? 5 : .4f;
        }
        s->plant_cash -= on_plant_cost[type];
        s->plant_cooldown[type] = plant_delay[type];
        return 1;
    }
    if (role == ON_ROLE_ZOMBIES) {
        if (c->kind == ON_CMD_FINISH) {
            s->left = 0;
            if (!s->duck_count) s->winner = ON_WIN_PLANTS;
            return 1;
        }
        if (c->kind != ON_CMD_SPAWN || c->row < 0 || c->row >= ON_ROWS) return 0;
        int t = -1;
        for (int i = 0; i < 3; i++) if (c->type == on_duck_type[i]) t = i;
        if (t < 0 || s->left <= 0 || s->duck_count >= 70 ||
            s->zombie_cash < on_duck_cost[t] || s->duck_cooldown[t] > 0) return 0;
        OnDuck *d = &s->ducks[s->duck_count++];
        d->id = s->next_id++;
        d->type = on_duck_type[t];
        d->row = c->row;
        d->x = 1280 + 45 + (s->next_id % 3) * 12;
        d->hp = d->max_hp = (float)on_duck_hp[t];
        d->speed = 24;
        d->anim = 0;
        s->left--;
        s->zombie_cash -= on_duck_cost[t];
        s->duck_cooldown[t] = duck_delay[t];
        return 1;
    }
    return 0;
}

static void remove_duck(OnMatch *s, int i) {
    s->duck_count--;
    memmove(&s->ducks[i], &s->ducks[i + 1],
            (size_t)(s->duck_count - i) * sizeof(s->ducks[0]));
}
static void remove_pea(OnMatch *s, int i) {
    s->pea_count--;
    memmove(&s->peas[i], &s->peas[i + 1],
            (size_t)(s->pea_count - i) * sizeof(s->peas[0]));
}
static void remove_coin(OnMatch *s, int i) {
    s->coin_count--;
    memmove(&s->coins[i], &s->coins[i + 1],
            (size_t)(s->coin_count - i) * sizeof(s->coins[0]));
}

void on_match_step(OnMatch *s, float dt) {
    if (!s || s->version != 1 || s->winner) return;
    if (!isfinite(dt) || dt < 0) dt = 0;
    if (dt > .05f) dt = .05f;
    s->time += dt;
    for (int i = 0; i < ON_PLANT_TYPES; i++)
        s->plant_cooldown[i] = max_zero(s->plant_cooldown[i] - dt);
    for (int i = 0; i < 3; i++)
        s->duck_cooldown[i] = max_zero(s->duck_cooldown[i] - dt);
    s->zombie_income += dt;
    if (s->zombie_income >= 5) {
        s->zombie_income -= 5;
        s->zombie_cash += 75;
        if (s->zombie_cash > 3000) s->zombie_cash = 3000;
    }
    for (int r = 0; r < ON_ROWS; r++) for (int col = 0; col < ON_COLS; col++) {
        int i = pos(r, col);
        OnPlant *p = &s->plants[i];
        if (p->type < 0) continue;
        if (p->hp <= 0) { p->type = -1; continue; }
        if (p->type == ON_PEA_PLANT) {
            p->fire -= dt;
            int has_duck = 0;
            for (int k = 0; k < s->duck_count; k++)
                if (s->ducks[k].row == r && s->ducks[k].x > cx(col)) has_duck = 1;
            if (p->fire <= 0 && has_duck) {
                if (s->pea_count < 190)
                    s->peas[s->pea_count++] = (OnPea){r, cx(col) + 16, cy(r) - 6};
                p->fire = 1.4f;
            }
        } else if (p->type == ON_SUNFLOWER) {
            p->fire -= dt;
            if (p->fire <= 0) {
                if (s->coin_count < 70)
                    s->coins[s->coin_count++] = (OnCoin){s->next_id++, cx(col) + 26,
                                                         cy(r) + 28, 16};
                p->fire = 8;
            }
        }
    }
    for (int i = 0; i < s->pea_count;) {
        OnPea *p = &s->peas[i];
        p->x += 540 * dt;
        int hit = 0;
        for (int j = 0; j < s->duck_count; j++) {
            OnDuck *d = &s->ducks[j];
            if (d->row == p->row && p->x >= d->x - 25 && p->x <= d->x + 21) {
                d->hp -= 20; hit = 1; break;
            }
        }
        if (hit || p->x >= 1300) remove_pea(s, i);
        else i++;
    }
    for (int i = 0; i < s->duck_count;)
        if (s->ducks[i].hp <= 0) remove_duck(s, i);
        else i++;
    for (int i = 0; i < s->duck_count; i++) {
        OnDuck *d = &s->ducks[i];
        d->anim += dt * 8;
        int col = (int)floorf((d->x - ON_BOARD_X) / ON_CELL_W);
        if (col < 0) col = 0;
        if (col >= ON_COLS) col = ON_COLS - 1;
        int idx = pos(d->row, col);
        OnPlant *p = &s->plants[idx];
        if (d->x <= ON_BOARD_X + 70) {
            OnMower *m = &s->mowers[d->row];
            if (!m->used) { m->used = 1; m->running = 1; }
            else if (!m->running && d->x < ON_BOARD_X - 25) {
                s->winner = ON_WIN_ZOMBIES; break;
            }
        }
        if (d->x <= cx(col) + 28 && p->type == ON_JUMPER) {
            p->type = -1;
            d->x += 3 * ON_CELL_W;
        } else if (d->x <= cx(col) + 28 && p->type >= 0) {
            p->hp -= 100 * dt;
        } else if (d->x <= cx(col) + 28 && s->lilies[idx]) {
            s->lilies[idx] = 0;
        } else d->x -= d->speed * dt;
    }
    for (int r = 0; r < ON_ROWS; r++) {
        OnMower *m = &s->mowers[r];
        if (!m->running) continue;
        m->x += 520 * dt;
        for (int i = 0; i < s->duck_count;)
            if (s->ducks[i].row == r && s->ducks[i].x < m->x + 40 &&
                s->ducks[i].x > m->x - 25) remove_duck(s, i);
            else i++;
        if (m->x > 1320) m->running = 0;
    }
    for (int i = 0; i < s->coin_count;)
        if ((s->coins[i].life -= dt) <= 0) remove_coin(s, i);
        else i++;
    if (!s->winner && s->left == 0 && s->duck_count == 0)
        s->winner = ON_WIN_PLANTS;
}
