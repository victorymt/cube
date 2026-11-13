#ifndef VOXELCRAFT_WORLD_COLLISION_H
#define VOXELCRAFT_WORLD_COLLISION_H

#include "raylib.h"

#include <stdbool.h>

/* World-space camera occupancy query. The world owns whether a point belongs
 * to a legacy map cell or a physical cube cell; gameplay consumers do not need
 * the backend storage API or topology kind. */
bool WorldPointInsideSolid(Vector3 position);

#endif
