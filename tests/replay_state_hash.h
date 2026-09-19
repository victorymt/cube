#ifndef VOXELCRAFT_TEST_REPLAY_STATE_HASH_H
#define VOXELCRAFT_TEST_REPLAY_STATE_HASH_H

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Hash explicit fields, never struct padding, pointers or cache slot order.
 * These diagnostics compare runs of the same build, not floating-point
 * results from different compilers or architectures. */
static inline uint64_t ReplayHashBegin(void)
{
    return UINT64_C(14695981039346656037);
}

static inline uint64_t ReplayHashU64(uint64_t hash, uint64_t value)
{
    for (unsigned index = 0; index < 8u; index++) {
        hash = (hash ^ (value & UINT64_C(0xff))) * UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

static inline uint64_t ReplayHashFloat(uint64_t hash, float value)
{
    _Static_assert(sizeof(float) == sizeof(uint32_t), "32-bit float required");
    assert(isfinite(value));
    uint32_t bits = 0u;
    if (value != 0.0f) memcpy(&bits, &value, sizeof(bits));
    return ReplayHashU64(hash, bits);
}

static inline uint64_t ReplayHashDouble(uint64_t hash, double value)
{
    _Static_assert(sizeof(double) == sizeof(uint64_t), "64-bit double required");
    assert(isfinite(value));
    uint64_t bits = 0u;
    if (value != 0.0) memcpy(&bits, &value, sizeof(bits));
    return ReplayHashU64(hash, bits);
}

#endif
