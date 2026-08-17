#ifndef VOXELCRAFT_SPACE_WORLD_TRANSITION_H
#define VOXELCRAFT_SPACE_WORLD_TRANSITION_H

#include "space/space_types.h"

struct Player;

bool HomeWorldCanEnter(Vector3 position);
Vector3 HomeWorldSurfaceReturnPosition(void);
bool HomeWorldLaunchTarget(const SpaceTravelPose *surfacePose,
                           SpaceTravelPose *outSpacePose);
bool HomeWorldEnterSurface(void);
void HomeWorldLeaveSurface(Vector3 returnPosition);
void HomeWorldReset(void);
void HomeWorldRestoreLegacyState(const struct Player *player);
void HomeWorldRestoreLegacyStateForSpaceLayer(const struct Player *player,
                                              int storedLayerY);

bool PlanetWorldLandingTarget(Vector3 position, SpaceBodyInfo *out);
bool PlanetWorldEnterSurface(const SpaceBodyInfo *body,
                             Vector3 approachPosition);
float PlanetWorldAtmosphereEntryHeight(void);
bool PlanetWorldLaunchTarget(const SpaceTravelPose *surfacePose,
                             SpaceTravelPose *outSpacePose);
void PlanetWorldLeaveSurface(void);
void PlanetWorldReset(void);
void PlanetWorldMigrateSpaceLayer(int storedLayerY);

#endif
