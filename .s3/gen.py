macros_types = open('.s3/macros_types.txt').read().rstrip('\n')
pod = open('.s3/pod.txt').read().rstrip('\n')
hdr = '''#ifndef VOXELCRAFT_SURFACE_TYPES_H
#define VOXELCRAFT_SURFACE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* Plain-old-data surface identity. Deliberately free of raylib: surface keys
 * are stored, hashed and compared in modules that never draw anything, and
 * `raylib.h` alone rebuilds 187 of 235 objects. The one surface type that needs
 * a vector (SurfaceFrame) stays in surface_topology.h. */
'''
open('src/world/surface_types.h','w').write(hdr + macros_types + '\n\n' + pod + '\n\n#endif\n')
print("wrote surface_types.h")
