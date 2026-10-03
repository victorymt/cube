#ifndef VOXELCRAFT_GAME_PLUGIN_STREAM_H
#define VOXELCRAFT_GAME_PLUGIN_STREAM_H

#include "core/plugin.h"

bool GamePluginStreamSnapshot(void *userData,
                              VoxelcraftPluginStreamSnapshot *outSnapshot);
bool GamePluginStreamSnapshotV2(void *userData,
                                VoxelcraftPluginStreamSnapshotV2 *outSnapshot);

#endif
