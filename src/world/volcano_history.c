#include "world/volcano_history.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

struct VolcanoHistory {
    VolcanoTerrainDefinition definition;
    VolcanoHistoryConfig config;
    VolcanoHistoryStats stats;
    Vector3 up, east, north;
    VolcanoHistoryEvent events[VOLCANO_HISTORY_MAX_EVENTS];
    double *ground;
    VolcanoHistoryCell *cells;
};
static uint32_t Mix(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
VolcanoHistoryConfig VolcanoHistoryDefaults(void)
{
    return (VolcanoHistoryConfig){4, 3, 400, 800, 1, 6, 10, 32};
}
static bool Valid(const VolcanoTerrainDefinition *d, VolcanoHistoryConfig c)
{
    if (!d || !SurfaceAddressIsValid(d->center) ||
        !isfinite(d->radius) || d->radius < 16 || d->radius > 180 ||
        !isfinite(d->height) || d->height <= 0 || d->height > 128 ||
        !isfinite(d->craterRadius) || d->craterRadius <= 0 || d->craterRadius >= d->radius ||
        !isfinite(d->craterDepth) || d->craterDepth < 0 || d->craterDepth >= d->height) return false;
    return (c.cellSize == 2 || c.cellSize == 4 || c.cellSize == 8) &&
        c.events > 0 && c.events <= VOLCANO_HISTORY_MAX_EVENTS &&
        c.supplySteps > 0 && c.supplySteps <= 4000 && c.settleSteps <= 4000 &&
        isfinite(c.volumeScale) && c.volumeScale > 0 && c.volumeScale <= 8 &&
        isfinite(c.cooling) && c.cooling >= 0 && c.cooling <= 1000 &&
        isfinite(c.mobility) && c.mobility > 0 && c.mobility <= 100 &&
        isfinite(c.maxDepth) && c.maxDepth > 0 && c.maxDepth <= 128;
}
Vector3 VolcanoHistoryDirection(const VolcanoHistory *h, unsigned x, unsigned y)
{
    if (!h || x >= h->stats.side || y >= h->stats.side) return (Vector3){0};
    float u = ((int)x - (int)(h->stats.side / 2)) * (float)h->config.cellSize;
    float v = ((int)y - (int)(h->stats.side / 2)) * (float)h->config.cellSize;
    return Vector3Normalize(Vector3Add(h->up, Vector3Add(
        Vector3Scale(h->east, u / SURFACE_RADIUS_BLOCKS),
        Vector3Scale(h->north, v / SURFACE_RADIUS_BLOCKS))));
}
VolcanoHistory *VolcanoHistoryBuild(const VolcanoTerrainDefinition *d,
    VolcanoHistoryConfig c, VolcanoHistoryGround ground, void *user)
{
    if (!Valid(d, c)) return NULL;
    /* Square covers the complete warped foot plus 80 blocks of apron. */
    unsigned half = (unsigned)ceil((d->radius * 1.12 + 80) / c.cellSize);
    unsigned side = half * 2 + 1, count = side * side;
    if (side > VOLCANO_DEPOSITION_MAX_SIDE) return NULL;
    VolcanoHistory *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->definition = *d; h->config = c;
    h->stats = (VolcanoHistoryStats){.side = side, .events = c.events,
        .cellSize = c.cellSize, .extent = half * c.cellSize};
    h->up = SurfaceAddressDirection(d->center);
    h->east = Vector3Normalize(Vector3CrossProduct(
        fabsf(h->up.y) < .95f ? (Vector3){0,1,0} : (Vector3){1,0,0}, h->up));
    h->north = Vector3CrossProduct(h->up, h->east);
    h->ground = calloc(count, sizeof(double));
    h->cells = calloc(count * c.events, sizeof(*h->cells));
    if (!h->ground || !h->cells) {
        VolcanoHistoryDestroy(h); return NULL;
    }
    for (unsigned y = 0; y < side; y++) for (unsigned x = 0; x < side; x++) {
        Vector3 direction = VolcanoHistoryDirection(h, x, y);
        h->ground[y * side + x] = ground ? ground(direction, user) : VolcanoTerrainShape(d, direction).height;
    }
    VolcanoDepositionConfig dc = {side, side, c.supplySteps + c.settleSteps,
        c.cellSize, c.maxDepth, 293, c.mobility, c.cooling};
    /* Upper bound including the deposition model's columns and work arrays. */
    h->stats.peakBytes = sizeof(*h) + (size_t)count *
        (sizeof(double) + c.events * sizeof(VolcanoHistoryCell) + 9 * sizeof(double)) + 512;
    LavaRock rock = (d->seed & 1u) ? LAVA_ROCK_RHYOLITE : LAVA_ROCK_BASALT;
    if (d->seed & 2u) rock = (d->seed & 1u) ? LAVA_ROCK_PUMICE : LAVA_ROCK_SCORIA;
    for (unsigned event = 0; event < c.events; event++) {
        /* Geological quiet time leaves a cold substrate, not a homogeneous
           hot/cold soup. Each new flow runs ON the previous solid surface.
           This avoids instant quenching by the entire old column and retains
           old deposits as terrain barriers rather than erasing them. */
        VolcanoDepositionModel *m = VolcanoDepositionCreate(dc, h->ground);
        if (!m) { VolcanoHistoryDestroy(h); return NULL; }
        VolcanoHistoryEvent *e = &h->events[event];
        uint32_t key = Mix(d->seed ^ (event + 1) * 0x9e3779b9u);
        *e = (VolcanoHistoryEvent){.sourceX = half, .sourceY = half,
            .requestedVolume = (12000 + key % 8192) * c.volumeScale,
            .temperatureK = 1425 + (key >> 16) % 101};
        h->stats.requestedVolume += e->requestedVolume;
        for (unsigned step = 0; step < c.supplySteps + c.settleSteps; step++) {
            if (step < c.supplySteps) {
                /* Retry rejected inventory while this eruption is supplying. */
                double due = e->requestedVolume * (step + 1) / c.supplySteps - e->acceptedVolume;
                e->acceptedVolume += VolcanoDepositionSupply(m, half, half, due, e->temperatureK);
            }
            if (!VolcanoDepositionStep(m)) {
                VolcanoDepositionDestroy(m); VolcanoHistoryDestroy(h); return NULL;
            }
        }
        e->terminalCooling = VolcanoDepositionCoolToAmbient(m);
        h->stats.terminalCooling += e->terminalCooling;
        h->stats.remainingVolume += e->requestedVolume - e->acceptedVolume;
        VolcanoDepositionLedger l = VolcanoDepositionInspect(m);
        h->stats.ledger.steps += l.steps;
        h->stats.ledger.suppliedVolume += l.suppliedVolume;
        h->stats.ledger.suppliedEnergy += l.suppliedEnergy;
        h->stats.ledger.cooledEnergy += l.cooledEnergy;
        h->stats.ledger.retainedVolume += l.retainedVolume;
        h->stats.ledger.retainedEnergy += l.retainedEnergy;
        h->stats.ledger.liquidVolume += l.liquidVolume;
        h->stats.ledger.boundaryVolume += l.boundaryVolume;
        h->stats.ledger.volumeError = h->stats.ledger.suppliedVolume - h->stats.ledger.retainedVolume;
        h->stats.ledger.energyError = h->stats.ledger.suppliedEnergy - h->stats.ledger.retainedEnergy - h->stats.ledger.cooledEnergy;
        e->ledger = h->stats.ledger;
        for (unsigned y = 0; y < side; y++) for (unsigned x = 0; x < side; x++) {
            unsigned i = y * side + x;
            VolcanoDepositionCell cell;
            VolcanoDepositionRead(m, x, y, &cell);
            VolcanoHistoryCell prior = event ? h->cells[(event - 1) * count + i] :
                (VolcanoHistoryCell){.ground = cell.ground};
            unsigned mask = prior.flowMask;
            if (cell.transportedVolume > 1e-8 ||
                (x == half && y == half && e->acceptedVolume > 0)) mask |= 1u << event;
            h->cells[event * count + i] = (VolcanoHistoryCell){prior.ground,
                prior.depth + cell.solidDepth, mask, rock};
            h->ground[i] = cell.surface;
        }
        VolcanoDepositionDestroy(m);
    }
    double area = c.cellSize * c.cellSize;
    for (unsigned y = 0; y < side; y++) for (unsigned x = 0; x < side; x++) {
        VolcanoHistoryCell cell = h->cells[(c.events - 1) * count + y * side + x];
        double radius = hypot((double)x - half, (double)y - half) * c.cellSize;
        if (radius > d->craterRadius * 1.5) h->stats.flankVolume += cell.depth * area;
        if (radius > d->radius * .75) h->stats.footVolume += cell.depth * area;
        h->stats.maxDepth = fmax(h->stats.maxDepth, cell.depth);
        if (cell.depth > .01) h->stats.flowRadius = fmax(h->stats.flowRadius, radius);
    }
    return h;
}
void VolcanoHistoryDestroy(VolcanoHistory *h)
{
    if (!h) return;
    free(h->ground); free(h->cells); free(h);
}
VolcanoHistoryStats VolcanoHistoryInspect(const VolcanoHistory *h)
{
    return h ? h->stats : (VolcanoHistoryStats){0};
}
bool VolcanoHistoryRead(const VolcanoHistory *h, unsigned event, unsigned x, unsigned y,
    VolcanoHistoryCell *out)
{
    if (!h || !out || event >= h->config.events || x >= h->stats.side || y >= h->stats.side) return false;
    *out = h->cells[(event * h->stats.side + y) * h->stats.side + x]; return true;
}
bool VolcanoHistoryEventAt(const VolcanoHistory *h, unsigned event, VolcanoHistoryEvent *out)
{
    if (!h || !out || event >= h->config.events) return false;
    *out = h->events[event]; return true;
}
