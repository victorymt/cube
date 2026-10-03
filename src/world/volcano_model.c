#include "world/volcano_model.h"

#include <math.h>
#include <string.h>

uint64_t VolcanoId(SurfaceAddress address)
{
    if (!SurfaceAddressIsValid(address)) return 0u;
    return ((uint64_t)address.bodyId << 32) |
           ((uint64_t)VOLCANO_GENERATOR_VERSION << 27) |
           ((uint64_t)address.face << 24) |
           ((uint64_t)address.u << 12) | (uint64_t)address.v;
}

VolcanoDefinition VolcanoDefine(uint32_t bodyId, uint32_t seed,
                                int mapX, int height, int mapZ,
                                float potential, bool forced)
{
    SurfaceMapCell cell = SurfaceCanonicalMapCell((float)mapX, (float)mapZ);
    SurfaceAddress address = SurfaceAddressFromMapCoordinates(
        bodyId, (float)cell.x, (float)cell.z, height);
    return (VolcanoDefinition){
        .id = VolcanoId(address), .seed = seed, .address = address,
        .mapX = cell.x, .mapZ = cell.z, .potential = potential, .forced = forced
    };
}

SurfaceMapCell VolcanoMapCell(SurfaceAddress address)
{
    SurfaceMapCell cell;
    if (SurfaceAddressCanonicalMapCell(address, &cell)) return cell;
    /* Cube-face cells need not have an exact integer lat/lon representative.
       Keep the candidate identity and place it at the nearest map column. */
    float longitude, latitude;
    SurfaceAddressLatLon(address, &longitude, &latitude);
    return SurfaceCanonicalMapCell(
        roundf(longitude * ((float)SURFACE_EQUATOR_BLOCKS / (2.0f * PI))),
        roundf(latitude * ((float)SURFACE_POLE_TO_POLE_BLOCKS / PI)));
}

static SurfaceAddress CandidateCenter(SurfaceAddress address)
{
    address.u = address.u / VOLCANO_CANDIDATE_SPACING * VOLCANO_CANDIDATE_SPACING +
                VOLCANO_CANDIDATE_SPACING / 2;
    address.v = address.v / VOLCANO_CANDIDATE_SPACING * VOLCANO_CANDIDATE_SPACING +
                VOLCANO_CANDIDATE_SPACING / 2;
    address.radial = 0;
    return address;
}

bool VolcanoCandidateAt(SurfaceAddress address, uint32_t seed,
                         VolcanoSiteSample sample, void *context,
                         VolcanoDefinition *out)
{
    if (!out || !sample || !SurfaceAddressIsValid(address)) return false;
    address = CandidateCenter(address);
    float potential = 0.0f;
    int height = 0;
    if (!sample(address, &potential, &height, context) ||
        !isfinite(potential) || potential < 0.10f || potential > 1.0f) return false;
    uint64_t id = VolcanoId(address);
    for (int v = -1; v <= 1; v++) {
        for (int u = -1; u <= 1; u++) {
            if (u == 0 && v == 0) continue;
            SurfaceAddress neighbor = CandidateCenter(SurfaceAddressOffset(
                address, u * VOLCANO_CANDIDATE_SPACING,
                v * VOLCANO_CANDIDATE_SPACING, 0));
            float other = 0.0f;
            int ignored = 0;
            if (sample(neighbor, &other, &ignored, context) &&
                (other > potential || (other == potential && VolcanoId(neighbor) < id))) {
                return false;
            }
        }
    }
    SurfaceMapCell cell = VolcanoMapCell(address);
    address.radial = height;
    *out = (VolcanoDefinition){ .id = id, .seed = seed, .address = address,
        .mapX = cell.x, .mapZ = cell.z, .potential = potential };
    return VolcanoDefinitionValid(out);
}

bool VolcanoDefinitionValid(const VolcanoDefinition *d)
{
    if (!d || d->id != VolcanoId(d->address) || d->id == 0u ||
        d->address.radial < -1000000 || d->address.radial > 1000000 ||
        !isfinite(d->potential) || d->potential <= 0.0f || d->potential > 1.0f ||
        d->mapX < -SURFACE_EQUATOR_BLOCKS || d->mapX > SURFACE_EQUATOR_BLOCKS ||
        d->mapZ < -SURFACE_POLE_TO_POLE_BLOCKS || d->mapZ > SURFACE_POLE_TO_POLE_BLOCKS) {
        return false;
    }
    SurfaceMapCell cell = SurfaceCanonicalMapCell((float)d->mapX, (float)d->mapZ);
    if (cell.x != d->mapX || cell.z != d->mapZ) return false;
    if (d->forced) {
        SurfaceAddress mapped = SurfaceAddressFromMapCoordinates(d->address.bodyId,
            (float)d->mapX, (float)d->mapZ, d->address.radial);
        return SurfaceAddressEqual(mapped, d->address);
    }
    SurfaceMapCell expected = VolcanoMapCell(d->address);
    return expected.x == d->mapX && expected.z == d->mapZ;
}

bool VolcanoStateValid(const VolcanoState *s)
{
    return s && s->phase >= VOLCANO_DORMANT && s->phase <= VOLCANO_RECOVERY &&
        s->phaseTicks <= 1000000u && s->eruptionTicks >= 1u &&
        s->eruptionTicks <= 36000u && isfinite(s->intensity) &&
        s->intensity > 0.0f && s->intensity <= 1.0f &&
        isfinite(s->pressure) && s->pressure >= 0.0f && s->pressure <= 1.0f &&
        isfinite(s->reservoir) && s->reservoir >= 0.0f && s->reservoir <= 4096.0f;
}

VolcanoState VolcanoModelCreate(const VolcanoDefinition *definition)
{
    return (VolcanoState){ .eruptionTicks = 240u,
        .intensity = definition ? definition->potential : 0.5f,
        .reservoir = 256.0f };
}

bool VolcanoModelForce(VolcanoState *s, float intensity, unsigned ticks)
{
    if (!VolcanoStateValid(s) || !isfinite(intensity) || intensity <= 0.0f ||
        intensity > 1.0f || ticks < 1u || ticks > 36000u ||
        (s->phase != VOLCANO_DORMANT && s->phase != VOLCANO_RECOVERY) ||
        s->event == UINT32_MAX) return false;
    s->phase = VOLCANO_UNREST;
    s->phaseTicks = 0u;
    s->eruptionTicks = ticks;
    s->intensity = intensity;
    s->event++;
    return true;
}

void VolcanoModelStop(VolcanoState *s)
{
    if (!VolcanoStateValid(s) || s->phase == VOLCANO_DORMANT ||
        s->phase >= VOLCANO_COOLING) return;
    s->phase = VOLCANO_COOLING;
    s->phaseTicks = 0u;
}

void VolcanoModelStep(VolcanoState *s, const VolcanoDefinition *d)
{
    if (!VolcanoStateValid(s) || !VolcanoDefinitionValid(d)) return;
    if (s->activeTicks != UINT64_MAX) s->activeTicks++;
    s->phaseTicks++;
    unsigned duration = 1u;
    switch (s->phase) {
    case VOLCANO_DORMANT:
        duration = 1200u + (unsigned)((d->id ^ d->seed ^ s->event) % 6000u);
        s->pressure = fminf(0.7f, s->pressure + d->potential * 0.001f);
        if (d->forced || s->event == UINT32_MAX) {
            s->phaseTicks = 0u;
            return;
        }
        if (s->phaseTicks >= duration && s->reservoir > 0.0f && s->pressure >= 0.5f) {
            (void)VolcanoModelForce(s, d->potential, 120u + (unsigned)(d->id % 240u));
        }
        return;
    case VOLCANO_UNREST:
        duration = 60u + (unsigned)((1.0f - s->intensity) * 120.0f);
        s->pressure = fminf(1.0f, s->pressure + 0.02f);
        break;
    case VOLCANO_ERUPTING:
        duration = s->eruptionTicks;
        s->pressure = 0.75f + s->intensity * 0.25f;
        break;
    case VOLCANO_WANING:
        duration = 60u;
        s->pressure *= 0.96f;
        break;
    case VOLCANO_COOLING:
        /* The runtime holds this phase while nearby hot lava remains. */
        duration = 120u;
        s->pressure *= 0.96f;
        break;
    case VOLCANO_RECOVERY:
        duration = 172800u;
        break;
    }
    if (s->phaseTicks >= duration) {
        s->phase = s->phase == VOLCANO_RECOVERY ? VOLCANO_DORMANT : s->phase + 1;
        s->phaseTicks = 0u;
    }
}

const char *VolcanoPhaseName(VolcanoPhase phase)
{
    static const char *names[] = { "dormant", "unrest", "erupting",
        "waning", "cooling", "recovery" };
    return phase >= VOLCANO_DORMANT && phase <= VOLCANO_RECOVERY ? names[phase] : "invalid";
}
