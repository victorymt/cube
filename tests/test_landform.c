#include "world/landform_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static LandformBaseline Baseline(Vector3 d, void *user)
{
    float sea = *(float *)user;
    return (LandformBaseline){ sea + d.y * 80 + 15 * sinf(d.x * 22) * cosf(d.z * 17), .6f, .5f };
}
int main(void)
{
    double total = 0;
    for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i++) {
        assert(LandformCellAtDirection(LandformCellDirection(i)) == i);
        total += LandformCellArea(i);
        for (int e = 0; e < 4; e++) {
            uint32_t n = LandformNeighbor(i,e);
            assert(n != i && n < LANDFORM_CELL_COUNT);
            bool reciprocal = false;
            for (int j = 0; j < 4; j++) reciprocal |= LandformNeighbor(n,j) == i;
            assert(reciprocal);
        }
    }
    double sphere = 4.0 * PI * SURFACE_RADIUS_BLOCKS * SURFACE_RADIUS_BLOCKS;
    assert(fabs(total - sphere) < sphere * .00001);
    float sea = 80;
    clock_t start = clock();
    LandformAtlas *a = LandformBuild(sea, Baseline, &sea);
    LandformAtlas *b = LandformBuild(sea, Baseline, &sea);
    assert(a && b && a->rivers && a->lakes);
    assert(!memcmp(a->cells, b->cells, sizeof(*a->cells) * LANDFORM_CELL_COUNT));
    double outlets = 0;
    for (uint32_t i=0;i<LANDFORM_CELL_COUNT;i++) {
        LandformCell *c=&a->cells[i];
        if (c->downstream == LANDFORM_NONE) { outlets += c->area; continue; }
        LandformCell *d=&a->cells[c->downstream];
        assert(d->order < c->order && d->spill <= c->spill);
        assert(d->area >= c->area && d->discharge >= c->discharge);
        assert(d->watershed == c->watershed);
    }
    assert(fabs(outlets-total)<total*.00001);
    printf("landform: %u rivers, %u lake cells, %.2f MiB, %.3fs/build\n",a->rivers,a->lakes,a->bytes/1048576.0,(double)(clock()-start)/CLOCKS_PER_SEC/2);
    unsigned channels = 0;
    for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i++) {
        LandformCell *c = &a->cells[i];
        if (c->width == 0 || c->spill - c->elevation > 1.5f) continue;
        LandformCell *d = &a->cells[c->downstream];
        float previous = INFINITY;
        for (int k = 0; k <= 8; k++) {
            float t = k / 8.0f;
            Vector3 p = { c->direction.x * (1-t) + d->direction.x * t,
                          c->direction.y * (1-t) + d->direction.y * t,
                          c->direction.z * (1-t) + d->direction.z * t };
            float length = sqrtf(p.x*p.x + p.y*p.y + p.z*p.z);
            p.x /= length; p.y /= length; p.z /= length;
            LandformSample s = LandformSampleAt(a, p, 150);
            assert(isfinite(s.elevation));
            if (s.kind == LANDFORM_RIVER) {
                assert(s.hasWater && s.waterLevel > s.elevation);
                assert(s.waterLevel <= previous + .01f);
                previous = s.waterLevel;
                channels++;
            }
        }
    }
    assert(channels > 100);
    LandformDestroy(a); LandformDestroy(b);
    puts("landform tests passed");
}
