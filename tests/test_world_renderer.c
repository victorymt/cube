#include "presentation/world_renderer.h"

#include "raymath.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void TestMaterialProfiles(void)
{
    WorldMaterialProfile soil = WorldMaterialForTexture(TEX_DIRT);
    WorldMaterialProfile water = WorldMaterialForTexture(TEX_WATER);
    WorldMaterialProfile metal = WorldMaterialForTexture(TEX_IRON_ORE);
    WorldMaterialProfile copper = WorldMaterialForTexture(TEX_COPPER_ORE);
    WorldMaterialProfile gravel = WorldMaterialForTexture(TEX_GRAVEL);
    WorldMaterialProfile crystal = WorldMaterialForTexture(TEX_CRYSTAL);
    WorldMaterialProfile granite = WorldMaterialForTexture(TEX_GRANITE);
    WorldMaterialProfile peat = WorldMaterialForTexture(TEX_PEAT);
    WorldMaterialProfile sulfur = WorldMaterialForTexture(TEX_SULFUR_ORE);
    WorldMaterialProfile packedIce = WorldMaterialForTexture(TEX_PACKED_ICE);
    WorldMaterialProfile gneiss = WorldMaterialForTexture(TEX_GNEISS);
    WorldMaterialProfile loam = WorldMaterialForTexture(TEX_LOAM);
    WorldMaterialProfile nickel = WorldMaterialForTexture(TEX_NICKEL_ORE);
    WorldMaterialProfile lava = WorldMaterialForTexture(TEX_LAVA);
    WorldMaterialProfile moss = WorldMaterialForTexture(TEX_MOSS_CARPET);
    WorldMaterialProfile microbial = WorldMaterialForTexture(TEX_MICROBIAL_MAT);
    WorldMaterialProfile luminous = WorldMaterialForTexture(TEX_LUMINOUS_POD);
    WorldMaterialProfile bloom = WorldMaterialForTexture(TEX_CRYSTAL_BLOOM);
    WorldMaterialProfile frond = WorldMaterialForTexture(TEX_CANOPY_FROND);
    assert(soil.kind == WORLD_MATERIAL_OPAQUE);
    assert(soil.roughness > 0.9f);
    assert(water.kind == WORLD_MATERIAL_WATER);
    assert(water.specular > 0.8f);
    assert(metal.kind == WORLD_MATERIAL_METAL);
    assert(copper.kind == WORLD_MATERIAL_METAL);
    assert(gravel.kind == WORLD_MATERIAL_OPAQUE);
    assert(gravel.roughness > 0.85f);
    assert(crystal.kind == WORLD_MATERIAL_OPAQUE);
    assert(crystal.specular > 0.6f);
    assert(crystal.emission == 0.0f);
    assert(granite.kind == WORLD_MATERIAL_OPAQUE);
    assert(granite.roughness > 0.85f);
    assert(peat.kind == WORLD_MATERIAL_OPAQUE);
    assert(peat.roughness > 0.9f);
    assert(sulfur.kind == WORLD_MATERIAL_OPAQUE);
    assert(sulfur.roughness > 0.85f);
    assert(packedIce.kind == WORLD_MATERIAL_OPAQUE);
    assert(packedIce.specular > 0.6f);
    assert(gneiss.roughness > 0.85f);
    assert(loam.roughness > 0.9f);
    assert(nickel.kind == WORLD_MATERIAL_METAL);
    assert(metal.roughness < soil.roughness);
    assert(lava.emission == 1.0f);
    assert(moss.roughness > 0.9f && moss.specular < 0.1f);
    assert(microbial.roughness == moss.roughness);
    assert(luminous.emission > 0.7f);
    assert(luminous.emission < lava.emission);
    assert(bloom.specular > 0.6f && bloom.roughness < 0.3f);
    assert(frond.roughness > 0.8f && frond.emission == 0.0f);
}

static void TestEveryTextureHasValidMaterialProfile(void)
{
    for (int texture = 0; texture < TEX_COUNT; texture++) {
        WorldMaterialProfile profile =
            WorldMaterialForTexture((BlockTexture)texture);
        assert(isfinite(profile.roughness));
        assert(isfinite(profile.specular));
        assert(isfinite(profile.emission));
        assert(profile.roughness >= 0.0f && profile.roughness <= 1.0f);
        assert(profile.specular >= 0.0f && profile.specular <= 1.0f);
        assert(profile.emission >= 0.0f && profile.emission <= 1.0f);
        assert(profile.kind >= WORLD_MATERIAL_OPAQUE);
        assert(profile.kind <= WORLD_MATERIAL_METAL);
    }

    WorldMaterialProfile below = WorldMaterialForTexture((BlockTexture)-1);
    WorldMaterialProfile above = WorldMaterialForTexture((BlockTexture)TEX_COUNT);
    assert(below.roughness == 0.82f && below.specular == 0.10f);
    assert(below.emission == 0.0f && below.kind == WORLD_MATERIAL_OPAQUE);
    assert(above.roughness == below.roughness);
    assert(above.specular == below.specular);
    assert(above.emission == below.emission);
    assert(above.kind == below.kind);
}

static void TestLightingSanitization(void)
{
    WorldLightingState invalid = {
        .sunDirection = { NAN, 0.0f, 0.0f },
        .cameraPosition = { INFINITY, 0.0f, 0.0f },
        .directStrength = NAN,
        .ambientStrength = -4.0f,
        .shadowStrength = 12.0f,
        .fogDensity = INFINITY,
        .fogStart = -50.0f,
        .wetness = 3.0f,
        .exposure = NAN,
        .saturation = 8.0f,
        .warmth = -2.0f,
        .waveStrength = INFINITY,
        .time = NAN,
        .shadowsEnabled = true
    };
    WorldLightingState state = WorldLightingStateSanitize(invalid);
    assert(fabsf(Vector3Length(state.sunDirection) - 1.0f) < 0.0001f);
    assert(Vector3LengthSqr(state.cameraPosition) == 0.0f);
    assert(state.directStrength == 0.0f);
    assert(state.ambientStrength >= 0.02f);
    assert(state.shadowStrength <= 0.92f);
    assert(state.fogDensity == 0.0f);
    assert(state.fogStart == 0.0f);
    assert(state.wetness == 1.0f);
    assert(state.exposure == 1.0f);
    assert(state.saturation == 1.5f);
    assert(state.warmth == 0.0f);
    assert(state.waveStrength == 0.18f);
    assert(state.time == 0.0f);
    assert(!state.shadowsEnabled);
}

static void TestNoContextFallback(void)
{
    WorldRendererShutdown();
    assert(!WorldRendererIsReady());
    assert(!WorldRendererShadowsReady());
    assert(WorldRendererTextureBytes() == 0);
    assert(!WorldRendererInit(GRAPHICS_QUALITY_MEDIUM));
    assert(!WorldRendererIsReady());
    WorldRendererShutdown();
    WorldRendererShutdown();
}

static void TestShadowBounds(void)
{
    Vector3 target = { 0.0f, 0.0f, 0.0f };
    Vector3 lightDirection = { 0.0f, -1.0f, 0.0f };
    assert(WorldRendererTestShadowSphereVisible(
        target, lightDirection, (Vector3){ 0.0f, 0.0f, 0.0f }, 8.0f));
    assert(WorldRendererTestShadowSphereVisible(
        target, lightDirection, (Vector3){ 72.0f, 0.0f, 0.0f }, 8.0f));
    assert(!WorldRendererTestShadowSphereVisible(
        target, lightDirection, (Vector3){ 72.1f, 0.0f, 0.0f }, 8.0f));
    assert(WorldRendererTestShadowSphereVisible(
        target, lightDirection, (Vector3){ 0.0f, 0.0f, -72.0f }, 8.0f));
    assert(!WorldRendererTestShadowSphereVisible(
        target, lightDirection, (Vector3){ 0.0f, 0.0f, -72.1f }, 8.0f));
    assert(!WorldRendererTestShadowSphereVisible(
        target, lightDirection, target, -1.0f));
}

int main(void)
{
    TestMaterialProfiles();
    TestEveryTextureHasValidMaterialProfile();
    TestLightingSanitization();
    TestShadowBounds();
    TestNoContextFallback();
    puts("world renderer tests passed");
    return 0;
}
