#ifndef VOXELCRAFT_INTERACTION_CUBE_H
#define VOXELCRAFT_INTERACTION_CUBE_H

#include "gameplay/interaction.h"
#include "raymath.h"

/* Cube-grid selection. Traces the ray against cube cells and reports the hit in
 * the local cell coordinates HitResult uses, so callers do not branch on the
 * grid. Install with InteractionSetCubeRaycastHook. */
HitResult InteractionCubeRaycast(Vector3 origin, Vector3 direction,
                                 float maxDistance, unsigned mask);

#endif
