#include "world/volcano_terrain.h"
#include "raymath.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static Vector3 Offset(SurfaceAddress address, float x, float y)
{
    SurfaceFrame frame = SurfaceFrameAt(address);
    return Vector3Normalize(Vector3Add(frame.up,
        Vector3Add(Vector3Scale(frame.east, x / SURFACE_RADIUS_BLOCKS),
                   Vector3Scale(frame.north, y / SURFACE_RADIUS_BLOCKS))));
}

static void TestShapeAndIdentity(void)
{
    unsigned count = 0;
    for (SurfaceFace face = 0; face < SURFACE_FACE_COUNT; face++) {
        for (int v = 320; v < 4096; v += 512) for (int u = 320; u < 4096; u += 512) {
            SurfaceAddress center = {7, face, u, v, 0};
            VolcanoTerrainDefinition d, again;
            if (!VolcanoTerrainDefine(12345, .8f, center, &d)) continue;
            count++;
            assert(VolcanoTerrainDefine(12345, .8f, center, &again));
            assert(d.seed == again.seed && d.radius == again.radius && d.height == again.height);
            VolcanoTerrainSample middle = VolcanoTerrainShape(&d, SurfaceAddressDirection(center));
            assert(fabsf(middle.height - (d.height - d.craterDepth)) < .001f);
            assert(middle.crater > .999f && middle.foundation > .999f);
            for (int angle = 0; angle < 12; angle++) {
                float a = angle * PI / 6;
                float rim = 0;
                for (int r = 10; r <= 38; r++) {
                    float h = VolcanoTerrainShape(&d, Offset(center, r*cosf(a), r*sinf(a))).height;
                    rim = fmaxf(rim, h);
                }
                assert(rim > middle.height + 15);
                assert(VolcanoTerrainShape(&d, Offset(center, d.radius*1.3f*cosf(a),
                                                     d.radius*1.3f*sinf(a))).height == 0);
            }
            VolcanoTerrainSample actual = VolcanoTerrainAt(12345, .8f, SurfaceAddressDirection(center));
            assert(actual.definition.seed == d.seed);
            assert(fabsf(actual.height - middle.height) < .001f);
        }
    }
    assert(count > 50 && count < 300);
}

static void TestNeighborhoodCoverage(void)
{
    /* Compare the optimized lookup with every center, including cube edges. */
    for (int i = 0; i < 500; i++) {
        Vector3 p = Vector3Normalize((Vector3){sinf(i*2.17f), cosf(i*.73f), sinf(i*1.19f+.1f)});
        if (i < 6) p = Vector3Normalize((Vector3){1, i%2 ? -1 : 1, (i/2-1)*1.0f});
        float brute = 0;
        for (SurfaceFace face=0; face<SURFACE_FACE_COUNT; face++)
            for (int v=320; v<4096; v+=512) for (int u=320; u<4096; u+=512) {
                VolcanoTerrainDefinition d;
                if (VolcanoTerrainDefine(88, 1, (SurfaceAddress){0,face,u,v,0}, &d))
                    brute = fmaxf(brute, VolcanoTerrainShape(&d,p).height);
            }
        assert(fabsf(VolcanoTerrainAt(88,1,p).height-brute) < .001f);
    }
    /* Unit-sphere queries share their support across longitude wrap/poles. */
    for (int i=0; i<400; i++) {
        float latitude = -1.57f + i * 3.14f / 399;
        Vector3 left = { -cosf(latitude), sinf(latitude), .0000001f };
        Vector3 right = { -cosf(latitude), sinf(latitude), -.0000001f };
        assert(fabsf(VolcanoTerrainAt(88,1,left).height - VolcanoTerrainAt(88,1,right).height) < .02f);
    }
}

int main(void)
{
    TestShapeAndIdentity();
    TestNeighborhoodCoverage();
    VolcanoTerrainDefinition d;
    SurfaceAddress center = {0,0,320,320,0};
    assert(!VolcanoTerrainDefine(0,0,center,&d));
    assert(!VolcanoTerrainDefine(0,NAN,center,&d));
    assert(!VolcanoTerrainDefine(0,1,(SurfaceAddress){0,0,321,320,0},&d));
    assert(VolcanoTerrainAt(0,0,(Vector3){1,0,0}).height == 0);
    assert(VolcanoTerrainAt(0,1,(Vector3){NAN,0,0}).height == 0);
    assert(VolcanoTerrainShape(NULL,(Vector3){1,0,0}).height == 0);
    puts("volcano terrain shape, identity and spherical support tests passed");
}
