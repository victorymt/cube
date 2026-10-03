#include "world/landform_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "raymath.h"

static LandformBaseline FlatBaseline(Vector3 d, void *user)
{
    (void)d; (void)user;
    return (LandformBaseline){ 140, .6f, .5f };
}

static void TestDeposits(void)
{
    LandformAtlas *a = LandformBuild(80, FlatBaseline, NULL);
    assert(a);
    for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i++) a->cells[i].width = 0;
    uint32_t id = LANDFORM_RESOLUTION * 64 + 64;
    LandformCell *c = &a->cells[id];
    c->downstream = LandformNeighbor(id, 1);
    LandformCell *d = &a->cells[c->downstream];
    d->downstream = LandformNeighbor(c->downstream, 1);
    c->width = 4; c->area = 100000; c->discharge = 60000;
    c->elevation = c->spill = 150; c->slope = .2f;
    d->elevation = d->spill = 140; d->slope = .01f;
    a->cells[LandformNeighbor(id, 2)].elevation = 160;
    a->cells[LandformNeighbor(id, 3)].elevation = 160;
    Vector3 axis = Vector3Normalize(Vector3Subtract(d->direction,
        Vector3Scale(c->direction, Vector3DotProduct(c->direction, d->direction))));
    axis = Vector3Normalize(Vector3Subtract(axis,
        Vector3Scale(d->direction, Vector3DotProduct(axis, d->direction))));
    Vector3 side = Vector3Normalize(Vector3CrossProduct(d->direction, axis));
    Vector3 p = Vector3Normalize(Vector3Add(d->direction,
        Vector3Scale(Vector3Add(Vector3Scale(axis, 18), Vector3Scale(side, 9)), 1.0f / SURFACE_RADIUS_BLOCKS)));
    LandformSample fan = LandformSampleAt(a, p, 140);
    assert(fan.kind == LANDFORM_FAN && fan.elevation > 140 && !fan.hasWater);
    assert(fabsf(fan.gravel + fan.sand + fan.fines - 1) < 1e-5f);
    c->discharge = 0;
    assert(LandformSampleAt(a, p, 140).kind != LANDFORM_FAN);
    c->discharge = 60000;
    d->slope = .15f;
    assert(LandformSampleAt(a, p, 140).kind != LANDFORM_FAN);
    d->slope = .01f;
    assert(LandformSampleAt(a, p, 160).kind != LANDFORM_FAN);
    a->cells[LandformNeighbor(id, 2)].elevation = 140;
    a->cells[LandformNeighbor(id, 3)].elevation = 140;
    assert(LandformSampleAt(a, p, 140).kind != LANDFORM_FAN);

    c->elevation = c->spill = 140; c->slope = .01f;
    Vector3 middle = Vector3Normalize(Vector3Lerp(c->direction, d->direction, .5f));
    p = Vector3Normalize(Vector3Add(middle, Vector3Scale(side, 16.0f / SURFACE_RADIUS_BLOCKS)));
    LandformSample plain = LandformSampleAt(a, p, 141);
    assert(plain.kind == LANDFORM_FLOODPLAIN && plain.elevation > 141);
    assert(plain.fines > plain.gravel && plain.wetness > 0);
    assert(fabsf(plain.gravel + plain.sand + plain.fines - 1) < 1e-5f);
    c->slope = .1f;
    assert(LandformSampleAt(a, p, 141).kind != LANDFORM_FLOODPLAIN);
    c->discharge = c->area * .1f;
    LandformSample dry = LandformSampleAt(a, middle, 140);
    assert(dry.kind == LANDFORM_DRY_CHANNEL && !dry.hasWater && dry.elevation < 140);
    assert(!strcmp(LandformKindName((LandformKind)-1), "unknown"));
    LandformDestroy(a);
}

static LandformBaseline Baseline(Vector3 d, void *user)
{
    float sea = *(float *)user;
    return (LandformBaseline){ sea + d.y * 80 + 15 * sinf(d.x * 22) * cosf(d.z * 17), .6f, .5f };
}
int main(void)
{
    TestDeposits();
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
            assert(s.kind == LANDFORM_RIVER || s.kind == LANDFORM_LAKE || s.kind == LANDFORM_WETLAND);
            assert(s.hasWater && s.waterLevel > s.elevation);
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
