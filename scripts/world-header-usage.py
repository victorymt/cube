#!/usr/bin/env python3
"""Report which `world/` headers a source file actually needs.

`src/world/world.h` used to declare every world-facing function. It is now an
aggregate that re-exports four focused headers, so a file can include exactly
the group it uses. This script maps a file's call sites onto those groups.

Usage:
    python3 scripts/world-header-usage.py FILE...     # per-file report
    python3 scripts/world-header-usage.py --rewrite FILE...

`--rewrite` replaces a `world/world.h` include with the precise ones and drops
the redundant line. It only touches includes, never declarations, and prints
what it changed so the diff is reviewable.
"""

import re
import sys

# Header -> the symbols it declares. Order matters: the rewrite emits them in
# this order so the resulting include block stays stable across runs.
GROUPS = {
    "world/world_state.h": [
        "WorldGetSeed", "WorldSetSeed", "WorldTerrainMode", "WorldSetTerrainMode",
        "WorldReset", "WorldCleanup",
    ],
    "world/block_types.h": [
        "BlockName", "IsColorBlock", "IsValidBlockType", "IsWaterBlock",
        "IsLiquidBlock", "BlockRenderShapeFor", "IsPlantBlock", "IsEcologyBlock",
        "IsStage05Block", "IsStage06Block", "IsStage05GeologyBlock",
        "IsBiogenicBlock", "IsFireResidueBlock", "BlockCollisionHeight",
        "BlockCollisionHeightAt", "BlockLiquidHeightAt", "IsTranslucentBlock",
        "ColorBlockIndex", "ColorBlockFromIndex", "ColorPalette256",
        "BlockBaseColor", "BlockMaterialResponseFor", "NearestImageBlock",
        "BlockMaterialResponse",
    ],
    "world/block_lighting.h": [
        "TorchLightAdd", "TorchLightRemove", "RebuildTorchList",
        "TorchLightAtBlockNearby", "CollectNearbyTorchLights",
    ],
    "world/block_edits.h": [
        "WorldGetEditCount", "WorldGetEditRevision", "WorldGetEditAt",
        "WorldGetEditDimensionAt", "WorldGetEditSurfaceAddressAt",
        "WorldGetEditSurfaceMapCellAt", "WorldGetEditForCurrentDimension",
        "WorldGetBlockEditForCurrentDimensionAt", "WorldBeginUndoGroup",
        "WorldEndUndoGroup", "UndoBlockEdit", "RedoBlockEdit",
        "ClearUndoHistory", "GetBlockAt", "GetBlockBaseAt",
        "SurfaceBlockReadyAt", "SetBlock", "SetBlockNoUndo",
        "SetBlockNoUndoFromSource", "WorldCurrentMutationSource",
        "SetBlockForImport", "WorldMutationSource", "WORLD_MUTATION_",
    ],
}

AGGREGATE = "world/world.h"
# Chunk notifications are the only declarations world.h still owns; a file that
# calls them keeps the aggregate include.
CHUNK_FUNCS = [
    "WorldNotifyChunkLoaded", "WorldNotifyChunkSectionLoaded",
    "WorldPrepareChunkSectionUnload",
]


def needed_groups(text):
    """Headers required by the calls present in `text`, in GROUPS order."""
    found = []
    for header, symbols in GROUPS.items():
        if any(re.search(r"\b" + re.escape(s), text) for s in symbols):
            found.append(header)
    return found


def needs_aggregate(text):
    return any(re.search(r"\b" + re.escape(s) + r"\s*\(", text) for s in CHUNK_FUNCS)


def rewrite(path):
    with open(path, encoding="utf-8") as handle:
        lines = handle.readlines()

    # Only rewrite a file that includes the aggregate exactly once; a duplicate
    # include is a separate defect and should be reported, not silently merged.
    indexes = [i for i, line in enumerate(lines)
               if line.strip() == f'#include "{AGGREGATE}"']
    if not indexes:
        return None
    if len(indexes) > 1:
        return f"{path}: duplicate aggregate include on lines " + \
               ",".join(str(i + 1) for i in indexes)

    body = "".join(lines)
    headers = needed_groups(body)
    keep_aggregate = needs_aggregate(body)
    if keep_aggregate:
        headers = headers + [AGGREGATE]

    if not headers:
        return f"{path}: includes {AGGREGATE} but uses none of its symbols"

    replace_at = indexes[0]
    replacement = [f'#include "{h}"\n' for h in headers]
    new_lines = lines[:replace_at] + replacement + lines[replace_at + 1:]
    with open(path, "w", encoding="utf-8") as handle:
        handle.writelines(new_lines)
    return f"{path}: {AGGREGATE} -> {', '.join(h.split('/')[1] for h in headers)}"


def report(path):
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    headers = needed_groups(text)
    if needs_aggregate(text):
        headers = headers + [AGGREGATE]
    print(f"{path}: {' '.join(headers) if headers else '(no world symbols used)'}")


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    rewrite_mode = argv[0] == "--rewrite"
    paths = argv[1:] if rewrite_mode else argv
    for path in paths:
        if rewrite_mode:
            message = rewrite(path)
            if message:
                print(message)
        else:
            report(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
