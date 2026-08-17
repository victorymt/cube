#include "app/game_biology.h"

#include "ecology/evolution.h"
#include "ecology/evolution_catalog.h"
#include "presentation/render.h"
#include "space/space_state.h"
#include "world/world.h"

#include "raylib.h"

#include <stddef.h>
#include <stdio.h>

static const char *EvolutionModuleName(CreatureModuleType type)
{
    switch (type) {
    case CREATURE_MODULE_TORSO: return "TORSO";
    case CREATURE_MODULE_HEAD: return "HEAD";
    case CREATURE_MODULE_LIMB: return "LIMB";
    case CREATURE_MODULE_FOOT: return "FOOT";
    case CREATURE_MODULE_WING: return "WING";
    case CREATURE_MODULE_FIN: return "FIN";
    case CREATURE_MODULE_TAIL: return "TAIL";
    case CREATURE_MODULE_SENSOR: return "SENSOR";
    case CREATURE_MODULE_ARMOR: return "ARMOR";
    default: return "UNKNOWN";
    }
}

static void EvolutionChildrenText(const EvolutionCatalogIndividual *individual,
                                  unsigned first, unsigned count,
                                  char *text, size_t textSize)
{
    if (!text || textSize == 0u) return;
    text[0] = '\0';
    if (!individual || first >= individual->childCount) {
        snprintf(text, textSize, "none");
        return;
    }
    unsigned end = first + count;
    if (end > individual->childCount) end = individual->childCount;
    size_t used = 0u;
    for (unsigned child = first; child < end && used < textSize; child++) {
        int written = snprintf(text + used, textSize - used, "%s%08X",
                               child == first ? "" : " ",
                               individual->childIds[child]);
        if (written < 0 || (size_t)written >= textSize - used) break;
        used += (size_t)written;
    }
}

void DrawEvolutionScanPanel(const EntityEvolutionDebugInfo *info,
                                   bool scanLocked)
{
    if (!info || !info->valid) return;
    int width = 350;
    int x = GetScreenWidth() - width - 18;
    int y = 74;
    float height = scanLocked ? 320.0f : 150.0f;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y,
                                     (float)width, height },
                         0.04f, 6, Fade((Color){ 10, 18, 24, 255 }, 0.90f));
    DrawRectangleRoundedLinesEx((Rectangle){ (float)x, (float)y,
                                             (float)width, height },
                                0.04f, 6, 1.0f,
                                Fade((Color){ 114, 218, 172, 255 }, 0.72f));
    UiDrawText(TextFormat("%s  SPECIES %08X  //  LINEAGE %08X",
                          scanLocked ? "LOCKED" : "SCAN",
                          info->speciesId, info->lineageId),
               x + 14, y + 12, 16, (Color){ 176, 238, 208, 255 });
    UiDrawText(TextFormat("%s  GEN %u  MODULES %u  MUT %u",
                          EvolutionLocomotionName(info->locomotion),
                          info->generation, info->moduleCount,
                          info->mutationCount),
               x + 14, y + 39, 15, Fade(RAYWHITE, 0.90f));
    UiDrawText(TextFormat("AGE %.1f / %.1f d   %s%s",
                          info->ageDays, info->maturityAgeDays,
                          info->sex == CREATURE_SEX_FEMALE ? "F" : "M",
                          info->pregnant ? "  GESTATING" : ""),
               x + 14, y + 64, 15, Fade(RAYWHITE, 0.80f));
    UiDrawText(TextFormat("MASS %.2f   SPEED %.2f   DIET %.2f",
                          info->mass, info->speed, info->diet),
               x + 14, y + 89, 15, Fade(RAYWHITE, 0.80f));
    UiDrawText(TextFormat("HEALTH %3.0f%%   ENERGY %3.0f%%  %s",
                          info->health * 100.0f, info->energy * 100.0f,
                          info->corpse ? "CORPSE" :
                          info->juvenile ? "JUVENILE" : "ADULT"),
               x + 14, y + 116, 15, Fade(RAYWHITE, 0.80f));
    if (!scanLocked) return;
    EvolutionCatalogIndividual individual = { 0 };
    bool haveIndividual = EvolutionCatalogGetIndividual(
        PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed(),
        WorldCurrentSurfaceId(), info->organismId, &individual);
    UiDrawText(TextFormat("PARENTS  M:%08X  F:%08X",
                          info->motherId, info->fatherId),
               x + 14, y + 143, 14, Fade(RAYWHITE, 0.76f));
    UiDrawText(TextFormat("CHILDREN %u",
                          haveIndividual ? individual.childCount : 0u),
               x + 14, y + 166, 13, (Color){ 142, 216, 244, 255 });
    char firstChildren[96];
    char remainingChildren[96];
    EvolutionChildrenText(haveIndividual ? &individual : NULL, 0u, 4u,
                          firstChildren, sizeof(firstChildren));
    EvolutionChildrenText(haveIndividual ? &individual : NULL, 4u, 4u,
                          remainingChildren, sizeof(remainingChildren));
    UiDrawText(firstChildren, x + 14, y + 184, 12, Fade(RAYWHITE, 0.72f));
    if (haveIndividual && individual.childCount > 4u) {
        UiDrawText(remainingChildren, x + 14, y + 201, 12,
                   Fade(RAYWHITE, 0.72f));
    }
    UiDrawText(TextFormat("BODY %.2f long  %.2f radius  %.2f energy cost",
                          info->phenotype.bodyLength,
                          info->phenotype.bodyRadius,
                          info->phenotype.energyCost),
               x + 14, y + 222, 14, Fade(RAYWHITE, 0.76f));
    UiDrawText("MODULES", x + 14, y + 245, 13,
               (Color){ 142, 216, 244, 255 });
    int moduleY = y + 263;
    unsigned visible = info->phenotype.moduleCount < 4u
        ? info->phenotype.moduleCount : 4u;
    for (unsigned index = 0; index < visible; index++) {
        const CreatureModule *module = &info->phenotype.modules[index];
        UiDrawText(TextFormat("%s  %.2fx%.2fx%.2f  %.2f kg",
                              EvolutionModuleName((CreatureModuleType)module->type),
                              module->length, module->width, module->height,
                              module->mass),
                   x + 14, moduleY, 12, Fade(RAYWHITE, 0.72f));
        moduleY += 13;
    }
}

int BiologyAtlasNextSlot(int current, int direction)
{
    int start = current < 0 ? (direction > 0 ? -1 : EVOLUTION_CATALOG_MAX_SPECIES) : current;
    for (int step = 0; step < EVOLUTION_CATALOG_MAX_SPECIES; step++) {
        start += direction;
        if (start < 0) start = EVOLUTION_CATALOG_MAX_SPECIES - 1;
        if (start >= EVOLUTION_CATALOG_MAX_SPECIES) start = 0;
        EvolutionCatalogSpecies species = { 0 };
        if (EvolutionCatalogGetSpecies(start, &species)) return start;
    }
    return -1;
}

void DrawBiologyAtlas(GameRuntime *game)
{
    if (!game || !game->biologyAtlasOpen) return;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Fade((Color){ 5, 12, 18, 255 }, 0.96f));
    UiDrawText("BIOLOGY ATLAS", 30, 24, 26, (Color){ 176, 238, 208, 255 });
    UiDrawText(TextFormat("DISCOVERED SPECIES %d   INDIVIDUAL RECORDS %d",
                          EvolutionCatalogSpeciesCount(),
                          EvolutionCatalogIndividualCount()),
               32, 58, 14, Fade(RAYWHITE, 0.68f));
    UiDrawText("B / ESC close   UP/DOWN select", sw - 270, 30, 13,
               Fade(RAYWHITE, 0.62f));

    int listX = 28;
    int listY = 92;
    int listWidth = 250;
    DrawRectangleRounded((Rectangle){ (float)listX, (float)listY,
                                     (float)listWidth, (float)sh - 126.0f },
                         0.03f, 5, Fade((Color){ 12, 27, 34, 255 }, 0.92f));
    int listRows = (sh - 150) / 42;
    if (listRows < 1) listRows = 1;
    int selectedRank = 0;
    int rank = 0;
    for (int slot = 0; slot < EVOLUTION_CATALOG_MAX_SPECIES; slot++) {
        EvolutionCatalogSpecies species = { 0 };
        if (!EvolutionCatalogGetSpecies(slot, &species)) continue;
        if (slot == game->biologyAtlasSlot) selectedRank = rank;
        rank++;
    }
    int listScroll = selectedRank >= listRows ? selectedRank - listRows + 1 : 0;
    int visibleIndex = 0;
    for (int slot = 0; slot < EVOLUTION_CATALOG_MAX_SPECIES; slot++) {
        EvolutionCatalogSpecies species = { 0 };
        if (!EvolutionCatalogGetSpecies(slot, &species)) continue;
        if (visibleIndex < listScroll || visibleIndex >= listScroll + listRows) {
            visibleIndex++;
            continue;
        }
        Rectangle row = { (float)listX + 8.0f,
                          (float)listY + 8.0f + (visibleIndex - listScroll) * 42.0f,
                          (float)listWidth - 16.0f, 36.0f };
        bool selected = slot == game->biologyAtlasSlot;
        if (selected) DrawRectangleRounded(row, 0.12f, 4,
                                           Fade((Color){ 50, 112, 96, 255 }, 0.85f));
        UiDrawText(TextFormat("%08X  GEN %u", species.speciesId,
                              species.representativeGenome.generation),
                   (int)row.x + 8, (int)row.y + 5, 14,
                   selected ? RAYWHITE : Fade(RAYWHITE, 0.80f));
        CreaturePhenotype phenotype = EvolutionDevelop(
            &species.representativeGenome);
        UiDrawText(TextFormat("%s  %u scans", EvolutionLocomotionName(
                              phenotype.locomotion), species.observationCount),
                   (int)row.x + 8, (int)row.y + 21, 11,
                   Fade(RAYWHITE, selected ? 0.82f : 0.58f));
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), row)) {
            game->biologyAtlasSlot = slot;
        }
        visibleIndex++;
    }

    if (game->biologyAtlasSlot < 0) {
        game->biologyAtlasSlot = EvolutionCatalogFirstSpeciesSlot();
    }
    EvolutionCatalogSpecies species = { 0 };
    if (!EvolutionCatalogGetSpecies(game->biologyAtlasSlot, &species)) {
        UiDrawText("No species discovered. Scan an evolvable organism first.",
                   310, 120, 18, Fade(RAYWHITE, 0.78f));
        return;
    }
    CreaturePhenotype phenotype = EvolutionDevelop(&species.representativeGenome);
    int detailX = 310;
    UiDrawText(TextFormat("SPECIES %08X", species.speciesId), detailX, 100, 22,
               RAYWHITE);
    UiDrawText(TextFormat("LINEAGE %08X   REPRESENTATIVE GENOME %08X",
                          species.lineageId,
                          species.representativeGenome.genomeId),
               detailX, 132, 14, (Color){ 176, 238, 208, 255 });
    UiDrawText(TextFormat("%s  GENERATION %u  MUTATIONS %u",
                          EvolutionLocomotionName(phenotype.locomotion),
                          species.representativeGenome.generation,
                          species.representativeGenome.mutationCount),
               detailX, 158, 16, Fade(RAYWHITE, 0.86f));
    UiDrawText(TextFormat("MASS %.2f  LENGTH %.2f  RADIUS %.2f  SPEED %.2f",
                          phenotype.totalMass, phenotype.bodyLength,
                          phenotype.bodyRadius, phenotype.cruiseSpeed),
               detailX, 184, 14, Fade(RAYWHITE, 0.76f));
    UiDrawText(TextFormat("DIET %.2f  ATTACK %.2f  DEFENSE %.2f  ENERGY %.2f",
                          phenotype.diet, phenotype.attack, phenotype.defense,
                          phenotype.energyCost),
               detailX, 208, 14, Fade(RAYWHITE, 0.76f));
    UiDrawText(TextFormat("FIRST SEEN %d, %d   OBSERVATIONS %u",
                          species.firstX, species.firstZ,
                          species.observationCount),
               detailX, 232, 14, Fade(RAYWHITE, 0.68f));
    UiDrawText(TextFormat("WORLD %08X   SURFACE %08X",
                          species.worldSeed, species.surfaceId),
               detailX, 256, 13, Fade(RAYWHITE, 0.62f));
    EntityEvolutionDebugInfo lockedInfo = { 0 };
    EvolutionCatalogIndividual lockedIndividual = { 0 };
    int lockedIndex = game->evolutionScanLocked
        ? EntityEvolutionFindByOrganism(game->evolutionLockedOrganismId) : -1;
    bool haveLockedFamily = EntityEvolutionInspect(lockedIndex, &lockedInfo) &&
        lockedInfo.speciesId == species.speciesId &&
        EvolutionCatalogGetIndividual(
            PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed(),
            WorldCurrentSurfaceId(), lockedInfo.organismId,
            &lockedIndividual);
    UiDrawText(haveLockedFamily
                   ? TextFormat("FAMILY M:%08X F:%08X  CHILDREN %u",
                                lockedInfo.motherId, lockedInfo.fatherId,
                                lockedIndividual.childCount)
                   : "FAMILY lock a living representative to inspect relations",
               detailX, 278, 13, Fade(RAYWHITE, 0.66f));
    if (haveLockedFamily) {
        char familyChildren[192];
        EvolutionChildrenText(&lockedIndividual, 0u,
                              EVOLUTION_CATALOG_MAX_CHILDREN,
                              familyChildren, sizeof(familyChildren));
        UiDrawText(familyChildren, detailX, 298, 12, Fade(RAYWHITE, 0.62f));
    }
    UiDrawText("EXPRESSED MODULES", detailX, 326, 15,
               (Color){ 142, 216, 244, 255 });
    int moduleY = 352;
    unsigned moduleLimit = phenotype.moduleCount < 12u ? phenotype.moduleCount : 12u;
    for (unsigned index = 0; index < moduleLimit; index++) {
        if (moduleY + 14 > sh - 20) break;
        const CreatureModule *module = &phenotype.modules[index];
        UiDrawText(TextFormat("%02u  %-6s  %.2f x %.2f x %.2f  mass %.2f  eff %.2f",
                              index + 1u,
                              EvolutionModuleName((CreatureModuleType)module->type),
                              module->length, module->width, module->height,
                              module->mass, module->efficiency),
                   detailX, moduleY, 13, Fade(RAYWHITE, 0.76f));
        moduleY += 18;
    }
}
