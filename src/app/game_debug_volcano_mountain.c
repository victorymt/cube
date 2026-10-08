#include "app/game_debug_volcano_mountain.h"
#include "app/game_internal.h"
#include "world/volcano_deposition_model.h"
#include "world/volcano_terrain.h"
#include "world/lava_runtime.h"
#include "world/ash_runtime.h"
#include "world/world.h"
#include "world/world_environment.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

#define SIDE 81
#define HALF (SIDE / 2)

static LavaBoundary ImportBoundary(int x, int y, int z, void *user)
{
    (void)user;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    return GetBlockBaseAt(x - s.originX, y, z - s.originZ) == BLOCK_AIR ? LAVA_OPEN : LAVA_BLOCKED;
}
static bool Fail(GameRuntime *game, const char *reason)
{
    game->debugCommandFailed = true;
    snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure), "%s", reason);
    return false;
}
bool GameDebugVolcanoMountain(GameRuntime *game)
{
    WorldSurfaceContext world = WorldCurrentSurfaceContext();
    if (!world.active || world.kind != WORLD_SURFACE_HOME || WorldNetherIsActive() ||
        WorldTerrainMode() != TERRAIN_FLAT || WorldGetEditCount() || LavaRuntimeStats().records ||
        AshRuntimeStats().cells)
        return Fail(game, "mountain_requires_fresh_flat_world");
    /* Fixed canonical origin makes the supplied scene reproducible. */
    if (world.originX || world.originZ) return Fail(game, "mountain_requires_origin_zero");
    double ground[SIDE * SIDE];
    int base = WorldSurfaceHeightAt(0, 0) + 1;
    VolcanoTerrainDefinition d = { .center = {world.bodyId, SURFACE_FACE_POS_Y, 2048, 2048, 0},
        .seed = 1234, .radius = 27, .height = 22, .craterRadius = 6, .craterDepth = 9 };
    SurfaceFrame frame = SurfaceFrameAt(d.center);
    for (int z = 0; z < SIDE; z++) for (int x = 0; x < SIDE; x++) {
        Vector3 direction = Vector3Normalize(Vector3Add(frame.up, Vector3Add(
            Vector3Scale(frame.east, (x - HALF) / SURFACE_RADIUS_BLOCKS),
            Vector3Scale(frame.north, (z - HALF) / SURFACE_RADIUS_BLOCKS))));
        ground[z * SIDE + x] = base + floor(VolcanoTerrainShape(&d, direction).height);
    }
    VolcanoDepositionConfig config = {SIDE, SIDE, 4200, 1, 24, 293, 8, 4};
    VolcanoDepositionModel *model = VolcanoDepositionCreate(config, ground);
    if (!model) return Fail(game, "mountain_model_allocation_failed");
    /* Old east-flank episode, then a recent west-flank episode and central
       pool. These are explicit side vents; no teleporting central lava. */
    double requested = 0;
    for (unsigned tick = 0; tick < 4200; tick++) {
        unsigned x = HALF + 7, z = HALF; double supply = 0;
        if (tick < 120) supply = 4;
        if (tick >= 3900 && tick < 4020) { x = HALF - 7; z = HALF - 2; supply = 4; }
        if (tick >= 4000 && tick < 4050) {
            requested += 4;
            VolcanoDepositionSupply(model, HALF, HALF, 4, LAVA_SOURCE_K);
        }
        if (supply) {
            requested += supply;
            VolcanoDepositionSupply(model, x, z, supply, LAVA_SOURCE_K);
        }
        VolcanoDepositionStep(model);
    }
    VolcanoDepositionLedger ledger = VolcanoDepositionInspect(model);
    unsigned records = 0, hot = 0;
    for (int z = 0; z < SIDE; z++) for (int x = 0; x < SIDE; x++) {
        VolcanoDepositionCell c; VolcanoDepositionRead(model, x, z, &c);
        unsigned whole = c.temperatureK <= 293.00000001 ? (unsigned)floor(c.volume) : 0;
        unsigned units = (unsigned)floor((c.volume - whole) * LAVA_CAPACITY);
        unsigned layers = (units + LAVA_CAPACITY - 1) / LAVA_CAPACITY;
        records += layers; if (c.temperatureK > 293.00000001) hot += layers;
        int top = (int)c.ground + (int)whole + (int)layers;
        for (int y = base; y <= top; y++) {
            if (!SurfaceBlockReadyAt(x - HALF, y, z - HALF) ||
                GetBlockBaseAt(x - HALF, y, z - HALF) != BLOCK_AIR) {
                VolcanoDepositionDestroy(model);
                return Fail(game, "mountain_region_unloaded_or_occupied");
            }
        }
    }
    if (records > LAVA_MAX_RECORDS || hot > LAVA_MAX_HOT_CELLS) {
        VolcanoDepositionDestroy(model); return Fail(game, "mountain_overlay_capacity");
    }
    LavaContext context = {.valid = true, .bodyId = world.bodyId, .boundary = ImportBoundary};
    unsigned rock = 0, oldRock = 0; double represented = 0;
    bool ok = true;
    for (int z = 0; ok && z < SIDE; z++) for (int x = 0; ok && x < SIDE; x++) {
        VolcanoDepositionCell c; VolcanoDepositionRead(model, x, z, &c);
        unsigned whole = c.temperatureK <= 293.00000001 ? (unsigned)floor(c.volume) : 0;
        int y = base;
        for (; ok && y < (int)c.ground + (int)whole; y++) {
            ok = SetBlockNoUndoFromSource(x - HALF, y, z - HALF, BLOCK_BASALT, WORLD_MUTATION_ENVIRONMENT);
            rock++; if (y >= (int)c.ground) oldRock++;
        }
        unsigned remaining = (unsigned)floor((c.volume - whole) * LAVA_CAPACITY);
        represented += whole + (double)remaining / LAVA_CAPACITY;
        double specific = c.volume > 0 ? c.energy / c.volume : 0;
        while (ok && remaining) {
            unsigned units = remaining > LAVA_CAPACITY ? LAVA_CAPACITY : remaining;
            ok = LavaRuntimeImportThermal(&context, x - HALF, y++, z - HALF,
                (LavaThermalState){units, units * specific});
            remaining -= units;
        }
    }
    VolcanoDepositionDestroy(model);
    if (!ok) return Fail(game, "mountain_import_failed_partial_world");
    DebugControl *control = &game->debugControl;
    control->depositionReady = true;
    control->depositionSupplied = ledger.suppliedVolume; control->depositionRetained = ledger.retainedVolume;
    control->depositionLiquid = ledger.liquidVolume; control->depositionSolid = ledger.retainedVolume - ledger.liquidVolume;
    control->depositionVolumeError = ledger.volumeError; control->depositionEnergyError = ledger.energyError;
    control->depositionOutlet = ledger.boundaryVolume; control->depositionPool = 0;
    DebugControlReply(control,
        "DEBUG_CONTROL geology mountain center=0,%d,0 radius=27 rock_blocks=%u cooled_blocks=%u overlays=%u hot=%u requested=%.6f supplied=%.6f represented=%.6f quantization_loss=%.6f liquid=%.6f solid=%.6f boundary=%.6f volume_error=%.12g energy_error=%.12g\n",
        base, rock, oldRock, records, hot, requested, ledger.suppliedVolume, represented,
        ledger.retainedVolume - represented, ledger.liquidVolume, ledger.retainedVolume - ledger.liquidVolume,
        ledger.boundaryVolume, ledger.volumeError, ledger.energyError);
    return true;
}
