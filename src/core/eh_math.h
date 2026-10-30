/* Private float helpers for the core (not part of the public API). */
#ifndef EH_MATH_H
#define EH_MATH_H

#include <stdint.h>

#define EH_PI 3.14159265358979f
#define EH_TAU 6.28318530717959f

static inline float eh_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline float eh_smoothstep(float t)
{
    t = eh_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* Integer hash for deterministic pseudo-randomness: "lowbias32" with the
 * shift/multiplier constants published by Chris Wellons' hash-prospector
 * (https://github.com/skeeto/hash-prospector, released under the Unlicense). */
static inline uint32_t eh_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static inline float eh_hash01(uint32_t x) { return (float)(eh_hash32(x) >> 8) * (1.0f / 16777216.0f); }

#endif
