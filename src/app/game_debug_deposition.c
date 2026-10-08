#include "app/game_debug_deposition.h"
#include "world/volcano_deposition_model.h"
#include <string.h>

static void Snapshot(DebugControl *control, VolcanoDepositionModel *model, const char *name)
{
    VolcanoDepositionLedger l = VolcanoDepositionInspect(model);
    DebugControlReply(control,
        "DEBUG_CONTROL deposition case=%s step=%u supplied=%.12g retained=%.12g liquid=%.12g cooled_energy=%.12g volume_error=%.12g energy_error=%.12g\n",
        name, l.steps, l.suppliedVolume, l.retainedVolume, l.liquidVolume,
        l.cooledEnergy, l.volumeError, l.energyError);
    for (unsigned x = 0; x < 4; x++) {
        VolcanoDepositionCell c;
        if (!VolcanoDepositionRead(model, x, 0, &c)) continue;
        DebugControlReply(control,
            "DEBUG_CONTROL deposition_cell case=%s step=%u x=%u ground=%.12g volume=%.12g liquid=%.12g solid_depth=%.12g surface=%.12g temperature=%.12g\n",
            name, l.steps, x, c.ground, c.volume, c.liquidVolume, c.solidDepth, c.surface, c.temperatureK);
    }
}
bool GameDebugDepositionRun(DebugControl *control)
{
    static const char *names[] = {"slope", "basin", "overflow", "cooling"};
    control->depositionReady = false;
    if (control->depositionCase >= 4) return false;
    unsigned kind = control->depositionCase;
    const char *name = names[kind];
    bool basin = kind == 1 || kind == 2;
    double ground[4] = {6, 4, 2, 0};
    if (basin) { ground[0] = 10; ground[1] = 0; ground[2] = 1; ground[3] = -1; }
    VolcanoDepositionConfig config = {4, 1, 1100, 1, 100, 293, 1, kind == 3 ? 10000 : 0};
    VolcanoDepositionModel *model = VolcanoDepositionCreate(config, ground);
    if (!model) return false;
    double supplied = basin ? .5 : 1;
    bool ok = VolcanoDepositionSupply(model, basin ? 1 : 0, 0, supplied, 1450) == supplied;
    Snapshot(control, model, name);
    unsigned end = kind == 2 ? 1100 : kind == 1 ? 100 : 300;
    for (unsigned step = 1; ok && step <= end; step++) {
        if (kind == 2 && step == 101) {
            ok = VolcanoDepositionSupply(model, 1, 0, 4, 1450) == 4;
            DebugControlReply(control, "DEBUG_CONTROL deposition_supply case=overflow step=100 volume=4\n");
        }
        ok = ok && VolcanoDepositionStep(model);
        if (step == 1 || step == 10 || step == 100 || step == end) Snapshot(control, model, name);
    }
    VolcanoDepositionLedger l = VolcanoDepositionInspect(model);
    VolcanoDepositionCell outlet, pool;
    ok = ok && VolcanoDepositionRead(model, 3, 0, &outlet) && VolcanoDepositionRead(model, 1, 0, &pool);
    if (ok) {
        control->depositionReady = true;
        control->depositionSupplied = l.suppliedVolume; control->depositionRetained = l.retainedVolume;
        control->depositionLiquid = l.liquidVolume; control->depositionSolid = l.retainedVolume - l.liquidVolume;
        control->depositionVolumeError = l.volumeError; control->depositionEnergyError = l.energyError;
        control->depositionOutlet = outlet.volume; control->depositionPool = pool.volume;
    }
    VolcanoDepositionDestroy(model);
    return ok;
}
bool GameDebugDepositionResolve(const DebugControl *c, const char *field, DebugDslValue *out)
{
    if (!strcmp(field, "deposition_ready")) {
        out->type = DEBUG_DSL_VALUE_BOOL; out->as.boolean = c->depositionReady; return true;
    }
    out->type = DEBUG_DSL_VALUE_NUMBER;
    if (!strcmp(field, "deposition_supplied")) out->as.number = c->depositionSupplied;
    else if (!strcmp(field, "deposition_retained")) out->as.number = c->depositionRetained;
    else if (!strcmp(field, "deposition_liquid")) out->as.number = c->depositionLiquid;
    else if (!strcmp(field, "deposition_solid")) out->as.number = c->depositionSolid;
    else if (!strcmp(field, "deposition_volume_error")) out->as.number = c->depositionVolumeError;
    else if (!strcmp(field, "deposition_energy_error")) out->as.number = c->depositionEnergyError;
    else if (!strcmp(field, "deposition_outlet")) out->as.number = c->depositionOutlet;
    else if (!strcmp(field, "deposition_pool")) out->as.number = c->depositionPool;
    else return false;
    if (!c->depositionReady) out->as.number = 0;
    return true;
}
