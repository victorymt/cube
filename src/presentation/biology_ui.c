#include "presentation/biology_ui.h"

#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "ecology/evolution.h"
#include "ecology/evolution_catalog.h"
#include "presentation/entity_renderer.h"
#include "presentation/creature_renderer.h"
#include "presentation/render.h"
#include "space/space_state.h"
#include "world/world.h"

#include "raylib.h"

#include <stddef.h>
#include <math.h>
#include <stdio.h>

typedef struct BiologyPreviewState {
    bool initialized;
    bool labMode;
    bool rotate;
    bool silhouette;
    float yaw;
    uint32_t worldSeed;
    uint32_t seed;
    EvolutionArchetype archetype;
    int lengthPercent;
    int widthPercent;
    int appendagePercent;
    CreatureGenome genome;
    CreaturePhenotype phenotype;
    RenderTexture2D texture;
    BiologyPreviewPoseInfo motion;
    BiologyPreviewScenarioInfo scenario;
    FaunaPoseState pose;
    float animationTime;
} BiologyPreviewState;

static BiologyPreviewState preview;

static const char *PreviewArchetypeName(EvolutionArchetype archetype)
{
    switch (archetype) {
    case EVOLUTION_ARCHETYPE_FLIGHT: return "FLIGHT";
    case EVOLUTION_ARCHETYPE_AQUATIC: return "AQUATIC";
    default: return "GROUND";
    }
}

static uint8_t PreviewDimension(uint8_t value, int percent)
{
    int scaled = ((int)value * percent + 50) / 100;
    if (scaled < 1) scaled = 1;
    if (scaled > 255) scaled = 255;
    return (uint8_t)scaled;
}

static void PreviewRebuild(void)
{
    preview.genome = EvolutionGenomeSeed(preview.seed, preview.archetype);
    for (int chromosomeIndex = 0; chromosomeIndex < 2; chromosomeIndex++) {
        CreatureChromosome *chromosome =
            &preview.genome.chromosomes[chromosomeIndex];
        for (unsigned index = 0; index < chromosome->geneCount; index++) {
            DevelopmentGene *gene = &chromosome->genes[index];
            bool appendage = gene->moduleType == CREATURE_MODULE_LIMB ||
                gene->moduleType == CREATURE_MODULE_FOOT ||
                gene->moduleType == CREATURE_MODULE_WING ||
                gene->moduleType == CREATURE_MODULE_FIN ||
                gene->moduleType == CREATURE_MODULE_TAIL;
            gene->length = PreviewDimension(gene->length,
                appendage ? preview.appendagePercent : preview.lengthPercent);
            gene->width = PreviewDimension(gene->width, preview.widthPercent);
        }
    }
    preview.genome.genomeId = EvolutionGenomeHash(&preview.genome);
    preview.phenotype = EvolutionDevelop(&preview.genome);
}

static void PreviewInitialize(uint32_t worldSeed)
{
    if (preview.initialized && preview.worldSeed == worldSeed) return;
    preview.initialized = true;
    preview.worldSeed = worldSeed;
    preview.scenario = (BiologyPreviewScenarioInfo){0};
    preview.seed = worldSeed ^ 0x51f15e9du;
    preview.archetype = EVOLUTION_ARCHETYPE_GROUND;
    preview.lengthPercent = 100;
    preview.widthPercent = 100;
    preview.appendagePercent = 100;
    preview.yaw = -0.45f;
    preview.motion = (BiologyPreviewPoseInfo){0};
    preview.pose = (FaunaPoseState){0};
    PreviewRebuild();
}

void BiologyAtlasPreviewConfigure(uint32_t worldSeed, EvolutionArchetype type,
                                  uint32_t seed, int length, int width,
                                  int appendages, bool silhouette)
{
    PreviewInitialize(worldSeed);
    preview.labMode = true;
    preview.scenario = (BiologyPreviewScenarioInfo){0};
    preview.archetype = type;
    preview.seed = seed;
    preview.lengthPercent = length;
    preview.widthPercent = width;
    preview.appendagePercent = appendages;
    preview.rotate = false;
    preview.silhouette = silhouette;
    preview.yaw = -0.45f;
    preview.motion = (BiologyPreviewPoseInfo){0};
    preview.pose = (FaunaPoseState){0};
    PreviewRebuild();
}

bool BiologyAtlasPreviewPoseConfigure(int action, float speed, float phase, float turn)
{
    if (!preview.initialized || !preview.labMode) return false;
    if (action == -1) { preview.motion.frozen = false; return true; }
    if (action == -2) { preview.motion.frozen = true; return true; }
    if (action < 0 || action > FAUNA_ACTION_COUNT || !isfinite(speed) ||
        speed < 0 || speed > 8 || !isfinite(phase) || phase < 0 || phase >= 1 ||
        !isfinite(turn) || fabsf(turn) > 1) return false;
    if (action != preview.motion.action) preview.scenario.valid = false;
    preview.motion = (BiologyPreviewPoseInfo){action, speed, phase, turn, true};
    preview.pose = (FaunaPoseState){
        .gaitPhase = phase, .actionPhase = phase,
        .movement = fminf(1, speed / 0.35f), .turn = turn,
        .engagement = 1
    };
    if (action < FAUNA_ACTION_COUNT) preview.pose.weights[action] = 1;
    preview.animationTime = phase * 2 * PI;
    return true;
}

BiologyPreviewPoseInfo BiologyAtlasPreviewPoseInfo(void)
{
    return preview.motion;
}

bool BiologyAtlasPreviewScenarioConfigure(const FaunaBehaviorInput *input)
{
    if (!input || !preview.initialized || !preview.labMode) return false;
    FaunaBehaviorDecision decision = FaunaBehaviorEvaluate(input);
    float speed = decision.action == FAUNA_ACTION_FLEE ? 2.0f :
        FaunaBehaviorActionMoves(decision.action) ? 1.0f : 0.0f;
    if (!BiologyAtlasPreviewPoseConfigure(decision.action, speed, 0.48f, 0)) return false;
    preview.scenario = (BiologyPreviewScenarioInfo){true, *input, decision};
    return true;
}

BiologyPreviewScenarioInfo BiologyAtlasPreviewScenarioInfo(void)
{
    return preview.scenario;
}

static bool PreviewButton(Rectangle bounds, const char *label, bool selected)
{
    bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    DrawRectangleRounded(bounds, 0.16f, 4,
        selected ? (Color){ 50, 112, 96, 255 } :
        hovered ? (Color){ 39, 69, 79, 255 } :
                  (Color){ 22, 43, 52, 255 });
    UiDrawText(label, (int)bounds.x + 9, (int)bounds.y + 6, 14,
               selected ? RAYWHITE : Fade(RAYWHITE, 0.80f));
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void PreviewAdjust(int *value, int delta)
{
    *value += delta;
    if (*value < 50) *value = 50;
    if (*value > 180) *value = 180;
}

static void PreviewRender3D(Rectangle bounds)
{
    if (preview.texture.id == 0) {
        preview.texture = LoadRenderTexture(840, 520);
    }
    if (preview.texture.id == 0) return;
    Entity specimen = { 0 };
    specimen.active = true;
    specimen.evolvable = true;
    specimen.type = ENTITY_ALIEN_GRAZER;
    specimen.genome = preview.genome;
    specimen.phenotype = preview.phenotype;
    specimen.organismScale = 1.0f;
    specimen.ecologyActivity = 1.0f;
    specimen.ecologyCapacity = 1.0f;
    specimen.primaryBlock = BLOCK_GRASS;
    specimen.accentBlock = BLOCK_DIRT;
    if (IsKeyPressed(KEY_SPACE)) preview.rotate = !preview.rotate;
    if (IsKeyPressed(KEY_V)) preview.silhouette = !preview.silhouette;
    if (preview.rotate) preview.yaw += GetFrameTime() * 0.35f;
    specimen.yaw = preview.yaw;
    specimen.airborne = preview.phenotype.locomotion == CREATURE_LOCOMOTION_FLIGHT;
    specimen.aquatic = preview.phenotype.locomotion == CREATURE_LOCOMOTION_AQUATIC;
    specimen.behavior = (FaunaBehaviorAction)preview.motion.action;
    specimen.needs = preview.scenario.valid ? preview.scenario.input.needs : FaunaNeedsDefault();
    specimen.corpse = preview.motion.action == FAUNA_ACTION_COUNT;
    specimen.velocity = (Vector3){sinf(specimen.yaw) * preview.motion.speed, 0,
                                  cosf(specimen.yaw) * preview.motion.speed};
    if (!preview.motion.frozen && !specimen.corpse) {
        float dt = fminf(GetFrameTime(), 0.1f);
        preview.animationTime += dt;
        preview.pose = FaunaPoseAdvance(preview.pose, (FaunaPoseInput){
            .action = specimen.behavior, .speed = preview.motion.speed,
            .distance = preview.motion.speed * dt, .scale = 1,
            .turnRate = preview.motion.turn * 2, .engaged = true
        }, dt);
        preview.motion.phase = preview.pose.gaitPhase;
    }
    specimen.phase = preview.animationTime;
    specimen.pose = preview.pose;
    if (specimen.airborne && !specimen.corpse) {
        specimen.position.y = 0.9f * (1 - fminf(1,
            specimen.pose.weights[FAUNA_ACTION_REST] + specimen.pose.weights[FAUNA_ACTION_NEST]));
    }
    /* Fit the complete developed hierarchy, including repeated descendants. */
    Vector3 centers[EVOLUTION_MAX_MODULES] = {0};
    float extent = 1.0f, bottom = 0.0f, top = 0.0f;
    for (unsigned i = 0; i < preview.phenotype.moduleCount; i++) {
        const CreatureModule *m = &preview.phenotype.modules[i];
        Vector3 c = {m->localY, m->localZ, m->localX};
        if (m->parentIndex >= 0 && m->parentIndex < (int)i) {
            Vector3 parent = centers[(unsigned)m->parentIndex];
            c.x += parent.x; c.y += parent.y; c.z += parent.z;
        }
        centers[i] = c;
        float radial = sqrtf(c.x*c.x + c.z*c.z) +
            fmaxf(m->width, m->length) * 0.60f;
        extent = fmaxf(extent, radial);
        bottom = fminf(bottom, c.y - m->height * 0.5f);
        top = fmaxf(top, c.y + m->height * 0.5f);
    }
    float targetY = (top - bottom) * 0.5f;
    if (preview.phenotype.locomotion == CREATURE_LOCOMOTION_AQUATIC) targetY = 0.4f;
    targetY += specimen.position.y;
    float viewHeight = fmaxf((top-bottom) * 1.7f, extent * 1.85f);
    Camera3D camera = {
        .position = { 5.0f, targetY + 3.0f, 7.0f },
        .target = { 0.0f, targetY, 0.0f },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = viewHeight,
        .projection = CAMERA_ORTHOGRAPHIC
    };
    BeginTextureMode(preview.texture);
    ClearBackground((Color){ 8, 20, 28, 255 });
    BeginMode3D(camera);
    for (int line = -4; line <= 4; line++) {
        Color grid = {22, 39, 46, 255};
        DrawLine3D((Vector3){(float)line, 0, -4},
                   (Vector3){(float)line, 0, 4}, grid);
        DrawLine3D((Vector3){-4, 0, (float)line},
                   (Vector3){4, 0, (float)line}, grid);
    }
    CreatureRendererSetSilhouette(preview.silhouette);
    EntityRendererDraw(&specimen, 1);
    CreatureRendererSetSilhouette(false);
    EndMode3D();
    EndTextureMode();
    DrawTexturePro(preview.texture.texture,
                   (Rectangle){ 0.0f, 0.0f, 840.0f, -520.0f },
                   bounds, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    UiDrawText(TextFormat("%s  %.1f m/s  %s",
        FaunaBehaviorActionName(specimen.behavior), preview.motion.speed,
        preview.motion.frozen ? "FROZEN" : "LIVE"),
        (int)bounds.x + 10, (int)bounds.y + 10, 14, (Color){176, 238, 208, 255});
    if (preview.scenario.valid) {
        UiDrawText(TextFormat("DECISION %.3f  ENERGY %.2f  WATER %.2f",
            preview.scenario.decision.utility, specimen.needs.energy,
            specimen.needs.hydration), (int)bounds.x + 10,
            (int)bounds.y + 30, 14, (Color){176, 238, 208, 255});
    }
}

static void PreviewDrawControl(int x, int y, const char *label, int *value,
                               bool *changed)
{
    UiDrawText(label, x, y + 7, 15, Fade(RAYWHITE, 0.85f));
    if (PreviewButton((Rectangle){ (float)x + 190.0f, (float)y, 32.0f, 29.0f },
                      "-", false)) {
        PreviewAdjust(value, -10);
        *changed = true;
    }
    UiDrawText(TextFormat("%d%%", *value), x + 231, y + 7, 14,
               (Color){ 176, 238, 208, 255 });
    if (PreviewButton((Rectangle){ (float)x + 289.0f, (float)y, 32.0f, 29.0f },
                      "+", false)) {
        PreviewAdjust(value, 10);
        *changed = true;
    }
}

static void PreviewDrawLab(int sw, int sh)
{
    int x = 310;
    int previewWidth = sw - x - 28;
    if (previewWidth > 600) previewWidth = 600;
    bool changed = false;
    UiDrawText("GENERATED SPECIMEN", x, 100, 22, RAYWHITE);
    UiDrawText("Shape the specimen. SPACE rotates; V shows its silhouette.",
               x, 130, 14, Fade(RAYWHITE, 0.70f));
    if (PreviewButton((Rectangle){ (float)x, 166.0f, 92.0f, 30.0f },
                      "GROUND", preview.archetype == EVOLUTION_ARCHETYPE_GROUND) ||
        IsKeyPressed(KEY_ONE)) {
        preview.archetype = EVOLUTION_ARCHETYPE_GROUND;
        changed = true;
    }
    if (PreviewButton((Rectangle){ (float)x + 99.0f, 166.0f, 92.0f, 30.0f },
                      "FLIGHT", preview.archetype == EVOLUTION_ARCHETYPE_FLIGHT) ||
        IsKeyPressed(KEY_TWO)) {
        preview.archetype = EVOLUTION_ARCHETYPE_FLIGHT;
        changed = true;
    }
    if (PreviewButton((Rectangle){ (float)x + 198.0f, 166.0f, 92.0f, 30.0f },
                      "AQUATIC", preview.archetype == EVOLUTION_ARCHETYPE_AQUATIC) ||
        IsKeyPressed(KEY_THREE)) {
        preview.archetype = EVOLUTION_ARCHETYPE_AQUATIC;
        changed = true;
    }
    if (PreviewButton((Rectangle){ (float)x, 207.0f, 32.0f, 29.0f },
                      "-", false) || IsKeyPressed(KEY_LEFT)) {
        preview.seed--;
        changed = true;
    }
    UiDrawText(TextFormat("SEED %08X", preview.seed), x + 43, 214, 15,
               (Color){ 176, 238, 208, 255 });
    if (PreviewButton((Rectangle){ (float)x + 190.0f, 207.0f, 32.0f, 29.0f },
                      "+", false) || IsKeyPressed(KEY_RIGHT)) {
        preview.seed++;
        changed = true;
    }
    if (PreviewButton((Rectangle){ (float)x + 232.0f, 207.0f, 88.0f, 29.0f },
                      "RANDOM", false) || IsKeyPressed(KEY_R)) {
        preview.seed = (uint32_t)GetRandomValue(1, 0x7fffffff);
        changed = true;
    }
    PreviewDrawControl(x, 248, "BODY LENGTH", &preview.lengthPercent,
                       &changed);
    PreviewDrawControl(x, 285, "BODY WIDTH", &preview.widthPercent,
                       &changed);
    PreviewDrawControl(x, 322, "APPENDAGES", &preview.appendagePercent,
                       &changed);
    if (changed) PreviewRebuild();
    UiDrawText(TextFormat("%s  GENOME %08X  %s  MODULES %u",
                          PreviewArchetypeName(preview.archetype),
                          preview.genome.genomeId,
                          EvolutionLocomotionName(preview.phenotype.locomotion),
                          preview.phenotype.moduleCount),
               x, 367, 14, Fade(RAYWHITE, 0.82f));
    int previewY = 394;
    int previewHeight = sh - previewY - 28;
    if (previewHeight > previewWidth * 520 / 840)
        previewHeight = previewWidth * 520 / 840;
    previewWidth = previewHeight * 840 / 520;
    if (previewHeight > 100 && previewWidth > 100) {
        PreviewRender3D((Rectangle){ (float)x, (float)previewY,
                                     (float)previewWidth,
                                     (float)previewHeight });
    }
    UiDrawText("1/2/3 type   arrows seed   R random   SPACE rotate   V silhouette   TAB catalog",
               30, sh - 37, 13, Fade(RAYWHITE, 0.65f));
}

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
        EcologyWorldSurfaceId(), info->organismId, &individual);
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

void BiologyAtlasDraw(const BiologyAtlasUiInput *input, BiologyAtlasUiOutput *output)
{
    if (output) *output = (BiologyAtlasUiOutput){
        .selectedSlot = input ? input->selectedSlot : -1,
        .closeRequested = false
    };
    if (!input || !output || !input->open) return;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Fade((Color){ 5, 12, 18, 255 }, 0.96f));
    UiDrawText("BIOLOGY ATLAS", 30, 24, 26, (Color){ 176, 238, 208, 255 });
    UiDrawText(TextFormat("DISCOVERED SPECIES %d   INDIVIDUAL RECORDS %d",
                          EvolutionCatalogSpeciesCount(),
                          EvolutionCatalogIndividualCount()),
               32, 58, 14, Fade(RAYWHITE, 0.68f));
    UiDrawText("B / ESC close   TAB preview", sw - 250, 30, 13,
               Fade(RAYWHITE, 0.62f));

    PreviewInitialize(PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed());
    int speciesCount = EvolutionCatalogSpeciesCount();
    if (speciesCount > 0 && IsKeyPressed(KEY_TAB)) {
        preview.labMode = !preview.labMode;
    }
    if (speciesCount == 0 || preview.labMode) {
        DrawRectangleRounded((Rectangle){ 28.0f, 92.0f, 250.0f,
                                           (float)sh - 126.0f },
                             0.03f, 5, Fade((Color){ 12, 27, 34, 255 }, 0.92f));
        UiDrawText(speciesCount == 0 ? "NO SPECIES DISCOVERED" :
                   "SPECIMEN LAB", 42, 110, 15,
                   (Color){ 176, 238, 208, 255 });
        UiDrawText("Preview changes stay in the lab.", 42, 146, 13,
                   Fade(RAYWHITE, 0.72f));
        UiDrawText("They do not change world fauna.", 42, 167, 13,
                   Fade(RAYWHITE, 0.72f));
        PreviewDrawLab(sw, sh);
        return;
    }

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
        if (slot == output->selectedSlot) selectedRank = rank;
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
        bool selected = slot == output->selectedSlot;
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
            output->selectedSlot = slot;
        }
        visibleIndex++;
    }

    if (output->selectedSlot < 0) {
        output->selectedSlot = EvolutionCatalogFirstSpeciesSlot();
    }
    EvolutionCatalogSpecies species = { 0 };
    if (!EvolutionCatalogGetSpecies(output->selectedSlot, &species)) {
        UiDrawText("Select a discovered species or press TAB for the lab.",
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
    int lockedIndex = input->evolutionScanLocked
        ? EntityEvolutionFindByOrganism(input->evolutionLockedOrganismId) : -1;
    bool haveLockedFamily = EntityEvolutionInspect(lockedIndex, &lockedInfo) &&
        lockedInfo.speciesId == species.speciesId &&
        EvolutionCatalogGetIndividual(
            PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed(),
            EcologyWorldSurfaceId(), lockedInfo.organismId,
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
