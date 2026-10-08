#include "world/lava_model.h"
#include <math.h>

bool LavaMaterialValid(LavaMaterial m, unsigned volume)
{
    return isfinite(m.silica) && isfinite(m.vesicles) && isfinite(m.quenched) &&
        m.silica >= 0 && m.silica <= volume && m.vesicles >= 0 &&
        m.vesicles <= volume && m.quenched >= 0 && m.quenched <= volume;
}
LavaRock LavaRockForMaterial(LavaMaterial m, unsigned volume)
{
    if (!volume || !LavaMaterialValid(m, volume)) return LAVA_ROCK_BASALT;
    bool silicic = m.silica >= volume * 0.5;
    if (m.vesicles >= volume * 0.5) return silicic ? LAVA_ROCK_PUMICE : LAVA_ROCK_SCORIA;
    if (silicic && m.quenched >= volume * 0.25) return LAVA_ROCK_OBSIDIAN;
    return silicic ? LAVA_ROCK_RHYOLITE : LAVA_ROCK_BASALT;
}
const char *LavaRockName(LavaRock rock)
{
    static const char *names[] = { "basalt", "obsidian", "scoria", "pumice", "rhyolite" };
    return rock >= LAVA_ROCK_BASALT && rock <= LAVA_ROCK_RHYOLITE ? names[rock] : "unknown";
}

double LavaSpecificEnthalpy(double temperatureK)
{
    if (!isfinite(temperatureK)) return 0.0;
    return fmax(temperatureK - LAVA_REFERENCE_K, 0.0) +
        (temperatureK > LAVA_FREEZING_K ? LAVA_LATENT_K : 0.0);
}

bool LavaThermalValid(LavaThermalState s)
{
    return s.volume <= LAVA_CAPACITY && isfinite(s.energy) && s.energy >= 0.0 &&
        s.energy <= (double)s.volume * 4000.0 && (s.volume > 0u || s.energy == 0.0);
}

double LavaTemperature(LavaThermalState s)
{
    if (!s.volume) return LAVA_REFERENCE_K;
    double e = s.energy / s.volume;
    double solid = LAVA_FREEZING_K - LAVA_REFERENCE_K;
    if (e <= solid) return LAVA_REFERENCE_K + e;
    if (e <= solid + LAVA_LATENT_K) return LAVA_FREEZING_K;
    return LAVA_REFERENCE_K + e - LAVA_LATENT_K;
}

unsigned LavaLiquidVolume(LavaThermalState s)
{
    if (!s.volume) return 0u;
    double fraction = (s.energy / s.volume - (LAVA_FREEZING_K - LAVA_REFERENCE_K)) / LAVA_LATENT_K;
    if (fraction >= 1.0) return s.volume;
    if (fraction <= 0.0) return 0u;
    return (unsigned)floor(fraction * s.volume);
}

double LavaLiquidEnthalpy(LavaThermalState s)
{
    return fmax(s.volume ? s.energy / s.volume : 0.0,
                LAVA_FREEZING_K - LAVA_REFERENCE_K + LAVA_LATENT_K);
}

unsigned LavaFlowBudget(LavaThermalState s)
{
    unsigned liquid = LavaLiquidVolume(s);
    if (!liquid) return 0u;
    double mobility = fmin(1.0, fmax(0.05, (LavaTemperature(s) - LAVA_FREEZING_K) / 350.0));
    mobility *= (double)liquid / s.volume;
    /* A hot cell can drain within one fixed step; temperature and liquid
       fraction still slow partially frozen flow. This avoids baking a
       four-step cell-crossing delay into natural-scale lava channels. */
    unsigned budget = (unsigned)fmax(1.0, floor(LAVA_CAPACITY * mobility));
    return budget < liquid ? budget : liquid;
}

double LavaCool(LavaThermalState *s, double ambientK, double seconds)
{
    if (!s || !LavaThermalValid(*s) || !s->volume || !isfinite(ambientK) ||
        !isfinite(seconds) || seconds <= 0.0 || seconds > LAVA_STEP_SECONDS + 1e-9) return 0.0;
    ambientK = fmin(330.0, fmax(LAVA_REFERENCE_K, ambientK));
    double floorEnergy = s->volume * (ambientK - LAVA_REFERENCE_K);
    /* Thin films expose more area per unit volume. This is a game-scale
       surface cooling coefficient, not a full radiation/conduction solver. */
    double area = 1.0 + 4.0 * (double)s->volume / LAVA_CAPACITY;
    double loss = fmin(fmax(s->energy - floorEnergy, 0.0),
                       0.5 * LAVA_CAPACITY * area * seconds);
    s->energy -= loss;
    return loss;
}
