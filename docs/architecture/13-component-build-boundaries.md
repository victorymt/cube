# Component Build Boundaries

## Ownership

Production code is owned by seven top-level components under `src/`: `core`,
`world`, `space`, `ecology`, `gameplay`, `presentation`, and `app`. Public
headers form the contract for a component; implementation-only details remain
in `*_internal.h` or `*_dependencies.h` files. The architecture test rejects
unqualified local includes and direct imports that bypass these contracts.

## Build seam

`mk/modules.mk` discovers sources within each ownership directory and the
Makefile compiles each source to its own dependency-tracked object. Those
objects are collected into one static archive per component under
`build/<variant>/modules/`. The executable links the seven archives in a
`--start-group`/`--end-group`, which handles the existing narrow cross-component
calls while keeping the artifacts independently rebuildable.

Use `make component-world` (or the corresponding target for another component)
to rebuild one archive. `make components` builds the complete set, and
`make -j components plugins` can build built-in components and optional shared
plugins concurrently.

## Dependency direction

`core` contains neutral infrastructure and cannot include higher-level domain
headers. `world`, `space`, `ecology`, and `gameplay` expose state through value
types, snapshots, and callbacks rather than mutable globals. `presentation`
consumes those contracts for rendering and audio. `app` is the composition root:
it owns the game loop, wires callbacks, coordinates persistence, and links the
components without moving domain policy into another component.

## Verification

- `make test-modules` builds all seven archives.
- `make component-world` demonstrates an isolated component build.
- `make test-architecture` checks archive composition, independent targets,
  include boundaries, and private-state rules.
- `make test` and `make test-sanitize` cover the composed executable and the
  component-level unit-test matrix.
