#ifndef VOXELCRAFT_ECOLOGY_POPULATION_INTERNAL_H
#define VOXELCRAFT_ECOLOGY_POPULATION_INTERNAL_H

#include "ecology/ecology_internal.h"
#include "ecology/ecology_nutrient_model.h"

#include <pthread.h>

typedef struct EcologyPopulationRecord {
    bool valid;
    uint32_t surfaceId;
    int regionX;
    int regionZ;
    double lastUpdateTime;
    uint64_t lastAccess;
    PlanetRegionalPopulation population;
    PlanetPopulationMigrationState migration;
    PlanetEvolutionRegion evolution;
    EcologyNutrientState nutrients;
    uint8_t nutrientInitialized;
} EcologyPopulationRecord;

/* The storage implementation owns these records and their synchronization.
 * Population queries, events and step batches hold this mutex throughout use. */
extern EcologyPopulationRecord
    ecologyPopulationRecords[ECOLOGY_POPULATION_MAX_REGIONS];
extern uint32_t ecologyPopulationEpoch;
extern pthread_mutex_t ecologyPopulationMutex;

/* Both operations require the population mutex. Admission may relocate cold
 * storage: acquire working-set pointers only after all admissions finish. */
EcologyPopulationRecord *EcologyPopulationRecordAt(
    uint32_t surfaceId, int regionX, int regionZ, bool *created);
EcologyPopulationRecord *EcologyPopulationFindStoredRecord(
    uint32_t surfaceId, int regionX, int regionZ);

#endif
