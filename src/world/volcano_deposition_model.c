#include "world/volcano_deposition_model.h"
#include "world/lava_model.h"
#include <math.h>
#include <stdlib.h>

struct Column { double ground, volume, energy; };
struct VolcanoDepositionModel {
    VolcanoDepositionConfig config;
    unsigned count, steps;
    double area, capacity, suppliedVolume, suppliedEnergy, cooledEnergy;
    struct Column *columns;
    double *flux, *incoming;
};

static double Liquid(const struct Column *c)
{
    if (c->volume <= 0) return 0;
    double fraction = (c->energy / c->volume - (LAVA_FREEZING_K - LAVA_REFERENCE_K)) / LAVA_LATENT_K;
    return c->volume * fmin(1, fmax(0, fraction));
}
static double Temperature(const struct Column *c)
{
    /* Normalize to a one-unit thermal state: the existing phase relation is
       intensive, while coarse-column mass must not be truncated to a voxel. */
    return LavaTemperature((LavaThermalState){ c->volume > 0 ? 1u : 0u,
        c->volume > 0 ? c->energy / c->volume : 0 });
}
static double LiquidEnthalpy(const struct Column *c)
{
    return fmax(c->volume > 0 ? c->energy / c->volume : 0,
                LAVA_FREEZING_K - LAVA_REFERENCE_K + LAVA_LATENT_K);
}
static int Neighbor(const VolcanoDepositionModel *m, unsigned i, unsigned side)
{
    unsigned x = i % m->config.width, y = i / m->config.width;
    switch (side) {
    case 0: return x ? (int)i - 1 : -1;
    case 1: return x + 1 < m->config.width ? (int)i + 1 : -1;
    case 2: return y ? (int)(i - m->config.width) : -1;
    default: return y + 1 < m->config.height ? (int)(i + m->config.width) : -1;
    }
}
VolcanoDepositionModel *VolcanoDepositionCreate(VolcanoDepositionConfig c, const double *ground)
{
    if (!ground || !c.width || !c.height || c.width > VOLCANO_DEPOSITION_MAX_SIDE ||
        c.height > VOLCANO_DEPOSITION_MAX_SIDE || !c.maxSteps || c.maxSteps > VOLCANO_DEPOSITION_MAX_STEPS ||
        !isfinite(c.cellSize) || c.cellSize < 1 || c.cellSize > 64 ||
        !isfinite(c.maxDepth) || c.maxDepth <= 0 || c.maxDepth > 1024 ||
        !isfinite(c.ambientK) || c.ambientK < LAVA_REFERENCE_K || c.ambientK > 330 ||
        !isfinite(c.mobility) || c.mobility < 0 || c.mobility > 100 ||
        !isfinite(c.cooling) || c.cooling < 0 || c.cooling > 1000000) return NULL;
    unsigned count = c.width * c.height;
    for (unsigned i = 0; i < count; i++)
        if (!isfinite(ground[i]) || fabs(ground[i]) > 1000000) return NULL;
    VolcanoDepositionModel *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->columns = calloc(count, sizeof(*m->columns));
    m->flux = calloc(count * 4u, sizeof(*m->flux));
    m->incoming = calloc(count, sizeof(*m->incoming));
    if (!m->columns || !m->flux || !m->incoming) { VolcanoDepositionDestroy(m); return NULL; }
    m->config = c; m->count = count;
    m->area = c.cellSize * c.cellSize; m->capacity = m->area * c.maxDepth;
    for (unsigned i = 0; i < count; i++) m->columns[i].ground = ground[i];
    return m;
}
void VolcanoDepositionDestroy(VolcanoDepositionModel *m)
{
    if (!m) return;
    free(m->columns); free(m->flux); free(m->incoming); free(m);
}
double VolcanoDepositionSupply(VolcanoDepositionModel *m, unsigned x, unsigned y,
                                double volume, double temperatureK)
{
    if (!m || m->steps >= m->config.maxSteps || x >= m->config.width || y >= m->config.height ||
        !isfinite(volume) || volume <= 0 || !isfinite(temperatureK) ||
        temperatureK < m->config.ambientK || temperatureK > 3000) return 0;
    struct Column *c = &m->columns[y * m->config.width + x];
    double accepted = fmin(volume, fmax(0, m->capacity - c->volume));
    double energy = accepted * LavaSpecificEnthalpy(temperatureK);
    c->volume += accepted; c->energy += energy;
    m->suppliedVolume += accepted; m->suppliedEnergy += energy;
    return accepted;
}
static bool Step(VolcanoDepositionModel *m, bool reverse)
{
    if (!m || m->steps >= m->config.maxSteps) return false;
    /* Each donor writes its own slots. No neighbor changes during this pass. */
    for (unsigned n = 0; n < m->count; n++) {
        unsigned i = reverse ? m->count - 1 - n : n;
        struct Column c = m->columns[i];
        double liquid = Liquid(&c), sum = 0;
        double mobility = c.volume > 0 ? liquid / c.volume : 0;
        for (unsigned side = 0; side < 4; side++) {
            int j = Neighbor(m, i, side);
            double wanted = 0;
            if (j >= 0 && liquid > 0) {
                struct Column d = m->columns[j];
                double head = c.ground - d.ground + (c.volume - d.volume) / m->area;
                /* Pairwise equalization bound prevents large steps overshooting.
                   Donor-wide scaling below also handles competing outlets. */
                double rate = fmin(.125, m->config.mobility * mobility *
                    VOLCANO_DEPOSITION_STEP_SECONDS / m->config.cellSize);
                wanted = fmax(0, head) * m->area * rate;
            }
            m->flux[i * 4 + side] = wanted; sum += wanted;
        }
        double scale = sum > liquid && sum > 0 ? liquid / sum : 1;
        for (unsigned side = 0; side < 4; side++) m->flux[i * 4 + side] *= scale;
    }
    /* Canonical gathers make floating-point summation independent of the
       donor evaluation order. Reject capacity excess back to its source. */
    for (unsigned i = 0; i < m->count; i++) {
        double incoming = 0;
        for (unsigned side = 0; side < 4; side++) {
            int j = Neighbor(m, i, side);
            if (j >= 0) incoming += m->flux[(unsigned)j * 4 + (side ^ 1u)];
        }
        m->incoming[i] = incoming;
    }
    for (unsigned i = 0; i < m->count; i++) for (unsigned side = 0; side < 4; side++) {
        int j = Neighbor(m, i, side);
        if (j < 0) continue;
        double space = fmax(0, m->capacity - m->columns[j].volume);
        if (m->incoming[j] > space)
            m->flux[i * 4 + side] *= space / m->incoming[j];
    }
    /* Store transported energy in scratch before any column is modified. */
    for (unsigned i = 0; i < m->count; i++) m->incoming[i] = LiquidEnthalpy(&m->columns[i]);
    for (unsigned i = 0; i < m->count; i++) {
        struct Column *c = &m->columns[i];
        double dv = 0, de = 0;
        for (unsigned side = 0; side < 4; side++) {
            double outgoing = m->flux[i * 4 + side];
            dv -= outgoing; de -= outgoing * m->incoming[i];
            int j = Neighbor(m, i, side);
            if (j >= 0) {
                double incoming = m->flux[(unsigned)j * 4 + (side ^ 1u)];
                dv += incoming; de += incoming * m->incoming[j];
            }
        }
        c->volume += dv; c->energy += de;
        /* Only roundoff can cross zero. Conservation errors remain visible
           in the ledger rather than being hidden by balancing counters. */
        c->volume = fmax(0, c->volume); c->energy = fmax(0, c->energy);
        double floorEnergy = c->volume * (m->config.ambientK - LAVA_REFERENCE_K);
        /* Top-surface cooling: coarse cell area, independent of cell depth.
           Constant game-scale loss, not radiation or a conduction solver. */
        double loss = fmin(fmax(0, c->energy - floorEnergy),
            m->config.cooling * m->area * VOLCANO_DEPOSITION_STEP_SECONDS);
        c->energy -= loss; m->cooledEnergy += loss;
    }
    m->steps++;
    return true;
}
bool VolcanoDepositionStep(VolcanoDepositionModel *m) { return Step(m, false); }
#ifdef VOLCANO_DEPOSITION_TESTING
bool VolcanoDepositionStepReverse(VolcanoDepositionModel *m) { return Step(m, true); }
#endif
bool VolcanoDepositionRead(const VolcanoDepositionModel *m, unsigned x, unsigned y,
                            VolcanoDepositionCell *out)
{
    if (!m || !out || x >= m->config.width || y >= m->config.height) return false;
    struct Column c = m->columns[y * m->config.width + x];
    double liquid = Liquid(&c);
    *out = (VolcanoDepositionCell){ c.ground, c.volume, c.energy, liquid,
        (c.volume - liquid) / m->area, c.ground + c.volume / m->area, Temperature(&c) };
    return true;
}
VolcanoDepositionLedger VolcanoDepositionInspect(const VolcanoDepositionModel *m)
{
    VolcanoDepositionLedger l = {0};
    if (!m) return l;
    l.steps = m->steps; l.suppliedVolume = m->suppliedVolume;
    l.suppliedEnergy = m->suppliedEnergy; l.cooledEnergy = m->cooledEnergy;
    for (unsigned i = 0; i < m->count; i++) {
        struct Column c = m->columns[i];
        l.retainedVolume += c.volume; l.retainedEnergy += c.energy; l.liquidVolume += Liquid(&c);
        unsigned x = i % m->config.width, y = i / m->config.width;
        if (!x || !y || x + 1 == m->config.width || y + 1 == m->config.height)
            l.boundaryVolume += c.volume;
    }
    l.volumeError = l.suppliedVolume - l.retainedVolume;
    l.energyError = l.suppliedEnergy - l.retainedEnergy - l.cooledEnergy;
    return l;
}
