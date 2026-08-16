#include "gameplay/ship_ground_effects.h"

#include "gameplay/ship_exhaust.h"
#include "presentation/particles.h"
#include "raymath.h"
#include "world/block_atlas.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdint.h>

typedef struct ShipGroundEffectSample {
    bool found;
    bool liquid;
    BlockType block;
    float distance;
    Vector3 point;
} ShipGroundEffectSample;

static float dustCarry = 0.0f;
static uint32_t randomState = 0x5a17c9e3u;

static float GroundEffectRandom(void)
{
    uint32_t value = randomState;
    if (value == 0u) value = 0x9e3779b9u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    randomState = value;
    return (float)(value >> 8) * (1.0f / 16777216.0f);
}

void ShipGroundEffectsReset(void)
{
    dustCarry = 0.0f;
    randomState = 0x5a17c9e3u;
}

static ShipGroundEffectSample GroundEffectAt(const Player *player)
{
    ShipGroundEffectSample sample = { 0 };
    if (!player || !WorldIsSurfaceActive()) return sample;
    int x = (int)floorf(player->position.x);
    int z = (int)floorf(player->position.z);
    int startY = (int)floorf(player->position.y - 0.35f);
    int minimumY = startY - 8;
    if (minimumY < WorldSurfaceMinY()) minimumY = WorldSurfaceMinY();
    for (int y = startY; y >= minimumY; y--) {
        BlockType block = GetBlockAt(x, y, z);
        if (block == BLOCK_AIR) continue;
        bool liquid = IsLiquidBlock(block);
        float height = liquid ? 1.0f : BlockCollisionHeight(block);
        if (!liquid && height <= 0.0f) continue;
        float top = (float)y + height;
        sample = (ShipGroundEffectSample){
            .found = true,
            .liquid = liquid,
            .block = block,
            .distance = fmaxf(0.0f, player->position.y - top),
            .point = { player->position.x, top + 0.05f,
                       player->position.z }
        };
        break;
    }
    return sample;
}

static Color GroundEffectColor(const ShipGroundEffectSample *sample)
{
    if (sample->liquid) return (Color){ 176, 218, 235, 180 };
    Color base = BlockBaseColor(sample->block);
    return (Color){
        (unsigned char)Clamp((float)base.r * 0.72f + 58.0f, 0.0f, 255.0f),
        (unsigned char)Clamp((float)base.g * 0.72f + 52.0f, 0.0f, 255.0f),
        (unsigned char)Clamp((float)base.b * 0.68f + 46.0f, 0.0f, 255.0f),
        185
    };
}

void ShipGroundEffectsEmit(const Player *player, float dt,
                           float requestedIntensity, bool burst)
{
    ShipGroundEffectSample ground = GroundEffectAt(player);
    float intensity = ShipDustIntensity(ground.distance, ground.found) *
                      Clamp(requestedIntensity, 0.0f, 1.0f);
    if (intensity <= 0.01f) return;
    int count = burst ? 18 : ShipExhaustEmissionCount(
        42.0f * intensity, dt, &dustCarry, 8);
    Color startColor = GroundEffectColor(&ground);
    for (int i = 0; i < count; i++) {
        float angle = GroundEffectRandom() * 2.0f * PI;
        float radius = 0.25f + GroundEffectRandom() * 0.90f;
        Vector3 radial = { cosf(angle), 0.0f, sinf(angle) };
        Vector3 position = Vector3Add(
            ground.point, Vector3Scale(radial, radius));
        position.x += (GroundEffectRandom() - 0.5f) * 0.25f;
        position.z += (GroundEffectRandom() - 0.5f) * 0.25f;
        float speed = (1.2f + GroundEffectRandom() * 1.8f) *
                      (0.55f + intensity * 0.75f);
        Vector3 velocity = Vector3Scale(radial, speed);
        velocity.y = 0.35f + GroundEffectRandom() *
                     (ground.liquid ? 1.15f : 0.65f);
        float startSize = 0.07f + GroundEffectRandom() * 0.07f;
        float endSize = startSize * (ground.liquid ? 2.2f : 3.6f);
        Color endColor = startColor;
        endColor.a = 0;
        ParticleStyle style = {
            .startSize = { startSize, startSize, startSize },
            .endSize = { endSize, endSize * 0.72f, endSize },
            .startColor = startColor,
            .endColor = endColor,
            .gravity = ground.liquid ? 1.5f : 2.4f
        };
        ParticlesEmitStyled(position, velocity, &style,
                            (ground.liquid ? 0.48f : 0.62f) +
                                GroundEffectRandom() * 0.28f);
    }
}
