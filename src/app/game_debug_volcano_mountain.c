#include "app/game_debug_volcano_mountain.h"
#include "app/game_internal.h"
#include "world/volcano_deposition_model.h"
#include "world/lava_runtime.h"
#include "world/ash_runtime.h"
#include "world/world.h"
#include "world/world_environment.h"
#include <math.h>

/* A one-block grid covers the western flank, independently of the full
   procedural mountain. Keep the existing runtime budgets unchanged. */
#define SIDE 128
#define MID (SIDE / 2)

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
    DebugControl *control = &game->debugControl;
    if (!world.active || world.kind != WORLD_SURFACE_PLANET || WorldNetherIsActive() ||
        WorldGetEditCount() || LavaRuntimeStats().records || AshRuntimeStats().cells ||
        !control->geologyTerrainFound || control->geologyTerrainBody != world.bodyId ||
        control->geologyTerrainSeed != world.seed)
        return Fail(game, "mountain_requires_unedited_planet_and_geology_find");
    /* This fixed showcase targets the natural desert/1234 mountain selected
       by its scene. Other sizes need their own bounded source/domain design. */
    if (control->geologyTerrainRadius < 110 || control->geologyTerrainRadius > 115)
        return Fail(game, "mountain_fixture_requires_radius_110_to_115");
    int centerX = control->geologyTerrainX - world.originX;
    int centerZ = control->geologyTerrainZ - world.originZ;
    int originX = centerX - 126, originZ = centerZ - MID;
    double ground[SIDE * SIDE];
    for (int z = 0; z < SIDE; z++) for (int x = 0; x < SIDE; x++)
        ground[z * SIDE + x] = WorldSurfaceHeightAt(originX + x, originZ + z) + 1;
    VolcanoDepositionConfig config = {SIDE, SIDE, 5000, 1, 24, 293, 8, 12};
    VolcanoDepositionModel *model = VolcanoDepositionCreate(config, ground);
    if (!model) return Fail(game, "mountain_model_allocation_failed");
    /* Two explicit upper-flank sources, 30 blocks west of the crater center.
       Older material cools before the second episode enters the same slope. */
    double requested = 0;
    for (unsigned tick = 0; tick < 5000; tick++) {
        unsigned z = MID + 12; double supply = 0;
        if (tick < 240) supply = 4;
        if (tick >= 4100 && tick < 4700) { z = MID - 12; supply = 2; }
        if (supply) {
            requested += supply;
            VolcanoDepositionSupply(model, 96, z, supply, LAVA_SOURCE_K);
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
        for (unsigned j = 0; j < whole + layers; j++) {
            int y = (int)c.ground + (int)j;
            if (!SurfaceBlockReadyAt(originX + x, y, originZ + z) ||
                GetBlockBaseAt(originX + x, y, originZ + z) != BLOCK_AIR) {
                VolcanoDepositionDestroy(model);
                return Fail(game, "mountain_deposit_region_unloaded_or_occupied");
            }
        }
    }
    if (records > LAVA_MAX_RECORDS || hot > LAVA_MAX_HOT_CELLS || ledger.boundaryVolume > .001) {
        DebugControlReply(control, "DEBUG_CONTROL geology mountain rejected overlays=%u hot=%u boundary=%.6f\n",
            records, hot, ledger.boundaryVolume);
        VolcanoDepositionDestroy(model); return Fail(game, "mountain_fixture_budget_or_boundary");
    }
    LavaContext context = {.valid = true, .bodyId = world.bodyId, .boundary = ImportBoundary};
    unsigned rock = 0; double represented = 0;
    bool ok = true;
    for (int z = 0; ok && z < SIDE; z++) for (int x = 0; ok && x < SIDE; x++) {
        VolcanoDepositionCell c; VolcanoDepositionRead(model, x, z, &c);
        unsigned whole = c.temperatureK <= 293.00000001 ? (unsigned)floor(c.volume) : 0;
        int y = (int)c.ground;
        for (unsigned j = 0; ok && j < whole; j++) {
            ok = SetBlockNoUndoFromSource(originX + x, y++, originZ + z, BLOCK_BASALT, WORLD_MUTATION_ENVIRONMENT);
            rock++;
        }
        unsigned remaining = (unsigned)floor((c.volume - whole) * LAVA_CAPACITY);
        represented += whole + (double)remaining / LAVA_CAPACITY;
        double specific = c.volume > 0 ? c.energy / c.volume : 0;
        while (ok && remaining) {
            unsigned units = remaining > LAVA_CAPACITY ? LAVA_CAPACITY : remaining;
            ok = LavaRuntimeImportThermal(&context, originX + x + world.originX, y++, originZ + z + world.originZ,
                (LavaThermalState){units, units * specific});
            remaining -= units;
        }
    }
    VolcanoDepositionDestroy(model);
    if (!ok) return Fail(game, "mountain_import_failed_partial_world");
    control->depositionReady = true;
    control->depositionSupplied = ledger.suppliedVolume; control->depositionRetained = ledger.retainedVolume;
    control->depositionLiquid = ledger.liquidVolume; control->depositionSolid = ledger.retainedVolume - ledger.liquidVolume;
    control->depositionVolumeError = ledger.volumeError; control->depositionEnergyError = ledger.energyError;
    control->depositionOutlet = ledger.boundaryVolume; control->depositionPool = 0;
    DebugControlReply(control,
        "DEBUG_CONTROL geology mountain center=%d,%d,%d radius=%.3f height=%.3f cooled_blocks=%u overlays=%u hot=%u requested=%.6f supplied=%.6f represented=%.6f quantization_loss=%.6f liquid=%.6f solid=%.6f boundary=%.6f volume_error=%.12g energy_error=%.12g\n",
        centerX, control->geologyTerrainY, centerZ, control->geologyTerrainRadius, control->geologyTerrainHeight,
        rock, records, hot, requested, ledger.suppliedVolume, represented,
        ledger.retainedVolume - represented, ledger.liquidVolume, ledger.retainedVolume - ledger.liquidVolume,
        ledger.boundaryVolume, ledger.volumeError, ledger.energyError);
    return true;
}
