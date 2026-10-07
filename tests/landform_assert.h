#ifndef TEST_LANDFORM_ASSERT_H
#define TEST_LANDFORM_ASSERT_H
#include "world/landform_model.h"
#include <assert.h>

// Compare semantic fields, never C struct padding (which is unspecified).
static inline void AssertLandformEqual(LandformSample a, LandformSample b)
{
#define SAME(field) assert(a.field == b.field)
    SAME(elevation); SAME(waterLevel); SAME(area); SAME(slope); SAME(wetness);
    SAME(gravel); SAME(sand); SAME(fines); SAME(sedimentDepth); SAME(channelDistance);
    SAME(watershed); SAME(reach); SAME(kind); SAME(hasWater);
#undef SAME
}
#endif
