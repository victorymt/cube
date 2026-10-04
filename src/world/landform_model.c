#include "world/landform_model.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

#define N LANDFORM_RESOLUTION
#define FACE_CELLS (N * N)
#define STEP (SURFACE_FACE_BLOCKS / N)

static SurfaceAddress CellAddress(uint32_t id)
{
    return (SurfaceAddress){ .face = (SurfaceFace)(id / FACE_CELLS),
        .u = (int)(id % N) * STEP + STEP / 2,
        .v = (int)((id / N) % N) * STEP + STEP / 2 };
}

static uint32_t CellId(SurfaceAddress a)
{
    return (uint32_t)a.face * FACE_CELLS + (uint32_t)(a.v / STEP) * N +
           (uint32_t)(a.u / STEP);
}

uint32_t LandformCellAtDirection(Vector3 direction)
{
    return CellId(SurfaceAddressFromDirection(0, direction, 0));
}

Vector3 LandformCellDirection(uint32_t cell)
{
    return SurfaceAddressDirection(CellAddress(cell));
}

uint32_t LandformNeighbor(uint32_t cell, int edge)
{
    SurfaceAddress a = CellAddress(cell);
    int u = a.u / STEP, v = a.v / STEP;
    if (edge == 0 && u > 0) return cell - 1;
    if (edge == 1 && u < N - 1) return cell + 1;
    if (edge == 2 && v > 0) return cell - N;
    if (edge == 3 && v < N - 1) return cell + N;
    // Cross the shared edge by one fine cell, preserving its along-edge
    // coordinate. Extrapolating a whole coarse cell distorts corner adjacency.
    if (edge < 2) a.u = edge == 0 ? 0 : SURFACE_FACE_BLOCKS - 1;
    else a.v = edge == 2 ? 0 : SURFACE_FACE_BLOCKS - 1;
    return CellId(SurfaceAddressOffset(a, edge == 0 ? -1 : edge == 1 ? 1 : 0,
                                      edge == 2 ? -1 : edge == 3 ? 1 : 0, 0));
}

static double SolidAngle(double s, double t)
{
    return atan2(s * t, sqrt(1.0 + s * s + t * t));
}

float LandformCellArea(uint32_t cell)
{
    double s = 2.0 * (cell % N) / N - 1.0;
    double t = 2.0 * ((cell / N) % N) / N - 1.0;
    double d = 2.0 / N;
    double angle = SolidAngle(s + d, t + d) - SolidAngle(s, t + d) -
                   SolidAngle(s + d, t) + SolidAngle(s, t);
    return (float)(angle * SURFACE_RADIUS_BLOCKS * SURFACE_RADIUS_BLOCKS);
}

static bool Before(const LandformCell *cells, uint32_t a, uint32_t b)
{
    return cells[a].spill < cells[b].spill ||
           (cells[a].spill == cells[b].spill && a < b);
}

static void Push(uint32_t *heap, uint32_t *count, const LandformCell *cells, uint32_t id)
{
    uint32_t i = (*count)++;
    while (i && Before(cells, id, heap[(i - 1) / 2])) {
        heap[i] = heap[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    heap[i] = id;
}

static uint32_t Pop(uint32_t *heap, uint32_t *count, const LandformCell *cells)
{
    uint32_t result = heap[0], last = heap[--*count], i = 0;
    while (i * 2 + 1 < *count) {
        uint32_t child = i * 2 + 1;
        if (child + 1 < *count && Before(cells, heap[child + 1], heap[child])) child++;
        if (!Before(cells, heap[child], last)) break;
        heap[i] = heap[child];
        i = child;
    }
    heap[i] = last;
    return result;
}

void LandformDestroy(LandformAtlas *atlas)
{
    if (!atlas) return;
    free(atlas->cells);
    free(atlas);
}

LandformAtlas *LandformBuild(float seaLevel, LandformBaselineFn baseline, void *user)
{
    if (!baseline || !isfinite(seaLevel)) return NULL;
    LandformAtlas *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->cells = calloc(LANDFORM_CELL_COUNT, sizeof(*a->cells));
    uint32_t *heap = malloc(LANDFORM_CELL_COUNT * sizeof(*heap));
    uint32_t *order = malloc(LANDFORM_CELL_COUNT * sizeof(*order));
    if (!a->cells || !heap || !order) {
        free(heap); free(order); LandformDestroy(a); return NULL;
    }
    a->seaLevel = seaLevel;
    a->bytes = sizeof(*a) + LANDFORM_CELL_COUNT * sizeof(*a->cells);
    uint32_t count = 0, lowest = 0;
    for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i++) {
        LandformCell *c = &a->cells[i];
        c->direction = LandformCellDirection(i);
        LandformBaseline b = baseline(c->direction, user);
        if (!isfinite(b.elevation) || !isfinite(b.runoff) || !isfinite(b.hardness)) {
            free(heap); free(order); LandformDestroy(a); return NULL;
        }
        c->elevation = b.elevation;
        c->spill = INFINITY;
        c->runoff = Clamp(b.runoff, 0, 1);
        c->hardness = Clamp(b.hardness, 0, 1);
        c->localArea = c->area = LandformCellArea(i);
        c->discharge = c->area * c->runoff;
        c->downstream = LANDFORM_NONE;
        c->watershed = i;
        if (b.elevation < a->cells[lowest].elevation) lowest = i;
        if (b.elevation <= seaLevel) {
            c->spill = seaLevel;
            Push(heap, &count, a->cells, i);
        }
    }
    for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i++) {
        LandformCell *c = &a->cells[i];
        if (c->runoff >= .18f || c->elevation <= seaLevel) continue;
        bool minimum = true;
        for (int e = 0; e < 4; e++) {
            const LandformCell *n = &a->cells[LandformNeighbor(i, e)];
            if (n->elevation <= c->elevation + 2) minimum = false;
        }
        if (!minimum) continue;
        c->spill = c->elevation + c->runoff * 5;
        Push(heap, &count, a->cells, i);
        a->closedBasins++;
    }
    if (count == 0) {
        a->closedBasins = 1;
        a->cells[lowest].spill = a->cells[lowest].elevation;
        Push(heap, &count, a->cells, lowest);
    }
    uint32_t visited = 0;
    while (count) {
        uint32_t id = Pop(heap, &count, a->cells);
        LandformCell *c = &a->cells[id];
        c->order = visited;
        order[visited++] = id;
        for (int edge = 0; edge < 4; edge++) {
            uint32_t other = LandformNeighbor(id, edge);
            LandformCell *n = &a->cells[other];
            if (isfinite(n->spill)) continue;
            n->spill = fmaxf(n->elevation, c->spill);
            n->downstream = id;
            n->watershed = c->watershed;
            Push(heap, &count, a->cells, other);
        }
    }
    // Improve descending routes without changing the acyclic flood order.
    for (uint32_t k = 0; k < visited; k++) {
        LandformCell *c = &a->cells[order[k]];
        if (c->downstream == LANDFORM_NONE) continue;
        float best = -1;
        for (int e = 0; e < 4; e++) {
            uint32_t id = LandformNeighbor(order[k], e);
            const LandformCell *n = &a->cells[id];
            if (n->order >= c->order || n->spill > c->spill) continue;
            float distance = Vector3Distance(c->direction, n->direction) * SURFACE_RADIUS_BLOCKS;
            float slope = (c->spill - n->spill) / distance;
            if (slope > best || (slope == best && id < c->downstream)) {
                c->downstream = id; best = slope;
            }
        }
        c->watershed = a->cells[c->downstream].watershed;
        c->slope = fmaxf(0, best);
    }
    for (uint32_t k = visited; k-- > 0;) {
        LandformCell *c = &a->cells[order[k]];
        if (c->downstream != LANDFORM_NONE) {
            a->cells[c->downstream].area += c->area;
            a->cells[c->downstream].discharge += c->discharge;
        }
    }
    for (uint32_t i = 0; i < LANDFORM_CELL_COUNT; i++) {
        LandformCell *c = &a->cells[i];
        if (c->elevation > seaLevel && c->spill - c->elevation > 1.5f) a->lakes++;
        if (c->elevation > seaLevel && c->downstream != LANDFORM_NONE && c->area > 14000) {
            c->width = Clamp(1.6f + sqrtf(c->area) * 0.014f, 2, 12);
            c->width *= 1.0f - 0.25f * c->hardness;
            a->rivers++;
        }
    }
    free(heap); free(order);
    return a;
}

const char *LandformKindName(LandformKind kind)
{
    static const char *names[] = { "upland", "river", "dry_channel", "lake",
        "wetland", "floodplain", "fan" };
    return (unsigned)kind <= LANDFORM_FAN ? names[kind] : "unknown";
}

static float SmoothWeight(float value)
{
    value = Clamp(value, 0, 1);
    return value * value * (3 - 2 * value);
}

static float ChannelDepth(const LandformCell *cell)
{
    return Clamp(1.5f + sqrtf(cell->area) * .003f, 2, 4);
}

// Static geological deposits. Candidates must have sediment supply, a steep
// confined approach and a flatter receiving reach in the same drainage basin.
static void SampleFan(const LandformAtlas *a, uint32_t id, Vector3 p,
                      float base, uint32_t watershed, LandformSample *deposit)
{
    const LandformCell *c = &a->cells[id];
    if (c->width == 0 || c->downstream == LANDFORM_NONE || c->slope < .06f ||
        c->discharge < c->localArea * .1f) return;
    const LandformCell *d = &a->cells[c->downstream];
    if (d->downstream == LANDFORM_NONE || d->slope > .035f || d->slope > c->slope * .4f ||
        d->spill - d->elevation > 1.5f || d->elevation <= a->seaLevel ||
        watershed != d->watershed) return;
    unsigned banks = 0;
    for (int e = 0; e < 4; e++)
        banks += a->cells[LandformNeighbor(id, e)].elevation > c->elevation + 2;
    if (banks < 2) return;
    Vector3 axis = Vector3Normalize(Vector3Subtract(d->direction,
        Vector3Scale(c->direction, Vector3DotProduct(c->direction, d->direction))));
    // Reproject the incoming direction onto the tangent plane at the outlet.
    axis = Vector3Normalize(Vector3Subtract(axis,
        Vector3Scale(d->direction, Vector3DotProduct(axis, d->direction))));
    Vector3 side = Vector3Normalize(Vector3CrossProduct(d->direction, axis));
    Vector3 offset = Vector3Scale(Vector3Subtract(p, d->direction), SURFACE_RADIUS_BLOCKS);
    float along = Vector3DotProduct(offset, axis);
    float length = fminf(sqrtf(d->localArea) * 1.8f, Clamp(sqrtf(c->area) * .12f, 12, 80));
    if (along <= 0 || along >= length) return;
    float t = along / length;
    float halfWidth = c->width * 1.5f + along * .55f;
    float across = fabsf(Vector3DotProduct(offset, side)) / halfWidth;
    if (across >= 1) return;
    float plane = d->spill - d->slope * along;
    // Clip against ridges and low basins; never bridge valleys with a fan.
    if (base > plane + 2 || base < plane - 5) return;
    float weight = SmoothWeight(t * 5) * SmoothWeight((1 - t) * 2) * SmoothWeight(1 - across);
    float thickness = (1.0f + (1 - c->hardness) * 3) * weight;
    if (thickness < .05f || thickness <= deposit->sedimentDepth) return;
    *deposit = (LandformSample){ .elevation = base + thickness, .kind = LANDFORM_FAN,
        .watershed = d->watershed, .reach = id, .area = c->area, .slope = d->slope,
        .channelDistance = 1e6f, .sedimentDepth = thickness, .wetness = .2f,
        .gravel = Lerp(.7f, .15f, t), .sand = Lerp(.25f, .45f, t),
        .fines = Lerp(.05f, .4f, t) };
}

LandformSample LandformSampleAt(const LandformAtlas *a, Vector3 p, float base)
{
    LandformSample s = { .elevation = base, .channelDistance = 1e6f,
        .reach = LANDFORM_NONE, .watershed = LANDFORM_NONE };
    if (!a || !a->cells) return s;
    uint32_t center = LandformCellAtDirection(p);
    const LandformCell *home = &a->cells[center];
    s.watershed = home->watershed;
    s.area = home->area;
    s.slope = home->slope;
    if (base < a->seaLevel) return s;
    SurfaceAddress anchor = CellAddress(center);
    float nearest = 1e6f;
    float channelBed = INFINITY;
    const LandformCell *channel = NULL;
    float channelLevel = 0;
    LandformSample deposit = s;
    float lakeWeight = 0, lakeTotal = 0, lakeLevel = 1e6f;
    for (int v = -3; v <= 3; v++) for (int u = -3; u <= 3; u++) {
        int localU = anchor.u / STEP + u, localV = anchor.v / STEP + v;
        uint32_t id = localU >= 0 && localU < N && localV >= 0 && localV < N
            ? (uint32_t)anchor.face * FACE_CELLS + (uint32_t)localV * N + (uint32_t)localU
            : CellId(SurfaceAddressOffset(anchor, u * STEP, v * STEP, 0));
        const LandformCell *c = &a->cells[id];
        SampleFan(a, id, p, base, home->watershed, &deposit);
        float pointDistance = Vector3Distance(p, c->direction) * SURFACE_RADIUS_BLOCKS;
        float radius = sqrtf(c->localArea) * 1.5f;
        float w = fmaxf(0, 1.0f - pointDistance / radius);
        w *= w;
        lakeTotal += w;
        if (c->spill - c->elevation > 1.5f && c->elevation > a->seaLevel) {
            lakeWeight += w;
            if (w > .02f) lakeLevel = fminf(lakeLevel, c->spill);
        }
        if (c->width == 0 || c->downstream == LANDFORM_NONE) continue;
        const LandformCell *d = &a->cells[c->downstream];
        Vector3 edge = Vector3Subtract(d->direction, c->direction);
        Vector3 offset = Vector3Subtract(p, c->direction);
        float t = Clamp(Vector3DotProduct(offset, edge) / Vector3LengthSqr(edge), 0, 1);
        Vector3 onLine = Vector3Normalize(Vector3Add(c->direction, Vector3Scale(edge, t)));
        float distance = Vector3Distance(p, onLine) * SURFACE_RADIUS_BLOCKS;
        float width = Lerp(c->width, fmaxf(c->width, d->width), t);
        float normalized = distance / width;
        // A shared confluence vertex has one spill height. All incident
        // reaches interpolate that same value, independent of chunk order.
        float level = Lerp(c->spill, d->spill, t);
        float bed = level - Lerp(ChannelDepth(c), ChannelDepth(d), t);
        if (normalized < 3.0f) {
            float bank = Clamp((normalized - 1.0f) / 2.0f, 0, 1);
            float elevation = Lerp(bed, fmaxf(base, bed), bank * bank * (3 - 2 * bank));
            if (normalized <= 1) channelBed = fminf(channelBed, bed);
            else if (elevation < s.elevation) s.elevation = elevation;
        }
        if (normalized > 1 && normalized < 5 && c->slope < .025f &&
            home->watershed == c->watershed && base >= level && base < level + 4) {
            float weight = SmoothWeight((5 - normalized) / 4) * SmoothWeight((level + 4 - base) / 4);
            float thickness = 1.5f * weight;
            if (thickness > deposit.sedimentDepth) {
                deposit = (LandformSample){ .elevation = base + thickness,
                    .kind = LANDFORM_FLOODPLAIN, .watershed = c->watershed, .reach = id,
                    .area = c->area, .slope = c->slope, .channelDistance = distance,
                    .sedimentDepth = thickness, .wetness = .65f * weight,
                    .gravel = .1f * (1 - weight), .sand = .3f, .fines = .6f + .1f * weight };
            }
        }
        if (normalized < nearest) {
            nearest = normalized;
            channel = c;
            channelLevel = level;
            s.channelDistance = distance;
            s.reach = id; s.watershed = c->watershed;
            s.area = c->area; s.slope = c->slope;
        }
    }
    if (isfinite(channelBed)) s.elevation = fminf(base, channelBed);
    bool perennial = channel && channel->discharge / channel->area >= .18f;
    // The bank is carved out to three channel widths. Fill its submerged
    // columns to the same level as the core, using the voxel height rules
    // from terrain generation so water ends against solid ground, not air.
    bool wetBank = perennial && nearest < 3 &&
        (int)floorf(channelLevel) > (int)lroundf(s.elevation);
    if (nearest <= 1 || wetBank) {
        s.kind = perennial ? LANDFORM_RIVER : LANDFORM_DRY_CHANNEL;
        s.hasWater = perennial;
        s.waterLevel = channelLevel;
        s.wetness = s.hasWater ? 1 : .15f;
        s.gravel = .7f; s.sand = .25f; s.fines = .05f;
        s.sedimentDepth = 3;
    }
    if (deposit.kind != LANDFORM_UPLAND && !isfinite(channelBed) && !wetBank) {
        // Carved banks take precedence over deposition so the outlet stays open.
        float carved = s.elevation;
        s = deposit;
        if (carved < base) s.elevation = carved;
    }
    if (lakeTotal > 0 && lakeWeight / lakeTotal > .45f && base < lakeLevel &&
        (!s.hasWater || lakeLevel <= s.waterLevel)) {
        s.kind = LANDFORM_LAKE;
        s.hasWater = home->runoff > .18f;
        s.waterLevel = lakeLevel;
        s.wetness = s.hasWater ? 1 : .2f;
        s.gravel = .05f; s.sand = .15f; s.fines = .8f;
        s.sedimentDepth = 4;
    }
    if (s.hasWater && (int)floorf(s.waterLevel) <= (int)lroundf(s.elevation))
        s.hasWater = false;
    if (s.hasWater && s.kind == LANDFORM_LAKE && s.waterLevel - s.elevation < 2.5f) {
        s.kind = LANDFORM_WETLAND;
    }
    return s;
}
