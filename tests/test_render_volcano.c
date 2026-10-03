#include "presentation/render_volcano.h"
#include "presentation/surface_render_transform.h"
#include "raymath.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static Matrix drawTransform;
static unsigned depth, draws;
static Vector3 bottoms[4], tops[4];
void rlPushMatrix(void) { assert(depth++ == 0u); }
void rlPopMatrix(void) { assert(depth-- == 1u); }
void rlMultMatrixf(const float *m)
{
    assert(depth == 1u);
    drawTransform = (Matrix){ .m0=m[0], .m1=m[1], .m2=m[2], .m3=m[3],
        .m4=m[4], .m5=m[5], .m6=m[6], .m7=m[7], .m8=m[8], .m9=m[9],
        .m10=m[10], .m11=m[11], .m12=m[12], .m13=m[13], .m14=m[14], .m15=m[15] };
}
void DrawCube(Vector3 p, float width, float height, float length, Color color)
{
    (void)color;
    assert(depth == 1u && draws < 4u && width > 0.0f && length > 0.0f);
    bottoms[draws] = Vector3Transform((Vector3){ p.x-width/2, p.y-height/2, p.z-length/2 }, drawTransform);
    tops[draws] = Vector3Transform((Vector3){ p.x+width/2, p.y+height/2, p.z+length/2 }, drawTransform);
    draws++;
}
static void Near(Vector3 a, Vector3 b) { assert(Vector3Distance(a,b) < 0.002f); }
static Vector3 TerrainPoint(Chunk chunk, int section, Vector3 camera,
                            int originX, int originZ, Vector3 point)
{
    if (!chunk.spherical) return Vector3Add(point, (Vector3){
        chunk.cx*CHUNK_SIZE, section*SURFACE_SECTION_HEIGHT, chunk.cz*CHUNK_SIZE });
    SurfaceFrame view = SurfaceFrameAtMapCoordinates(7u, originX+camera.x, originZ+camera.z, 0);
    SurfaceFrame patch = SurfaceFrameAtMapCoordinates(7u, originX+chunk.cx*CHUNK_SIZE,
        originZ+chunk.cz*CHUNK_SIZE, section*SURFACE_SECTION_HEIGHT);
    return Vector3Add(SurfaceFrameTransformPoint(&patch, &view, point),
                     (Vector3){ camera.x, 0, camera.z });
}
static void CheckPatch(Chunk chunk, int section, Vector3 camera, int ox, int oz)
{
    chunk.surfaceAddress.bodyId = 7u;
    Vector3 local = { 15, 9, 0 };
    LavaThermalState state = { 512u, 512u * (LAVA_FREEZING_K-LAVA_REFERENCE_K+LAVA_LATENT_K/2) };
    draws = 0;
    DrawLavaCell(SurfaceChunkRenderTransform(&chunk, section, camera, ox, oz), local, state, LAVA_ROCK_BASALT);
    assert(draws == 2u && depth == 0u);
    Near(bottoms[0], TerrainPoint(chunk, section, camera, ox, oz, local));
    Near(tops[0], TerrainPoint(chunk, section, camera, ox, oz, (Vector3){16,9.25f,1}));
    Near(bottoms[1], TerrainPoint(chunk, section, camera, ox, oz, (Vector3){15,9.25f,0}));
    Near(tops[1], TerrainPoint(chunk, section, camera, ox, oz, (Vector3){16,9.5f,1}));
    draws = 0;
    DrawAshCell(SurfaceChunkRenderTransform(&chunk, section, camera, ox, oz), local, 128u);
    assert(draws == 1u && depth == 0u);
    Near(bottoms[0], TerrainPoint(chunk, section, camera, ox, oz, local));
    Near(tops[0], TerrainPoint(chunk, section, camera, ox, oz, (Vector3){16,9.125f,1}));
    draws = 0;
    Matrix transform = SurfaceChunkRenderTransform(&chunk, section, camera, ox, oz);
    DrawLavaSteam(transform, local, 0.0, 0u, 0.0f, 0.0f);
    assert(draws == 0u && depth == 0u);
    DrawLavaSteam(transform, local, 12.0, 0u, 0.0f, 0.0f);
    assert(draws == LAVA_STEAM_PUFFS_PER_CELL && depth == 0u);
    Near(bottoms[0], TerrainPoint(chunk, section, camera, ox, oz, (Vector3){14.91f,9.087f,-0.09f}));
    Near(tops[0], TerrainPoint(chunk, section, camera, ox, oz, (Vector3){15.09f,9.213f,0.09f}));
}
int main(void)
{
    CheckPatch((Chunk){ .cx=10, .cz=0, .spherical=true }, 0, (Vector3){0,14,0}, 0, 0);
    CheckPatch((Chunk){ .cx=10, .cz=128, .spherical=true }, -2, (Vector3){3,14,2048}, 0, 0);
    CheckPatch((Chunk){ .cx=-511, .cz=254, .spherical=true }, 3, (Vector3){8180,40,4080}, 0, 0);
    CheckPatch((Chunk){ .cx=0, .cz=0, .spherical=true }, 0, (Vector3){-157,14,0}, 160, 2048);
    CheckPatch((Chunk){ .cx=-1, .cz=2 }, -2, (Vector3){3,14,0}, 160, 2048);
    puts("lava render layers follow terrain section transforms");
}
