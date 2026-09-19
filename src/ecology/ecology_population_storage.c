#include "ecology/ecology_population_internal.h"
#include "ecology/ecology_nutrient_model.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

EcologyPopulationRecord
    ecologyPopulationRecords[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
/* Inactive records retain their complete state until revisited. The sorted
 * backing store makes lookups and checkpoint order independent of cache slots. */
static EcologyPopulationRecord *ecologyPopulationColdRecords = NULL;
static size_t ecologyPopulationColdCount = 0u;
static size_t ecologyPopulationColdCapacity = 0u;
static size_t ecologyPopulationActiveCount = 0u;
static size_t ecologyPopulationFailedAdmissions = 0u;
#ifdef ECOLOGY_TESTING
static bool ecologyPopulationFailStorageAllocation = false;
#endif
static uint64_t ecologyPopulationAccessSerial = 0u;
uint32_t ecologyPopulationEpoch = 1u;
pthread_mutex_t ecologyPopulationMutex = PTHREAD_MUTEX_INITIALIZER;

static int EcologyPopulationCompareKeys(
    const EcologyPopulationRecord *first,
    const EcologyPopulationRecord *second)
{
    if (first->surfaceId != second->surfaceId)
        return first->surfaceId < second->surfaceId ? -1 : 1;
    if (first->regionX != second->regionX)
        return first->regionX < second->regionX ? -1 : 1;
    if (first->regionZ != second->regionZ)
        return first->regionZ < second->regionZ ? -1 : 1;
    return 0;
}

static size_t EcologyPopulationColdLowerBound(
    uint32_t surfaceId, int regionX, int regionZ, bool *found)
{
    EcologyPopulationRecord key = {
        .surfaceId = surfaceId, .regionX = regionX, .regionZ = regionZ
    };
    size_t low = 0u;
    size_t high = ecologyPopulationColdCount;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (EcologyPopulationCompareKeys(
                &ecologyPopulationColdRecords[middle], &key) < 0) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (found) *found = low < ecologyPopulationColdCount &&
        EcologyPopulationCompareKeys(
            &ecologyPopulationColdRecords[low], &key) == 0;
    return low;
}

static bool EcologyPopulationReserveCold(size_t required)
{
    if (required <= ecologyPopulationColdCapacity) return true;
    if (required > ECOLOGY_POPULATION_MAX_STORED_REGIONS) return false;
#ifdef ECOLOGY_TESTING
    if (ecologyPopulationFailStorageAllocation) return false;
#endif
    size_t capacity = ecologyPopulationColdCapacity
        ? ecologyPopulationColdCapacity : 64u;
    while (capacity < required) {
        if (capacity >= ECOLOGY_POPULATION_MAX_STORED_REGIONS / 2u) {
            capacity = ECOLOGY_POPULATION_MAX_STORED_REGIONS;
            break;
        }
        capacity *= 2u;
    }
    EcologyPopulationRecord *records = realloc(
        ecologyPopulationColdRecords, capacity * sizeof(*records));
    if (!records) return false;
    ecologyPopulationColdRecords = records;
    ecologyPopulationColdCapacity = capacity;
    return true;
}

/* The caller reserves space before modifying either cache. */
static void EcologyPopulationInsertCold(const EcologyPopulationRecord *record)
{
    size_t insertion = EcologyPopulationColdLowerBound(
        record->surfaceId, record->regionX, record->regionZ, NULL);
    memmove(&ecologyPopulationColdRecords[insertion + 1u],
            &ecologyPopulationColdRecords[insertion],
            (ecologyPopulationColdCount - insertion) * sizeof(*record));
    ecologyPopulationColdRecords[insertion] = *record;
    ecologyPopulationColdCount++;
}

static void EcologyPopulationAdmissionFailed(void)
{
    if (ecologyPopulationFailedAdmissions == 0u) {
        fprintf(stderr, "ecology: regional storage admission failed; "
                        "existing region history has been retained\n");
    }
    if (ecologyPopulationFailedAdmissions < SIZE_MAX)
        ecologyPopulationFailedAdmissions++;
}

static unsigned EcologyPopulationSetIndex(uint32_t surfaceId,
                                          int regionX, int regionZ)
{
    uint32_t hash = surfaceId;
    hash ^= (uint32_t)regionX * 0x9e3779b9u;
    hash ^= (uint32_t)regionZ * 0x85ebca6bu;
    return EcologyMix(hash) & (ECOLOGY_POPULATION_SET_COUNT - 1u);
}

EcologyPopulationRecord *EcologyPopulationRecordAt(
    uint32_t surfaceId, int regionX, int regionZ, bool *created)
{
    unsigned setIndex = EcologyPopulationSetIndex(surfaceId, regionX, regionZ);
    unsigned start = setIndex * ECOLOGY_POPULATION_SET_WAYS;
    EcologyPopulationRecord *selected = NULL;
    for (unsigned way = 0; way < ECOLOGY_POPULATION_SET_WAYS; way++) {
        EcologyPopulationRecord *record = &ecologyPopulationRecords[start + way];
        if (record->valid && record->surfaceId == surfaceId &&
            record->regionX == regionX && record->regionZ == regionZ) {
            selected = record;
            break;
        }
        if (!record->valid) {
            if (!selected || selected->valid) selected = record;
        } else if (!selected ||
                   (selected->valid && record->lastAccess < selected->lastAccess)) {
            selected = record;
        }
    }
    if (!selected) return NULL;

    bool isNew = false;
    if (!selected->valid || selected->surfaceId != surfaceId ||
        selected->regionX != regionX || selected->regionZ != regionZ) {
        bool stored = false;
        size_t coldIndex = EcologyPopulationColdLowerBound(
            surfaceId, regionX, regionZ, &stored);
        isNew = !stored;
        if ((isNew && ecologyPopulationActiveCount + ecologyPopulationColdCount >=
                ECOLOGY_POPULATION_MAX_STORED_REGIONS) ||
            (selected->valid && !stored &&
             !EcologyPopulationReserveCold(ecologyPopulationColdCount + 1u))) {
            EcologyPopulationAdmissionFailed();
            if (created) *created = false;
            return NULL;
        }
        EcologyPopulationRecord replacement = stored
            ? ecologyPopulationColdRecords[coldIndex]
            : (EcologyPopulationRecord){
                .valid = true, .surfaceId = surfaceId,
                .regionX = regionX, .regionZ = regionZ
            };
        if (stored) {
            memmove(&ecologyPopulationColdRecords[coldIndex],
                    &ecologyPopulationColdRecords[coldIndex + 1u],
                    (ecologyPopulationColdCount - coldIndex - 1u) *
                        sizeof(replacement));
            ecologyPopulationColdCount--;
        }
        if (selected->valid) {
            EcologyPopulationInsertCold(selected);
        } else {
            ecologyPopulationActiveCount++;
        }
        *selected = replacement;
    }
    /* Saturation preserves valid persisted access counters even if a loaded
     * checkpoint is at the uint64 limit. Wrapping would invalidate old records. */
    if (ecologyPopulationAccessSerial < UINT64_MAX)
        ecologyPopulationAccessSerial++;
    selected->lastAccess = ecologyPopulationAccessSerial;
    if (created) *created = isNew;
    return selected;
}

static int EcologyPopulationFindRecordIndex(uint32_t surfaceId,
                                            int regionX, int regionZ)
{
    unsigned setIndex = EcologyPopulationSetIndex(surfaceId, regionX, regionZ);
    unsigned start = setIndex * ECOLOGY_POPULATION_SET_WAYS;
    for (unsigned way = 0; way < ECOLOGY_POPULATION_SET_WAYS; way++) {
        unsigned index = start + way;
        EcologyPopulationRecord *record = &ecologyPopulationRecords[index];
        if (record->valid && record->surfaceId == surfaceId &&
            record->regionX == regionX && record->regionZ == regionZ) {
            return (int)index;
        }
    }
    return -1;
}

EcologyPopulationRecord *EcologyPopulationFindStoredRecord(
    uint32_t surfaceId, int regionX, int regionZ)
{
    int hotIndex = EcologyPopulationFindRecordIndex(surfaceId, regionX, regionZ);
    if (hotIndex >= 0) return &ecologyPopulationRecords[hotIndex];
    bool stored = false;
    size_t coldIndex = EcologyPopulationColdLowerBound(
        surfaceId, regionX, regionZ, &stored);
    return stored ? &ecologyPopulationColdRecords[coldIndex] : NULL;
}

static bool EcologyPopulationStateValid(
    const PlanetRegionalPopulation *population)
{
    if (!population) return false;
    const float values[] = {
        population->floraDensity, population->faunaDensity,
        population->floraCarryingCapacity,
        population->faunaCarryingCapacity,
        population->seasonalMemory, population->faunaHarvestPressure,
        population->radiationMemory
    };
    for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
        if (!isfinite(values[index]) || values[index] < 0.0f ||
            values[index] > 1.0f) {
            return false;
        }
    }
    return true;
}

static bool EcologyPopulationMigrationStateValid(
    const PlanetPopulationMigrationState *migration)
{
    if (!migration) return false;
    const float values[] = {
        migration->floraNet, migration->faunaNet,
        migration->floraFlowX, migration->floraFlowZ,
        migration->faunaFlowX, migration->faunaFlowZ
    };
    for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]);
         index++) {
        if (!isfinite(values[index]) || values[index] < -1.0f ||
            values[index] > 1.0f) {
            return false;
        }
    }
    return true;
}

static bool EcologyEvolutionStateValid(const PlanetEvolutionRegion *evolution)
{
    if (!evolution || evolution->lineageCount > PLANET_EVOLUTION_MAX_LINEAGES ||
        evolution->bootstrapGeneration > 24u) return false;
    if ((evolution->bootstrapComplete != 0u) !=
        (evolution->bootstrapGeneration == 24u)) return false;
    const float trophic[] = {
        evolution->herbivoreDensity, evolution->omnivoreDensity,
        evolution->carnivoreDensity
    };
    for (unsigned index = 0; index < sizeof(trophic) / sizeof(trophic[0]);
         index++) {
        if (!isfinite(trophic[index]) || trophic[index] < 0.0f ||
            trophic[index] > 1.0f) return false;
    }
    unsigned activeCount = 0u;
    float recomputedTrophic[3] = { 0.0f, 0.0f, 0.0f };
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        const PlanetEvolutionLineage *lineage = &evolution->lineages[index];
        if (lineage->active > 1u || lineage->archetype >
            (uint8_t)EVOLUTION_ARCHETYPE_AQUATIC) return false;
        if (!lineage->active) continue;
        activeCount++;
        for (unsigned previous = 0; previous < index; previous++) {
            const PlanetEvolutionLineage *peer = &evolution->lineages[previous];
            if (peer->active &&
                (peer->lineageId == lineage->lineageId ||
                 peer->speciesId == lineage->speciesId)) {
                return false;
            }
        }
        const float values[] = {
            lineage->density, lineage->dietMean, lineage->geneticVariance,
            lineage->geneFlow, lineage->fitness
        };
        if (lineage->lineageId == 0u || lineage->speciesId == 0u ||
            lineage->founderSeed == 0u || lineage->density <= 0.0f) {
            return false;
        }
        for (unsigned value = 0; value < sizeof(values) / sizeof(values[0]);
             value++) {
            if (!isfinite(values[value]) || values[value] < 0.0f ||
                values[value] > 1.0f) return false;
        }
        unsigned trophicIndex = lineage->dietMean < 0.36f ? 0u :
            lineage->dietMean < 0.64f ? 1u : 2u;
        recomputedTrophic[trophicIndex] += lineage->density;
    }
    if (activeCount != evolution->lineageCount) return false;
    for (unsigned index = 0; index < 3u; index++) {
        float expected = EcologyClamp(recomputedTrophic[index]);
        float actual = index == 0u ? evolution->herbivoreDensity :
            index == 1u ? evolution->omnivoreDensity :
            evolution->carnivoreDensity;
        if (fabsf(actual - expected) > 0.0001f) return false;
    }
    return true;
}

uint32_t EcologyPopulationEpoch(void)
{
    pthread_mutex_lock(&ecologyPopulationMutex);
    uint32_t result = ecologyPopulationEpoch;
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return result;
}

PlanetEcologyStorageStats EcologyPopulationGetStorageStats(void)
{
    pthread_mutex_lock(&ecologyPopulationMutex);
    PlanetEcologyStorageStats result = {
        .activeRegions = ecologyPopulationActiveCount,
        .coldRegions = ecologyPopulationColdCount,
        .capacityRegions = ECOLOGY_POPULATION_MAX_REGIONS +
            ecologyPopulationColdCapacity,
        .allocatedBytes = sizeof(ecologyPopulationRecords) +
            ecologyPopulationColdCapacity * sizeof(EcologyPopulationRecord),
        .maxRegions = ECOLOGY_POPULATION_MAX_STORED_REGIONS,
        .failedAdmissions = ecologyPopulationFailedAdmissions
    };
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return result;
}

#ifdef ECOLOGY_TESTING
void PlanetEcologyTestFailPopulationStorageAllocation(bool fail)
{
    pthread_mutex_lock(&ecologyPopulationMutex);
    ecologyPopulationFailStorageAllocation = fail;
    pthread_mutex_unlock(&ecologyPopulationMutex);
}

bool PlanetEcologyTestPopulationSnapshotAt(
    uint32_t surfaceId, int regionX, int regionZ,
    PlanetEcologyPopulationSnapshot *out)
{
    if (!out) return false;
    *out = (PlanetEcologyPopulationSnapshot){ 0 };
    pthread_mutex_lock(&ecologyPopulationMutex);
    int index = EcologyPopulationFindRecordIndex(surfaceId, regionX, regionZ);
    const EcologyPopulationRecord *record = index >= 0
        ? &ecologyPopulationRecords[index] : NULL;
    if (!record) {
        bool stored = false;
        size_t coldIndex = EcologyPopulationColdLowerBound(
            surfaceId, regionX, regionZ, &stored);
        if (stored) record = &ecologyPopulationColdRecords[coldIndex];
    }
    if (record) {
        *out = (PlanetEcologyPopulationSnapshot){
            .surfaceId = record->surfaceId,
            .regionX = record->regionX,
            .regionZ = record->regionZ,
            .lastUpdateTime = record->lastUpdateTime,
            .population = record->population,
            .migration = record->migration,
            .evolution = record->evolution,
            .nutrientInitialized = record->nutrientInitialized,
            .nutrients = record->nutrients
        };
    }
    bool found = record != NULL;
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return found;
}
#endif

void EcologyPopulationResetState(void)
{
    pthread_mutex_lock(&ecologyPopulationMutex);
    memset(ecologyPopulationRecords, 0, sizeof(ecologyPopulationRecords));
    free(ecologyPopulationColdRecords);
    ecologyPopulationColdRecords = NULL;
    ecologyPopulationColdCount = 0u;
    ecologyPopulationColdCapacity = 0u;
    ecologyPopulationActiveCount = 0u;
    ecologyPopulationFailedAdmissions = 0u;
#ifdef ECOLOGY_TESTING
    ecologyPopulationFailStorageAllocation = false;
#endif
    ecologyPopulationAccessSerial = 0u;
    ecologyPopulationEpoch++;
    if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    pthread_mutex_unlock(&ecologyPopulationMutex);
}

typedef struct EcologyPopulationCheckpointReference {
    const EcologyPopulationRecord *record;
    uint8_t active;
} EcologyPopulationCheckpointReference;

typedef struct EcologyPopulationCheckpointRecord {
    EcologyPopulationRecord record;
    uint8_t active;
} EcologyPopulationCheckpointRecord;

static int EcologyPopulationCompareCheckpointReferences(
    const void *first, const void *second)
{
    const EcologyPopulationCheckpointReference *a = first;
    const EcologyPopulationCheckpointReference *b = second;
    return EcologyPopulationCompareKeys(a->record, b->record);
}

static int EcologyPopulationCompareCheckpointRecords(
    const void *first, const void *second)
{
    const EcologyPopulationCheckpointRecord *a = first;
    const EcologyPopulationCheckpointRecord *b = second;
    return EcologyPopulationCompareKeys(&a->record, &b->record);
}

bool EcologyPopulationSaveState(FILE *file)
{
    bool success = false;
    EcologyPopulationCheckpointReference *records = NULL;
    pthread_mutex_lock(&ecologyPopulationMutex);
    if (!file) goto save_done;
    uint32_t count = (uint32_t)(ecologyPopulationActiveCount +
        ecologyPopulationColdCount);
    if (count > 0u) {
        records = calloc(count, sizeof(*records));
        if (!records) goto save_done;
    }
    uint32_t next = 0u;
    for (unsigned index = 0; index < ECOLOGY_POPULATION_MAX_REGIONS; index++) {
        if (!ecologyPopulationRecords[index].valid) continue;
        records[next++] = (EcologyPopulationCheckpointReference){
            .record = &ecologyPopulationRecords[index], .active = 1u
        };
    }
    for (size_t index = 0; index < ecologyPopulationColdCount; index++) {
        records[next++] = (EcologyPopulationCheckpointReference){
            .record = &ecologyPopulationColdRecords[index], .active = 0u
        };
    }
    if (count > 1u) qsort(records, count, sizeof(*records),
                         EcologyPopulationCompareCheckpointReferences);
    const uint32_t header[2] = { ECOLOGY_POPULATION_STATE_VERSION, count };
    if (fwrite(header, sizeof(header), 1, file) != 1 ||
        fwrite(&ecologyPopulationAccessSerial,
               sizeof(ecologyPopulationAccessSerial), 1, file) != 1) {
        goto save_done;
    }
    for (uint32_t index = 0; index < count; index++) {
        const EcologyPopulationRecord *record = records[index].record;
        if (index > 0u && EcologyPopulationCompareKeys(
                records[index - 1u].record, record) == 0) goto save_done;
        int32_t coordinates[2] = {
            (int32_t)record->regionX, (int32_t)record->regionZ
        };
        const float population[5] = {
            record->population.floraDensity,
            record->population.faunaDensity,
            record->population.floraCarryingCapacity,
            record->population.faunaCarryingCapacity,
            record->population.seasonalMemory
        };
        const float migration[6] = {
            record->migration.floraNet, record->migration.faunaNet,
            record->migration.floraFlowX, record->migration.floraFlowZ,
            record->migration.faunaFlowX, record->migration.faunaFlowZ
        };
        if (!EcologyPopulationStateValid(&record->population) ||
            !EcologyPopulationMigrationStateValid(&record->migration) ||
            !EcologyEvolutionStateValid(&record->evolution) ||
            record->nutrientInitialized > 1u ||
            (record->nutrientInitialized &&
             !EcologyNutrientStateValid(&record->nutrients)) ||
            !isfinite(record->lastUpdateTime) ||
            fwrite(&records[index].active,
                   sizeof(records[index].active), 1, file) != 1 ||
            fwrite(&record->surfaceId, sizeof(record->surfaceId), 1, file) != 1 ||
            fwrite(coordinates, sizeof(coordinates), 1, file) != 1 ||
            fwrite(&record->lastUpdateTime,
                   sizeof(record->lastUpdateTime), 1, file) != 1 ||
            fwrite(&record->lastAccess, sizeof(record->lastAccess), 1, file) != 1 ||
            fwrite(population, sizeof(population), 1, file) != 1 ||
            fwrite(migration, sizeof(migration), 1, file) != 1 ||
            fwrite(&record->population.faunaHarvestPressure,
                   sizeof(record->population.faunaHarvestPressure),
                   1, file) != 1 ||
            fwrite(&record->population.radiationMemory,
                   sizeof(record->population.radiationMemory), 1, file) != 1 ||
            fwrite(&record->evolution,
                   sizeof(record->evolution), 1, file) != 1 ||
            fwrite(&record->nutrientInitialized,
                   sizeof(record->nutrientInitialized), 1, file) != 1) {
            goto save_done;
        }
        if (record->nutrientInitialized &&
            fwrite(&record->nutrients, sizeof(record->nutrients), 1, file) != 1) {
            goto save_done;
        }
        if (!record->nutrientInitialized) {
            const EcologyNutrientState emptyNutrients = { 0 };
            if (fwrite(&emptyNutrients, sizeof(emptyNutrients), 1, file) != 1) {
                goto save_done;
            }
        }
    }
    success = true;
save_done:
    free(records);
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return success;
}

bool EcologyPopulationLoadState(FILE *file)
{
    bool success = false;
    EcologyPopulationCheckpointRecord *loaded = NULL;
    EcologyPopulationRecord *loadedCold = NULL;
    EcologyPopulationRecord loadedActive[ECOLOGY_POPULATION_MAX_REGIONS] = { 0 };
    pthread_mutex_lock(&ecologyPopulationMutex);
    uint32_t header[2];
    uint64_t loadedAccessSerial = 0u;
    if (!file || fread(header, sizeof(header), 1, file) != 1 ||
        fread(&loadedAccessSerial, sizeof(loadedAccessSerial), 1, file) != 1 ||
        (header[0] < ECOLOGY_POPULATION_LEGACY_STATE_VERSION ||
         header[0] > ECOLOGY_POPULATION_STATE_VERSION) ||
        header[1] > ECOLOGY_POPULATION_MAX_STORED_REGIONS ||
        (header[0] < ECOLOGY_POPULATION_STORAGE_STATE_VERSION &&
         header[1] > ECOLOGY_POPULATION_MAX_REGIONS)) {
        goto load_done;
    }

    if (header[1] > 0u) {
        loaded = calloc(header[1], sizeof(*loaded));
        if (!loaded) goto load_done;
    }
    size_t loadedColdCount = 0u;
    size_t loadedActiveCount = 0u;
    for (uint32_t item = 0; item < header[1]; item++) {
        uint8_t active = 1u;
        if (header[0] >= ECOLOGY_POPULATION_STORAGE_STATE_VERSION &&
            (fread(&active, sizeof(active), 1, file) != 1 || active > 1u)) {
            goto load_done;
        }
        uint32_t surfaceId = 0u;
        int32_t coordinates[2];
        double lastUpdateTime = 0.0;
        uint64_t lastAccess = 0u;
        float populationValues[5];
        float migrationValues[6] = { 0 };
        float faunaHarvestPressure = 0.0f;
        float radiationMemory = 0.0f;
        PlanetEvolutionRegion evolution = { 0 };
        EcologyNutrientState nutrients = { 0 };
        uint8_t nutrientInitialized = 0u;
        if (fread(&surfaceId, sizeof(surfaceId), 1, file) != 1 ||
            fread(coordinates, sizeof(coordinates), 1, file) != 1 ||
            fread(&lastUpdateTime, sizeof(lastUpdateTime), 1, file) != 1 ||
            fread(&lastAccess, sizeof(lastAccess), 1, file) != 1 ||
            fread(populationValues, sizeof(populationValues), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_MIGRATION_STATE_VERSION &&
            fread(migrationValues, sizeof(migrationValues), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_HARVEST_STATE_VERSION &&
            fread(&faunaHarvestPressure,
                  sizeof(faunaHarvestPressure), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_RADIATION_STATE_VERSION &&
            fread(&radiationMemory, sizeof(radiationMemory), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_EVOLUTION_STATE_VERSION &&
            fread(&evolution, sizeof(evolution), 1, file) != 1) {
            goto load_done;
        }
        if (header[0] >= ECOLOGY_POPULATION_NUTRIENT_STATE_VERSION &&
            (fread(&nutrientInitialized, sizeof(nutrientInitialized), 1, file) != 1 ||
             fread(&nutrients, sizeof(nutrients), 1, file) != 1)) {
            goto load_done;
        }
        PlanetRegionalPopulation population = {
            .floraDensity = populationValues[0],
            .faunaDensity = populationValues[1],
            .floraCarryingCapacity = populationValues[2],
            .faunaCarryingCapacity = populationValues[3],
            .seasonalMemory = populationValues[4],
            .faunaHarvestPressure = faunaHarvestPressure,
            .radiationMemory = radiationMemory
        };
        PlanetPopulationMigrationState migration = {
            .floraNet = migrationValues[0],
            .faunaNet = migrationValues[1],
            .floraFlowX = migrationValues[2],
            .floraFlowZ = migrationValues[3],
            .faunaFlowX = migrationValues[4],
            .faunaFlowZ = migrationValues[5]
        };
        if (surfaceId == 0u || !isfinite(lastUpdateTime) ||
            lastUpdateTime < 0.0 || lastAccess == 0u ||
            lastAccess > loadedAccessSerial ||
            coordinates[0] < INT_MIN / ECOLOGY_POPULATION_REGION_SIZE ||
            coordinates[0] > INT_MAX / ECOLOGY_POPULATION_REGION_SIZE ||
            coordinates[1] < INT_MIN / ECOLOGY_POPULATION_REGION_SIZE ||
            coordinates[1] > INT_MAX / ECOLOGY_POPULATION_REGION_SIZE ||
            !EcologyPopulationStateValid(&population) ||
            !EcologyPopulationMigrationStateValid(&migration) ||
            !EcologyEvolutionStateValid(&evolution) ||
            nutrientInitialized > 1u ||
            (nutrientInitialized && !EcologyNutrientStateValid(&nutrients))) {
            goto load_done;
        }

        loaded[item].active = active;
        loaded[item].record = (EcologyPopulationRecord){
            .valid = true,
            .surfaceId = surfaceId,
            .regionX = (int)coordinates[0],
            .regionZ = (int)coordinates[1],
            .lastUpdateTime = lastUpdateTime,
            .lastAccess = lastAccess,
            .population = population,
            .migration = migration,
            .evolution = evolution,
            .nutrients = nutrients,
            .nutrientInitialized = nutrientInitialized
        };
        if (active) loadedActiveCount++;
        else loadedColdCount++;
    }

    if (loadedActiveCount > ECOLOGY_POPULATION_MAX_REGIONS) goto load_done;
    if (header[1] > 1u) qsort(loaded, header[1], sizeof(*loaded),
                             EcologyPopulationCompareCheckpointRecords);
    if (loadedColdCount > 0u) {
        loadedCold = calloc(loadedColdCount, sizeof(*loadedCold));
        if (!loadedCold) goto load_done;
    }
    size_t coldIndex = 0u;
    for (uint32_t item = 0; item < header[1]; item++) {
        const EcologyPopulationRecord *record = &loaded[item].record;
        if (item > 0u && EcologyPopulationCompareKeys(
                &loaded[item - 1u].record, record) == 0) goto load_done;
        if (!loaded[item].active) {
            loadedCold[coldIndex++] = *record;
            continue;
        }
        unsigned start = EcologyPopulationSetIndex(
            record->surfaceId, record->regionX, record->regionZ) *
            ECOLOGY_POPULATION_SET_WAYS;
        EcologyPopulationRecord *slot = NULL;
        for (unsigned way = 0; way < ECOLOGY_POPULATION_SET_WAYS; way++) {
            if (!loadedActive[start + way].valid) {
                slot = &loadedActive[start + way];
                break;
            }
        }
        if (!slot) goto load_done;
        *slot = *record;
    }

    /* No live state is replaced until the entire checkpoint has passed all
     * validation and every allocation has succeeded. */
    memcpy(ecologyPopulationRecords, loadedActive, sizeof(loadedActive));
    free(ecologyPopulationColdRecords);
    ecologyPopulationColdRecords = loadedCold;
    loadedCold = NULL;
    ecologyPopulationColdCount = loadedColdCount;
    ecologyPopulationColdCapacity = loadedColdCount;
    ecologyPopulationActiveCount = loadedActiveCount;
    ecologyPopulationFailedAdmissions = 0u;
    ecologyPopulationAccessSerial = loadedAccessSerial;
    ecologyPopulationEpoch++;
    if (ecologyPopulationEpoch == 0u) ecologyPopulationEpoch = 1u;
    success = true;
load_done:
    free(loaded);
    free(loadedCold);
    pthread_mutex_unlock(&ecologyPopulationMutex);
    return success;
}
