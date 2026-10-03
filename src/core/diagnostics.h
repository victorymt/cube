#ifndef VOXELCRAFT_DIAGNOSTICS_H
#define VOXELCRAFT_DIAGNOSTICS_H

#include <stdint.h>

/*
 * Identity shared by all diagnostics captured for one observation point.
 * A zero field means that the caller did not have that part of the runtime
 * context available.
 */
typedef struct DiagnosticsCaptureIdentity {
    uint64_t captureId;
    uint64_t frameId;
    uint64_t simulationTick;
    uint64_t worldGeneration;
} DiagnosticsCaptureIdentity;

#endif
