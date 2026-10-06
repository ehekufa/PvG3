#ifndef PVG3_ONLINE_PARTICLE_H
#define PVG3_ONLINE_PARTICLE_H

#include "online_level.h"

#include <math.h>
#include <stdint.h>

/* Keep particle birth, motion and lifetime deterministic across native
 * gameplay and the LVGL editor preview. The browser mirrors this sampler. */
static inline uint32_t on_particle_hash(unsigned id, unsigned index,
                                        unsigned lane) {
    uint32_t value = (uint32_t)id ^
        ((uint32_t)index * UINT32_C(0x9e3779b9)) ^
        ((uint32_t)lane * UINT32_C(0x85ebca6b));
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16;
    return value;
}

static inline float on_particle_random(unsigned id, unsigned index,
                                       unsigned lane) {
    return (float)(on_particle_hash(id, index, lane) >> 8) *
           (1.0f / 16777216.0f);
}

/* Returns one alive particle's local pixel position, diameter and opacity.
 * `slot` is a stable draw slot in [0, ON_LEVEL_PARTICLE_MAX_VISIBLE). */
static inline int on_level_particle_sample(const OnLevelParticle *emitter,
                                           int id, float time, int slot,
                                           float area_w, float area_h,
                                           float *out_x, float *out_y,
                                           float *out_size,
                                           float *out_opacity) {
    if (!on_level_particle_valid(emitter) || !emitter->enabled || slot < 0 ||
        slot >= ON_LEVEL_PARTICLE_MAX_VISIBLE || !isfinite(time) ||
        !(time >= 0.0f)) return 0;
    const float lifetime = emitter->lifetime;
    const float rate = (float)emitter->rate;
    if (lifetime <= 0.0f || rate <= 0.0f) return 0;

    int particle_index = 0;
    float age = 0.0f;
    if (emitter->continuous) {
        float phase = time * rate + on_particle_random((unsigned)id, 0u, 1u);
        int newest = (int)floorf(phase);
        age = (phase - (float)newest + (float)slot) / rate;
        if (age > lifetime) return 0;
        particle_index = newest - slot;
    } else {
        const float period = 1.5f;
        int current_burst = (int)floorf(time / period);
        float burst_phase = time - (float)current_burst * period;
        int particles_per_burst = emitter->rate;
        int burst_index = slot / particles_per_burst;
        int index_in_burst = slot % particles_per_burst;
        if (current_burst < burst_index ||
            (float)burst_index * period > lifetime + period) return 0;
        float birth_offset = .24f * (float)index_in_burst /
                             (float)particles_per_burst;
        age = burst_phase + (float)burst_index * period - birth_offset;
        if (age < 0.0f || age > lifetime) return 0;
        particle_index = (current_burst - burst_index) * particles_per_burst +
                         index_in_burst;
    }

    unsigned seed = (unsigned)particle_index;
    float angle_jitter = on_particle_random((unsigned)id, seed, 2u);
    float angle = ((float)emitter->direction +
                   (angle_jitter - .5f) * (float)emitter->spread) *
                  .01745329251994329577f;
    float speed = (float)emitter->speed *
                  (.75f + .5f * on_particle_random((unsigned)id, seed, 3u));
    float remaining = 1.0f - age / lifetime;
    float start_x = (on_particle_random((unsigned)id, seed, 4u) - .5f) * area_w * .32f;
    float start_y = (on_particle_random((unsigned)id, seed, 5u) - .5f) * area_h * .32f;
    float x = start_x + cosf(angle) * speed * age;
    float y = start_y + sinf(angle) * speed * age;
    if (emitter->gravity_enabled)
        y += .5f * (float)emitter->gravity * age * age;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_size)
        *out_size = (float)emitter->size * (.62f + .38f * remaining) *
                    (.82f + .36f * on_particle_random((unsigned)id, seed, 6u));
    if (out_opacity)
        *out_opacity = remaining * (.78f + .22f *
            on_particle_random((unsigned)id, seed, 7u));
    return 1;
}

#endif
